#!/usr/bin/env python3
"""render-review.py — Append metadata before <!-- /METADATA(id) --> markers
in a parallel-review document, sourced from an events.jsonl event log.

Usage:
    python render-review.py <markdown-input> <events-jsonl> <markdown-output>

The output may be the same path as the input (in-place rewrite supported).
If <events-jsonl> is missing or empty, the markdown is copied through unchanged.

events.jsonl format (one JSON object per line):
    {"id": "C-1", "field": "triage",       "value": "🔧 Will Fix"}
    {"id": "C-1", "field": "estimate",     "value": "Direct"}
    {"id": "C-1", "field": "status",       "value": "🟢 Fixed"}
    {"id": "C-2", "field": "verification", "value": "✓ Verified"}

Behavior:
- Existing content between markers is preserved (no parsing of metadata lines).
- New events are inserted in fixed order (triage → estimate → status → verification)
  immediately before the closing marker.
- Same (id, field) pair in events: last write wins.
- Events for IDs whose closing marker is not found are appended at the end of the
  output as an "## Orphan Metadata" section.
- The script does NOT deduplicate; running it twice with the same events produces
  duplicate metadata lines. Each (id, field) is expected to be set at most once
  per markdown lifecycle (one render-respond run + one render-resolve run per round).
"""

import json
import os
import re
import sys

CLOSE_RE = re.compile(r"<!-- /METADATA\(([^)]+)\) -->")
DISPLAY_ORDER = ("triage", "estimate", "status", "verification")
LABELS = {
    "triage":       "Triage",
    "estimate":     "Estimate",
    "status":       "Status",
    "verification": "Verification",
}


def load_events(jsonl_path):
    """Read events.jsonl into {id: {field: value}}. Last write wins."""
    fields = {}
    if not os.path.exists(jsonl_path):
        return fields
    with open(jsonl_path, encoding="utf-8") as f:
        for raw in f:
            line = raw.strip()
            if not line:
                continue
            event = json.loads(line)
            fields.setdefault(event["id"], {})[event["field"]] = event["value"]
    return fields


def render(md_path, fields):
    """Render markdown lines and return (lines, seen_ids)."""
    seen_ids = set()
    out_lines = []
    with open(md_path, encoding="utf-8") as f:
        for raw in f:
            line = raw.rstrip("\n")
            m = CLOSE_RE.search(line)
            if m:
                cur_id = m.group(1)
                seen_ids.add(cur_id)
                # Append events in fixed order immediately before the closing marker.
                fv = fields.get(cur_id, {})
                for field in DISPLAY_ORDER:
                    if field in fv:
                        out_lines.append(f"- **{LABELS[field]}:** {fv[field]}")
            out_lines.append(line)
    return out_lines, seen_ids


def append_orphans(out_lines, fields, seen_ids):
    """Append a section at the end for events whose finding ID has no closing marker."""
    orphan_ids = sorted(set(fields.keys()) - seen_ids)
    if not orphan_ids:
        return
    out_lines.append("")
    out_lines.append("## Orphan Metadata")
    for orphan_id in orphan_ids:
        out_lines.append("")
        out_lines.append(f"### {orphan_id}")
        out_lines.append("")
        for field in DISPLAY_ORDER:
            if field in fields[orphan_id]:
                out_lines.append(f"- **{LABELS[field]}:** {fields[orphan_id][field]}")


def main():
    if len(sys.argv) != 4:
        sys.exit(f"Usage: {sys.argv[0]} <markdown-input> <events-jsonl> <markdown-output>")
    md_path, jsonl_path, out_path = sys.argv[1:4]

    if not os.path.exists(md_path):
        sys.exit(f"Error: markdown input not found: {md_path}")

    fields = load_events(jsonl_path)
    out_lines, seen_ids = render(md_path, fields)
    append_orphans(out_lines, fields, seen_ids)

    rendered = "\n".join(out_lines) + "\n"
    tmp_path = out_path + ".tmp"
    with open(tmp_path, "w", encoding="utf-8", newline="") as f:
        f.write(rendered)
    os.replace(tmp_path, out_path)


if __name__ == "__main__":
    main()
