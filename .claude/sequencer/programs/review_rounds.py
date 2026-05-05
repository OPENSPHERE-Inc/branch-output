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

_SEVERITY_COUNTS_SCHEMA = {
    "type": "object",
    "properties": {
        "critical": {"type": "integer", "minimum": 0},
        "major": {"type": "integer", "minimum": 0},
        "minor": {"type": "integer", "minimum": 0},
        "info": {"type": "integer", "minimum": 0},
    },
    "additionalProperties": True,
}

_REVIEW_INIT_SCHEMA = {
    "type": "object",
    "properties": {
        "doc_path": {"type": "string", "minLength": 1},
        "branch_dir": {"type": "string", "minLength": 1},
        "findings_total": {"type": "integer", "minimum": 0},
        "severity_counts": _SEVERITY_COUNTS_SCHEMA,
    },
    "required": ["doc_path", "branch_dir", "findings_total"],
    "additionalProperties": True,
}

_REVIEW_SCHEMA = {
    "type": "object",
    "properties": {
        "doc_path": {"type": "string", "minLength": 1},
        "findings_total": {"type": "integer", "minimum": 0},
        "severity_counts": _SEVERITY_COUNTS_SCHEMA,
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
        "maintain_count": {"type": "integer", "minimum": 0},
        "alternative_count": {"type": "integer", "minimum": 0},
        "downgrade_count": {"type": "integer", "minimum": 0},
        "code_changed": {"type": "boolean"},
        "summary_line": {"type": "string", "maxLength": 500},
    },
    "required": ["will_fix_count", "fixed_count", "wontfix_count", "code_changed"],
    "additionalProperties": True,
}

_RESOLVE_SCHEMA = {
    "type": "object",
    "properties": {
        "unresolved_count": {"type": "integer", "minimum": 0},
        "resolved_count": {"type": "integer", "minimum": 0},
        "feedback_count": {"type": "integer", "minimum": 0},
        "summary_line": {"type": "string", "maxLength": 500},
    },
    "required": ["unresolved_count"],
    "additionalProperties": True,
}

_FEEDBACK_SCHEMA = {
    "type": "object",
    "properties": {
        "unresolved_count": {"type": "integer", "minimum": 0},
        "resolved_count": {"type": "integer", "minimum": 0},
        "feedback_count": {"type": "integer", "minimum": 0},
        "code_changed": {"type": "boolean"},
        "summary_line": {"type": "string", "maxLength": 500},
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
    Run parallel-review against the branch diff for {base_clause}. Following the
    skill body, reviewers Write to individual files and an aggregator sub-agent
    consolidates them. The orchestrator (you) does not load review-finding bodies
    into context.

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

    Review-document language: the user's chat language.

    Reporting format (JSON):
    {{
      "doc_path": "<full path>",
      "branch_dir": "<branch_dir>",
      "findings_total": <int>,
      "severity_counts": {{"critical": <int>, "major": <int>, "minor": <int>, "info": <int>}}
    }}
    - branch_dir: must be reported because it is reused consistently across
      subsequent rounds and the final report.
    - severity_counts: copy directly from the aggregator sub-agent's return value.\
""")

_TPL_REVIEW = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 1: parallel-review]
    Skill: {skill}
    Run parallel-review against the branch diff for {base_clause}. Following the
    skill body, reviewers Write to individual files and an aggregator sub-agent
    consolidates them. The orchestrator (you) does not load review-finding bodies
    into context.

    File-naming rules:
    - Output path: {output_base}/{branch_dir}/review-round{round_num}.md
    - Use the {branch_dir} that was fixed in Round 1 (do not re-suffix).

    Review-document language: the user's chat language.

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

    Reporting format (JSON):
    {{
      "doc_path": "<full path>",
      "findings_total": <int>,
      "severity_counts": {{"critical": <int>, "major": <int>, "minor": <int>, "info": <int>}}
    }}\
""")

_TPL_RESPOND = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 2: review-respond]
    Skill: {skill}
    Respond to the findings in review document {doc_path}. Following the skill
    body, delegate parsing / triage / estimate / fix / format verification /
    build verification / aggregation to sub-agents. The orchestrator (you) only
    orchestrates the retry loop and does not load verdict bodies or finding
    bodies into context.
    {confirm_clause}
    {commit_clause}

    Additional constraints:
    - Do not pass the previous round's review document to anything other than the
      parsing sub-agent input.
    - Confirm the Will Fix count in the triage sub-agent's return value (also
      explicit when 0).
    - When evaluating spread signal e (Will Fix originating from a FIXME),
      verify whether the finding has its origin in a FIXME: / TODO: in the
      review text or the target files.

    Reporting format (JSON):
    {{
      "will_fix_count": <int>,
      "fixed_count": <int>,
      "wontfix_count": <int>,
      "maintain_count": <int>,
      "alternative_count": <int>,
      "downgrade_count": <int>,
      "code_changed": <bool>,
      "summary_line": "(<=200 chars 1-line summary; copy from the aggregator sub-agent's return value)"
    }}
    - will_fix_count: triage sub-agent's return value will_fix_count.
    - fixed_count: aggregator sub-agent's return value fixed_count
      (Maintain regular fixes + Alternative FIXME insertions; 0 if all Downgrade).
    - wontfix_count: triage sub-agent's return value wontfix_count.
    - maintain_count / alternative_count / downgrade_count: estimate aggregator
      sub-agent's return values.
    - code_changed: aggregator sub-agent's return value code_changed.
    - summary_line: 1-line summary for user notification (the leader holds only
      this 1 line in context).\
""")

_TPL_RESOLVE = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 3: review-resolve]
    Skill: {skill}
    Verify the resolution status of review document {doc_path}. Following the
    skill body, delegate parsing / per-finding verification / aggregation to
    sub-agents. The orchestrator (you) does not load verification bodies into
    context.

    Reporting format (JSON):
    {{
      "unresolved_count": <int>,
      "resolved_count": <int>,
      "feedback_count": <int>,
      "summary_line": "(<=200 chars 1-line summary; copy from the aggregator sub-agent's return value)"
    }}
    - unresolved_count: aggregator sub-agent's return value feedback_count
      (number of findings whose Verification still reads 💬 Feedback).
    - resolved_count: aggregator sub-agent's return value resolved_count.
    - feedback_count: synonymous with unresolved_count (both included for
      backward compatibility).
    - summary_line: 1-line summary for user notification.\
""")

_TPL_FEEDBACK = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 4: feedback re-fix (attempt {attempt}/{max_attempts})]
    For findings that still have a 💬 Feedback Verification in review document
    {doc_path}, run one cycle of {respond_skill}'s and {resolve_skill}'s
    sub-agent delegation flow.

    Step 4.{attempt}.1 Feedback triage
        Run {respond_skill} Step 1 (parse) → Step 2 (triage).
        Append to the triage prompt: prioritize triaging findings whose stage
        is "feedback" (current_meta.verification has Feedback details).

    Step 4.{attempt}.2 Feedback estimate
        Run {respond_skill} Step 3 (parallel estimate).
        Append to the estimate prompt: estimate taking the Feedback content in
        current_meta.verification into account. Consider Downgrade if cost
        inflates.
        If every finding is Downgraded, skip Step 4.{attempt}.3 and proceed to
        Step 4.{attempt}.4.

    Step 4.{attempt}.3 Feedback fix
        Run {respond_skill} Step 4 (fix) → Step 5 (format & build verification)
        → Step 6 (aggregation).
        Append to the fix prompt: re-fix taking the Feedback content in
        current_meta.verification into account.

    Step 4.{attempt}.4 Feedback verify
        Re-run {resolve_skill} (parsing Sub → verification Sub group → aggregator Sub).

    Reporting format (JSON):
    {{
      "unresolved_count": <int>,
      "resolved_count": <int>,
      "feedback_count": <int>,
      "code_changed": <bool>,
      "summary_line": "(<=200 chars 1-line summary)"
    }}
    - unresolved_count: review-resolve aggregator sub-agent's return value
      feedback_count after this attempt.
    - resolved_count: same return value's resolved_count.
    - feedback_count: synonymous with unresolved_count.
    - code_changed: whether even a single line of source code was modified
      during this attempt.
    - summary_line: 1-line summary for user notification.\
""")

_TPL_CONFIRM_ROUND = textwrap.dedent("""\
    [Round {round_num}/{max_rounds} Step 5: next-round confirmation (--confirm-round)]
    The round is ending with {unresolved} unresolved finding(s) remaining.
    Ask the user whether to proceed to the next round (Round {next_round}/{max_rounds}).

    Reporting format (JSON): {{"proceed": <bool>}}
    - proceed: true to advance to the next round; false to stop here.\
""")

_TPL_FINAL_REPORT = textwrap.dedent("""\
    [Step 6: final-report generation (delegated to the final-report aggregator sub-agent)]
    All {rounds_executed} round(s) are complete. Delegate to the final-report
    aggregator sub-agent to generate the final report.

    Output: {output_base}/{branch_dir}/final-report.md
    Termination reason: {termination_reason}

    Per-round review documents (input passed to the aggregator sub-agent):
    {round_docs_block}

    Per-round statistics (reference info passed to the aggregator sub-agent):
    {per_round_stats_block}

    Launch a sub-agent via the Agent tool with the following prompt:

    ```
    Generate the final report from all rounds' review documents.

    Input:
    - Per-round review documents (round_docs_block above)
    - Per-round statistics (per_round_stats_block above; reference info)
    - Termination reason: {termination_reason}
    - Report template: {format_path}
    - Output: {output_base}/{branch_dir}/final-report.md
    - Language: the user's chat language

    What to do:
    1. Read the template markdown to grasp its structure (<...> placeholders,
       table structure).
    2. Read each round's md and extract Triage / Estimate / Status / Verification
       values from <!-- METADATA(id) --> 〜 <!-- /METADATA(id) --> to obtain
       per-finding details (severity / location / summary / resolution /
       separate-PR recommendation status, etc.).
    3. Fill in the template's statistics summary, full finding list, future
       recommendations, and review-document index, then Write to the output.

    Return value: {{"report_path": "<full path>"}}
    ```

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
    lines: list[str] = []
    for r in round_records:
        sev = r.get("severity_counts") or {}
        sev_str = (
            f"crit={sev.get('critical', 0)},maj={sev.get('major', 0)},"
            f"min={sev.get('minor', 0)},info={sev.get('info', 0)}"
        )
        lines.append(
            f"    - Round {r['round_num']}: "
            f"findings={r['findings_total']} ({sev_str}), "
            f"will_fix={r['will_fix_count']}, "
            f"maintain={r.get('maintain_count', 0)}, "
            f"alternative={r.get('alternative_count', 0)}, "
            f"downgrade={r.get('downgrade_count', 0)}, "
            f"fixed={r['fixed_count']}, "
            f"wontfix={r['wontfix_count']}, "
            f"resolved={r.get('resolved_count', 0)}, "
            f"feedback_attempts={r['feedback_attempts']}, "
            f"unresolved={r['unresolved']}, "
            f"code_changed={r['code_changed']}"
        )
    return "\n".join(lines)


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
            "severity_counts": review_result.get("severity_counts") or {},
            "will_fix_count": 0,
            "fixed_count": 0,
            "wontfix_count": 0,
            "maintain_count": 0,
            "alternative_count": 0,
            "downgrade_count": 0,
            "resolved_count": 0,
            "feedback_attempts": 0,
            "unresolved": 0,
            "code_changed": False,
            "respond_summary_line": "",
            "resolve_summary_line": "",
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
        round_record["maintain_count"] = respond_result.get("maintain_count", 0)
        round_record["alternative_count"] = respond_result.get(
            "alternative_count", 0
        )
        round_record["downgrade_count"] = respond_result.get("downgrade_count", 0)
        round_record["code_changed"] = respond_result["code_changed"]
        round_record["respond_summary_line"] = respond_result.get(
            "summary_line", ""
        )

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
            round_record["resolved_count"] = resolve_result.get(
                "resolved_count", 0
            )
            round_record["resolve_summary_line"] = resolve_result.get(
                "summary_line", ""
            )

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
                if "resolved_count" in feedback_result:
                    round_record["resolved_count"] = feedback_result[
                        "resolved_count"
                    ]
                if feedback_result.get("summary_line"):
                    round_record["resolve_summary_line"] = feedback_result[
                        "summary_line"
                    ]
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
    total_maintain = sum(r.get("maintain_count", 0) for r in round_records)
    total_alternative = sum(r.get("alternative_count", 0) for r in round_records)
    total_downgrade = sum(r.get("downgrade_count", 0) for r in round_records)
    total_resolved = sum(r.get("resolved_count", 0) for r in round_records)
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
        "total_maintain": total_maintain,
        "total_alternative": total_alternative,
        "total_downgrade": total_downgrade,
        "total_resolved": total_resolved,
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
