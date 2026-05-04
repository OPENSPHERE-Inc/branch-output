"""review-rounds sequencer program.

Specification: ``.claude/skills/review-rounds/SKILL.md``

Drives parallel-review -> review-respond -> review-resolve per round, and while
unresolved findings remain runs an inner feedback re-fix loop (up to 3 attempts).
The Step 1 initialization (branch-name retrieval, branch_dir collision avoidance)
and the Step 3 final-report generation are also issued as Instructions.

Convergence checks:
  - findings_total == 0          -> Done
  - no code changes in the round -> Done
  - --confirm-round stop         -> Done
  - max_rounds reached           -> Abort
"""

from __future__ import annotations

import textwrap
from pathlib import Path

from agent_sequencer.api import Abort, Done, Instruction

NAME = "review-rounds"
DESCRIPTION = (
    "Full-featured review-rounds (with final-report generation) that uses the "
    "project's parallel-review / review-respond / review-resolve skills, "
    "implementing both the outer loop (up to N rounds) and the inner loop "
    "(feedback re-fix, up to 3 attempts)."
)

_DEFAULT_MAX_ROUNDS = 5
_DEFAULT_OUTPUT_BASE = ".claude/tmp"
_DEFAULT_FEEDBACK_ATTEMPTS = 3

PARAMS_SCHEMA = {
    "max_rounds": {
        "type": "integer",
        "default": _DEFAULT_MAX_ROUNDS,
        "minimum": 1,
        "maximum": 10,
        "description": "Maximum number of outer-loop rounds (1-10).",
    },
    "base": {
        "type": "string",
        "description": (
            "Base branch passed to parallel-review. "
            "If omitted, the agent resolves it from main / master."
        ),
    },
    "output_base": {
        "type": "string",
        "default": _DEFAULT_OUTPUT_BASE,
        "description": "Base output directory for review documents.",
    },
    "confirm": {
        "type": "boolean",
        "default": False,
        "description": (
            "When true, wait for user confirmation immediately after the "
            "review-respond estimate result (equivalent to the original "
            "SKILL's --confirm option)."
        ),
    },
    "confirm_round": {
        "type": "boolean",
        "default": False,
        "description": (
            "When true, if unresolved findings remain at the end of a round, "
            "wait for user confirmation before proceeding to the next round "
            "(equivalent to the original SKILL's --confirm-round option)."
        ),
    },
    "commit": {
        "type": "boolean",
        "default": False,
        "description": (
            "When true, perform an aggregated git commit after each finding "
            "fix (equivalent to the original SKILL's --commit option)."
        ),
    },
}

_PARALLEL_REVIEW_SKILL = ".claude/skills/parallel-review/SKILL.md"
_REVIEW_RESPOND_SKILL = ".claude/skills/review-respond/SKILL.md"
_REVIEW_RESOLVE_SKILL = ".claude/skills/review-resolve/SKILL.md"

# Adjacent bundle: markdown template for the final report.
# Resolved from __file__ so the path stays absolute regardless of plugin install
# location.
_FINAL_REPORT_FORMAT_PATH = (
    Path(__file__).resolve().parent / "review_rounds" / "final-report-format.md"
).as_posix()

_REVIEW_INIT_SCHEMA = {
    "type": "object",
    "properties": {
        "doc_path": {"type": "string", "minLength": 1},
        "branch_dir": {"type": "string", "minLength": 1},
        "findings_total": {"type": "integer", "minimum": 0},
    },
    "required": ["doc_path", "branch_dir", "findings_total"],
    "additionalProperties": True,
}

_REVIEW_SCHEMA = {
    "type": "object",
    "properties": {
        "doc_path": {"type": "string", "minLength": 1},
        "findings_total": {"type": "integer", "minimum": 0},
    },
    "required": ["doc_path", "findings_total"],
    "additionalProperties": True,
}

_RESPOND_SCHEMA = {
    "type": "object",
    "properties": {
        "will_fix_count": {"type": "integer", "minimum": 0},
        "fixed_count": {"type": "integer", "minimum": 0},
        "wontfix_count": {"type": "integer", "minimum": 0},
        "code_changed": {"type": "boolean"},
    },
    "required": ["will_fix_count", "fixed_count", "wontfix_count", "code_changed"],
    "additionalProperties": True,
}

_RESOLVE_SCHEMA = {
    "type": "object",
    "properties": {
        "unresolved_count": {"type": "integer", "minimum": 0},
    },
    "required": ["unresolved_count"],
    "additionalProperties": True,
}

_FEEDBACK_SCHEMA = {
    "type": "object",
    "properties": {
        "unresolved_count": {"type": "integer", "minimum": 0},
        "code_changed": {"type": "boolean"},
    },
    "required": ["unresolved_count", "code_changed"],
    "additionalProperties": True,
}

_USER_CONFIRM_SCHEMA = {
    "type": "object",
    "properties": {
        "proceed": {"type": "boolean"},
    },
    "required": ["proceed"],
    "additionalProperties": True,
}

_FINAL_REPORT_SCHEMA = {
    "type": "object",
    "properties": {
        "report_path": {"type": "string", "minLength": 1},
    },
    "required": ["report_path"],
    "additionalProperties": True,
}

# ----------------------------------------------------------------------
# Instruction templates
# ----------------------------------------------------------------------
# Expanded via str.format(), so JSON sample braces { } are escaped as {{ }}.

_TPL_REVIEW_INIT = textwrap.dedent("""\
    [Round 1/{max_rounds} Step 1: parallel-review (with initialization)]
    Skill: {skill}
    Run parallel-review against the branch diff for {base_clause}.

    Initialization:
    - Obtain the current branch name: git branch --show-current
    - Determine branch_dir:
      - Treat the branch name as a directory path (it may contain `/`).
      - If {output_base}/<branch> already exists, append a numeric suffix
        ({output_base}/<branch>_1, {output_base}/<branch>_2, ...) and use the
        smallest suffix that does not yet exist.
      - The resolved "<branch> or <branch>_N" is branch_dir.
    - Create the output directory with mkdir -p: {output_base}/<branch_dir>/

    File naming:
    - This round's (Round 1) output path: {output_base}/<branch_dir>/review-round1.md

    Review-document language: the user's chat language (Japanese for a Japanese
    conversation, English for English, etc.).

    Reporting format (JSON):
    {{"doc_path": "<full path>", "branch_dir": "<branch_dir>", "findings_total": <int>}}
    - branch_dir: must be reported because it is reused consistently across
      subsequent rounds and the final report.\
""")

_TPL_REVIEW = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 1: parallel-review]
    Skill: {skill}
    Run parallel-review against the branch diff for {base_clause}.

    File-naming rules:
    - Output path: {output_base}/{branch_dir}/review-round{round_num}.md
    - Use the {branch_dir} that was fixed in Round 1 (do not re-suffix).

    Review-document language: the user's chat language (Japanese for a Japanese
    conversation, English for English, etc.).

    Convergence-induction prevention:
    - Do not pass the previous round's review document to reviewers (bias avoidance).
    - Do not include past-round finding counts, count trends, or claims like
      "things are converging" in the reviewer prompt.
    - Do not include past-round finding IDs (C-1 / M-1 etc.) or past-round
      Fixed / Won't Fix statistics either.
    - Omitting parts of the template, or appending instructions to control the
      finding count, is prohibited.
    - The review orchestrator itself must not introduce findings outside the
      reviewers.

    Reporting format (JSON): {{"doc_path": "<full path>", "findings_total": <int>}}\
""")

_TPL_RESPOND = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 2: review-respond]
    Skill: {skill}
    Respond to the findings in review document {doc_path}.
    {confirm_clause}
    {commit_clause}

    Additional constraints:
    - Do not reference the previous round's review document.
    - The triage report must explicitly state the Will Fix count (also when 0).
    - When evaluating Spread signal e (Will Fix originating from a FIXME),
      verify whether the finding has its origin in a FIXME: / TODO: in the
      review text or the target files.

    Reporting format (JSON):
    {{"will_fix_count": <int>, "fixed_count": <int>, "wontfix_count": <int>, "code_changed": <bool>}}
    - will_fix_count: number of findings triaged as Will Fix.
    - fixed_count: number of Will Fix findings whose fix actually completed
      (sum of regular Maintain fixes and Alternative FIXME insertions; 0 if
      every finding was Downgraded).
    - wontfix_count: number of findings triaged as Won't Fix or estimated as
      Downgrade.
    - code_changed: whether even a single line of source code was modified in
      this step.\
""")

_TPL_RESOLVE = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 3: review-resolve]
    Skill: {skill}
    Verify the resolution status of review document {doc_path}.
    Reporting format (JSON): {{"unresolved_count": <int>}}
    - unresolved_count: number of findings whose Verification field still
      reads "Feedback".\
""")

_TPL_FEEDBACK = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 4: feedback re-fix (attempt {attempt}/{max_attempts})]
    For findings that still have a "Feedback" Verification in review document
    {doc_path}, perform the following four sub-steps in order.

    Step 4.{attempt}.1 Feedback triage
        Run Step 2 (Triage) of {respond_skill}.
        Additional constraint: prioritize findings whose Verification field
        reads "Feedback - ...".

    Step 4.{attempt}.2 Feedback estimate
        Run Step 3 (Estimate) of {respond_skill}.
        Additional constraint: for findings whose Verification reads
        "Feedback", perform the cost estimate taking the feedback content into
        account. If the feedback inflates the fix cost, consider Downgrade.
        If every finding is Downgraded, skip Step 4.{attempt}.3 and proceed to
        Step 4.{attempt}.4.

    Step 4.{attempt}.3 Feedback fix
        Run Steps 4-6 (Fix / Verification / Document update) of {respond_skill}.
        Additional constraint: for findings whose Verification reads
        "Feedback", re-fix taking the feedback content into account.

    Step 4.{attempt}.4 Feedback verify
        Run {resolve_skill} to re-evaluate any remaining feedback.

    Reporting format (JSON): {{"unresolved_count": <int>, "code_changed": <bool>}}
    - unresolved_count: number of findings whose Verification still reads
      "Feedback" after this attempt.
    - code_changed: whether even a single line of source code was modified
      during this attempt.\
""")

_TPL_CONFIRM_ROUND = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 5: next-round confirmation (--confirm-round)]
    The round is ending with {unresolved} unresolved finding(s) remaining.
    Ask the user whether to proceed to the next round (Round {next_round}/{max_rounds}).

    Reporting format (JSON): {{"proceed": <bool>}}
    - proceed: true to advance to the next round; false to stop here.\
""")

_TPL_FINAL_REPORT = textwrap.dedent("""\
    [Step 6: final-report generation]
    All {rounds_executed} round(s) are complete. Do not delegate to a sub-agent;
    this agent must generate the final report itself.

    Output path: {output_base}/{branch_dir}/final-report.md
    Termination reason: {termination_reason}
    Report language: the user's chat language (Japanese for a Japanese
    conversation, English for English, etc.).

    Per-round review documents:
    {round_docs_block}

    Per-round statistics (aggregated by the program):
    {per_round_stats_block}

    Procedure:
    1. Read template {format_path} and use its markdown structure as the
       skeleton. Replace `<...>` placeholders and the "..." cells in tables
       with real data.
    2. Read each review document and pull per-finding details (severity,
       location, summary, resolution, separate-PR recommendation, etc.) from
       the Triage / Estimate / Status / Verification fields between the
       metadata markers.
    3. Following the template structure, fill in the statistics summary,
       full finding list, future recommendations, and review-document index,
       then Write the result to the output path.

    Reporting format (JSON): {{"report_path": "<full path>"}}\
""")


def _format_round_docs_block(round_records: list[dict]) -> str:
    """Emit each round's doc_path as a list."""
    if not round_records:
        return "    (none)"
    return "\n".join(
        f"    - Round {r['round_num']}: {r['doc_path']}"
        for r in round_records
    )


def _format_per_round_stats_block(round_records: list[dict]) -> str:
    """Emit per-round statistics one line each (basis data for the final report)."""
    if not round_records:
        return "    (none)"
    return "\n".join(
        (
            f"    - Round {r['round_num']}: "
            f"findings={r['findings_total']}, "
            f"will_fix={r['will_fix_count']}, "
            f"fixed={r['fixed_count']}, "
            f"wontfix={r['wontfix_count']}, "
            f"feedback_attempts={r['feedback_attempts']}, "
            f"unresolved={r['unresolved']}, "
            f"code_changed={r['code_changed']}"
        )
        for r in round_records
    )


def run(ctx):
    """Sequencer program body that drives the round loop."""
    max_rounds = ctx.params.get("max_rounds", _DEFAULT_MAX_ROUNDS)
    base = ctx.params.get("base")
    output_base = ctx.params.get("output_base", _DEFAULT_OUTPUT_BASE)
    confirm = ctx.params.get("confirm", False)
    confirm_round = ctx.params.get("confirm_round", False)
    commit = ctx.params.get("commit", False)

    base_clause = (
        f"base branch {base}"
        if base
        else "the default base branch (main or master)"
    )
    confirm_clause = (
        "Option: --confirm enabled (wait for user confirmation immediately "
        "after the estimate result)."
        if confirm
        else "Option: --confirm disabled (continue without pausing after the estimate)."
    )
    commit_clause = (
        "Option: --commit enabled (perform an aggregated git commit after fixes)."
        if commit
        else "Option: --commit disabled (no commits)."
    )

    branch_dir: str | None = None
    round_records: list[dict] = []
    converged = False
    termination_reason: str | None = None

    for round_num in range(1, max_rounds + 1):
        ctx.publish_progress(
            current=round_num,
            of=max_rounds,
            label=f"Round {round_num}/{max_rounds}",
        )

        # ----- Step 1: parallel-review -----
        # Round 1: use the initialization-included template to fix branch_dir.
        # Round 2+: pass the fixed branch_dir to reuse.
        if round_num == 1:
            review_result = yield Instruction(
                text=_TPL_REVIEW_INIT.format(
                    max_rounds=max_rounds,
                    skill=_PARALLEL_REVIEW_SKILL,
                    base_clause=base_clause,
                    output_base=output_base,
                ),
                expect_schema=_REVIEW_INIT_SCHEMA,
                timeout_minutes=60,
            )
            branch_dir = review_result["branch_dir"]
        else:
            review_result = yield Instruction(
                text=_TPL_REVIEW.format(
                    round_num=round_num,
                    max_rounds=max_rounds,
                    skill=_PARALLEL_REVIEW_SKILL,
                    base_clause=base_clause,
                    output_base=output_base,
                    branch_dir=branch_dir,
                ),
                expect_schema=_REVIEW_SCHEMA,
                timeout_minutes=60,
            )

        doc_path = review_result["doc_path"]
        findings_total = review_result["findings_total"]

        round_record = {
            "round_num": round_num,
            "doc_path": doc_path,
            "findings_total": findings_total,
            "will_fix_count": 0,
            "fixed_count": 0,
            "wontfix_count": 0,
            "feedback_attempts": 0,
            "unresolved": 0,
            "code_changed": False,
        }

        # ----- Convergence check 1: zero findings -----
        if findings_total == 0:
            round_records.append(round_record)
            converged = True
            termination_reason = "Converged: zero findings"
            break

        # ----- Step 2: review-respond -----
        respond_result = yield Instruction(
            text=_TPL_RESPOND.format(
                round_num=round_num,
                max_rounds=max_rounds,
                skill=_REVIEW_RESPOND_SKILL,
                doc_path=doc_path,
                confirm_clause=confirm_clause,
                commit_clause=commit_clause,
            ),
            expect_schema=_RESPOND_SCHEMA,
            timeout_minutes=180,
        )

        round_record["will_fix_count"] = respond_result["will_fix_count"]
        round_record["fixed_count"] = respond_result["fixed_count"]
        round_record["wontfix_count"] = respond_result["wontfix_count"]
        round_record["code_changed"] = respond_result["code_changed"]

        # If fixed_count == 0 there is nothing to verify, so skip Steps 3-4.
        if respond_result["fixed_count"] > 0:
            # ----- Step 3: review-resolve -----
            resolve_result = yield Instruction(
                text=_TPL_RESOLVE.format(
                    round_num=round_num,
                    max_rounds=max_rounds,
                    skill=_REVIEW_RESOLVE_SKILL,
                    doc_path=doc_path,
                ),
                expect_schema=_RESOLVE_SCHEMA,
                timeout_minutes=30,
            )
            round_record["unresolved"] = resolve_result["unresolved_count"]

            # ----- Step 4: inner loop - feedback re-fix (up to 3 attempts) -----
            for attempt in range(1, _DEFAULT_FEEDBACK_ATTEMPTS + 1):
                if round_record["unresolved"] == 0:
                    break

                ctx.publish_progress(
                    current=round_num,
                    of=max_rounds,
                    label=(
                        f"Round {round_num}/{max_rounds} - "
                        f"feedback attempt {attempt}/{_DEFAULT_FEEDBACK_ATTEMPTS}"
                    ),
                )

                feedback_result = yield Instruction(
                    text=_TPL_FEEDBACK.format(
                        round_num=round_num,
                        max_rounds=max_rounds,
                        attempt=attempt,
                        max_attempts=_DEFAULT_FEEDBACK_ATTEMPTS,
                        doc_path=doc_path,
                        respond_skill=_REVIEW_RESPOND_SKILL,
                        resolve_skill=_REVIEW_RESOLVE_SKILL,
                    ),
                    expect_schema=_FEEDBACK_SCHEMA,
                    timeout_minutes=120,
                )
                round_record["feedback_attempts"] += 1
                round_record["unresolved"] = feedback_result["unresolved_count"]
                if feedback_result["code_changed"]:
                    round_record["code_changed"] = True

        round_records.append(round_record)

        # ----- Convergence check 2: no source-code changes -----
        if not round_record["code_changed"]:
            converged = True
            termination_reason = "Converged: no source code changes"
            break

        # ----- Next-round confirmation (--confirm-round + unresolved + rounds remain) -----
        if confirm_round and round_record["unresolved"] > 0 and round_num < max_rounds:
            confirm_result = yield Instruction(
                text=_TPL_CONFIRM_ROUND.format(
                    round_num=round_num,
                    max_rounds=max_rounds,
                    next_round=round_num + 1,
                    unresolved=round_record["unresolved"],
                ),
                expect_schema=_USER_CONFIRM_SCHEMA,
                timeout_minutes=60,
            )
            if not confirm_result["proceed"]:
                termination_reason = "Round loop stopped by user request"
                break
    else:
        # for/else: no break taken == max_rounds reached.
        termination_reason = f"Maximum round count {max_rounds} reached"

    # ----- Step 6: final-report generation -----
    report_path: str | None = None
    if branch_dir is not None and round_records:
        report_result = yield Instruction(
            text=_TPL_FINAL_REPORT.format(
                rounds_executed=len(round_records),
                output_base=output_base,
                branch_dir=branch_dir,
                termination_reason=termination_reason,
                format_path=_FINAL_REPORT_FORMAT_PATH,
                round_docs_block=_format_round_docs_block(round_records),
                per_round_stats_block=_format_per_round_stats_block(round_records),
            ),
            expect_schema=_FINAL_REPORT_SCHEMA,
            timeout_minutes=30,
        )
        report_path = report_result["report_path"]

    # ----- Cumulative aggregation -----
    total_will_fix = sum(r["will_fix_count"] for r in round_records)
    total_fixed = sum(r["fixed_count"] for r in round_records)
    total_wontfix = sum(r["wontfix_count"] for r in round_records)
    total_feedback_attempts = sum(r["feedback_attempts"] for r in round_records)
    last_unresolved = round_records[-1]["unresolved"] if round_records else 0

    summary = {
        "rounds_executed": len(round_records),
        "converged": converged,
        "reason": termination_reason,
        "branch_dir": branch_dir,
        "report_path": report_path,
        "total_will_fix": total_will_fix,
        "total_fixed": total_fixed,
        "total_wontfix": total_wontfix,
        "total_feedback_attempts": total_feedback_attempts,
        "last_unresolved": last_unresolved,
        "round_records": round_records,
    }

    if converged or termination_reason == "Round loop stopped by user request":
        yield Done(summary=summary)
    else:
        yield Abort(
            reason=(
                f"{termination_reason} (did not converge). "
                f"Cumulative will_fix={total_will_fix}, fixed={total_fixed}, "
                f"wontfix={total_wontfix}, feedback_attempts={total_feedback_attempts}; "
                f"unresolved at the last round={last_unresolved}. "
                f"Final report: {report_path or '(not generated)'}. "
                "Increase max_rounds or review the unresolved findings."
            )
        )
