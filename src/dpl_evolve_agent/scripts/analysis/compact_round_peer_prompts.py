#!/usr/bin/env python3
"""Repair generated student prompts with current compact prompt fragments.

This is a post-generation repair tool for long-running rounds that were started
before prompt-generation code changed.  Source-of-truth rules stay in
`prompt_templates/teacher_loop/student/rules.md`.
"""
from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import resolve_runtime_paths
from scripts.teacher_loop.common import render_prompt_template
from scripts.teacher_loop.context_packets import peer_briefing


EVIDENCE_POINTER_NOTE = render_prompt_template("student/evidence_pointer_note.md")
WORKSPACE_BUDGET_NOTE = render_prompt_template("packets/workspace_budget_note.md")


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser()
    parser.add_argument("--round-id", required=True)
    parser.add_argument("--case", required=True, dest="case_id")
    parser.add_argument("--flow-variant", required=True)
    parser.add_argument(
        "--state-root",
        type=Path,
        help="Defaults to DPL_EVOLVE_STATE_ROOT through runtime path resolution.",
    )
    parser.add_argument(
        "--watch-seconds",
        type=int,
        default=0,
        help="Keep scanning for newly generated iterations for this many seconds.",
    )
    parser.add_argument("--interval", type=int, default=10)
    return parser.parse_args()


def prompt_iteration(path: Path) -> int | None:
    for part in path.parts:
        match = re.fullmatch(r"iter_(\d+)", part)
        if match:
            return int(match.group(1))
    return None


def prompt_student_id(path: Path) -> str | None:
    match = re.fullmatch(r"(student_\d+)\.md", path.name)
    return match.group(1) if match else None


def replace_section(
    text: str, *, header: str, replacement: str, next_header: str | None
) -> str:
    start = text.find(header)
    if start < 0:
        return text
    end = len(text) if next_header is None else text.find(next_header, start)
    if end < 0:
        end = len(text)
    return text[:start] + replacement.strip() + "\n\n" + text[end:].lstrip()


def replace_peer_section(prompt: str, peer_text: str) -> str:
    replacement = "## Use Peer Evidence\n\n" + peer_text.strip()
    if "## Use Peer Evidence" in prompt:
        next_header = "\n# Student Rules" if "\n# Student Rules" in prompt else "\n## Mission"
        return replace_section(
            prompt,
            header="## Use Peer Evidence",
            replacement=replacement,
            next_header=next_header,
        )
    marker = "\n## Mission"
    if marker in prompt:
        return prompt.replace(marker, "\n\n" + replacement + marker, 1)
    marker = "\n# Student Rules"
    if marker in prompt:
        return prompt.replace(marker, "\n\n" + replacement + marker, 1)
    return prompt


def replace_student_rules(prompt: str) -> str:
    rules = render_prompt_template("student/rules.md").strip()
    marker = "\n\n## Current Teacher Insight Packet"
    end_marker = marker if marker in prompt else None
    if "# Student Rules" in prompt:
        return replace_section(
            prompt,
            header="# Student Rules",
            replacement=rules,
            next_header=end_marker,
        )
    for header in ("## Mission", "## Required Code Surface", "## Implementation Standard"):
        if header in prompt:
            return replace_section(
                prompt,
                header=header,
                replacement=rules,
                next_header=end_marker,
            )
    return prompt.rstrip() + "\n\n" + rules + "\n"


def ensure_evidence_pointer(prompt: str) -> str:
    if "These paths are evidence pointers, not reading requirements." in prompt:
        return prompt
    marker = "- Teacher output:"
    start = prompt.find(marker)
    if start < 0:
        return prompt
    end = prompt.find("\n\n", start)
    if end < 0:
        return prompt
    return prompt[: end + 2] + EVIDENCE_POINTER_NOTE + "\n" + prompt[end + 2 :]


def ensure_workspace_budget_note(packet: str) -> str:
    return replace_section(
        packet,
        header="## Per-Iteration Execution Budget",
        replacement=WORKSPACE_BUDGET_NOTE,
        next_header="\n## Prepare",
    )


def compact_once(
    *,
    round_dir: Path,
    runtime: object,
    case_id: str,
    flow_variant: str,
) -> list[Path]:
    changed: list[Path] = []
    for prompt_path in sorted(round_dir.glob("iter_*/prompts/student_*.md")):
        iteration = prompt_iteration(prompt_path)
        student_id = prompt_student_id(prompt_path)
        if iteration is None or student_id is None:
            continue
        prompt = prompt_path.read_text(encoding="utf-8", errors="replace")
        peer_text = peer_briefing(
            runtime=runtime,
            round_dir=round_dir,
            case_id=case_id,
            flow_variant=flow_variant,
            iteration=iteration,
            current_student=student_id,
        )
        updated = replace_student_rules(
            ensure_evidence_pointer(replace_peer_section(prompt, peer_text))
        )
        if updated != prompt:
            prompt_path.write_text(updated, encoding="utf-8")
            changed.append(prompt_path)
    for packet_path in sorted(round_dir.glob("iter_*/packet/student_*_workspace.md")):
        packet = packet_path.read_text(encoding="utf-8", errors="replace")
        updated = ensure_workspace_budget_note(packet)
        if updated != packet:
            packet_path.write_text(updated, encoding="utf-8")
            changed.append(packet_path)
    return changed


def main() -> int:
    args = parse_args()
    runtime = resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="compact_round_peer_prompts.py",
    )
    state_root = args.state_root or runtime.state_root
    round_dir = state_root / args.round_id / "teacher_rounds"
    if not round_dir.is_dir():
        raise SystemExit(f"missing round dir: {round_dir}")

    deadline = time.monotonic() + max(0, args.watch_seconds)
    while True:
        changed = compact_once(
            round_dir=round_dir,
            runtime=runtime,
            case_id=args.case_id,
            flow_variant=args.flow_variant,
        )
        for path in changed:
            print(f"compacted {path}", flush=True)
        if args.watch_seconds <= 0 or time.monotonic() >= deadline:
            break
        time.sleep(max(1, args.interval))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
