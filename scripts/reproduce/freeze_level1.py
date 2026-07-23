#!/usr/bin/env python3
"""Freeze a completed fresh Level 1 reconstruction for immutable Level 2 use."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
from typing import Any


EXPECTED_CASES = {
    "jpeg_util90_nangate45": "paper_level1_jpeg_util90",
    "aes_nangate45": "paper_level1_aes_util70",
    "swerv_wrapper_nangate45": "paper_level1_swerv_util60",
}
SOURCE_STARTS = (
    "framework_dpl_evolve",
    "diamond_dpl_evolve",
    "default_negotiation_dpl_evolve",
)


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def tree_digest(root: Path) -> str:
    """Hash portable source contents, never mutable Git bookkeeping."""
    digest = hashlib.sha256()
    paths = (
        path
        for path in root.rglob("*")
        if path.is_file() and ".git" not in path.relative_to(root).parts
    )
    for path in sorted(paths):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def load_json(path: Path) -> dict[str, Any]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict):
        raise ValueError(f"expected JSON object: {path}")
    return data


def portable(path: Path, state_root: Path) -> str:
    return "$DPL_EVOLVE_STATE_ROOT/" + path.relative_to(state_root).as_posix()


def require_file(path: Path, label: str) -> Path:
    if not path.is_file() or path.stat().st_size == 0:
        raise ValueError(f"missing or empty {label}: {path}")
    return path


def freeze_round(
    state_root: Path, round_id: str, expected_children: int
) -> tuple[dict[str, Any], str]:
    root = state_root / round_id
    manifest_path = require_file(root / "teacher_rounds" / "manifest.json", "round manifest")
    manifest = load_json(manifest_path)
    if manifest.get("round_id") != round_id:
        raise ValueError(f"round id mismatch in {manifest_path}")
    if manifest.get("calibration_mode") is not True:
        raise ValueError(f"round is not calibration mode: {round_id}")
    if manifest.get("dry_run") is not False or manifest.get("launch") is not True:
        raise ValueError(f"round is not a real launched run: {round_id}")
    if manifest.get("start_kind") != "framework":
        raise ValueError(f"Level 1 must start from framework: {round_id}")
    if manifest.get("teacher_model") != "gpt-5.5" or manifest.get(
        "teacher_reasoning_effort"
    ) != "xhigh":
        raise ValueError(f"unexpected Teacher profile: {round_id}")
    if manifest.get("student_model") != "gpt-5.4" or manifest.get(
        "student_reasoning_effort"
    ) != "xhigh":
        raise ValueError(f"unexpected Student profile: {round_id}")
    if float(manifest.get("student_runtime_multiplier", -1)) != 2.0:
        raise ValueError(f"Level 1 runtime gate is not 2x: {round_id}")

    case_id = str(manifest.get("case", ""))
    if case_id not in EXPECTED_CASES:
        raise ValueError(f"unexpected Level 1 case {case_id!r}: {round_id}")
    if manifest.get("flow_variant") != EXPECTED_CASES[case_id]:
        raise ValueError(f"unexpected flow variant for {case_id}: {round_id}")

    iterations = manifest.get("iterations")
    if not isinstance(iterations, list) or len(iterations) != 1:
        raise ValueError(f"Level 1 must contain exactly one iteration: {round_id}")
    iteration = iterations[0]
    if iteration.get("iteration") != "iter_01" or iteration.get("calibration_mode") is not True:
        raise ValueError(f"invalid calibration iteration record: {round_id}")
    children = iteration.get("children")
    if not isinstance(children, list) or len(children) != expected_children:
        raise ValueError(
            f"expected {expected_children} launched Students, found "
            f"{len(children) if isinstance(children, list) else 'invalid'}: {round_id}"
        )

    require_file(root / "start_seed_calibration" / "manifest.tsv", "three-branch seed calibration")
    operations = root / "checkpoints" / "operations"
    child_records: list[dict[str, Any]] = []
    for child in children:
        student_id = str(child.get("student_id", ""))
        operation_id = str(child.get("operation_id", ""))
        if not student_id or not operation_id:
            raise ValueError(f"malformed child record: {round_id}")
        invocation = require_file(
            operations / operation_id / "codex_invocation.json",
            f"Student invocation {operation_id}",
        )
        artifacts = root / "teacher_rounds" / "students" / student_id / "iter_01" / "artifacts"
        optional = {}
        for name in ("knowledge_card.md", "source_commit.json", "candidate_metrics_summary.json"):
            path = artifacts / name
            optional[name] = (
                {"path": portable(path, state_root), "sha256": sha256(path)}
                if path.is_file() and path.stat().st_size
                else None
            )
        child_records.append(
            {
                "student_id": student_id,
                "operation_id": operation_id,
                "invocation": {
                    "path": portable(invocation, state_root),
                    "sha256": sha256(invocation),
                },
                "artifacts": optional,
            }
        )

    review_operation = str(iteration.get("teacher_review_operation", ""))
    if not review_operation:
        raise ValueError(f"missing Teacher review operation: {round_id}")
    review = require_file(
        operations / review_operation / "codex_last_message.txt",
        f"Teacher final review {review_operation}",
    )
    review_invocation = require_file(
        operations / review_operation / "codex_invocation.json",
        f"Teacher review invocation {review_operation}",
    )
    review_text = review.read_text(encoding="utf-8", errors="replace").strip()
    if "calibration gate" not in review_text.lower() or "knowledge synthesis" not in review_text.lower():
        raise ValueError(f"Teacher final message lacks calibration review sections: {review}")

    record = {
        "round_id": round_id,
        "case": case_id,
        "flow_variant": manifest["flow_variant"],
        "students_launched": len(children),
        "iteration_count": 1,
        "teacher_review": {
            "evidence_id": f"{round_id}:iter_01:teacher_review",
            "operation_id": review_operation,
            "path": portable(review, state_root),
            "sha256": sha256(review),
            "invocation_path": portable(review_invocation, state_root),
            "invocation_sha256": sha256(review_invocation),
        },
        "children": child_records,
        "round_manifest": {
            "path": portable(manifest_path, state_root),
            "sha256": sha256(manifest_path),
        },
    }
    return record, review_text


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--round", action="append", required=True, dest="rounds")
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--manifest-output", type=Path)
    parser.add_argument("--children-per-case", required=True, type=int)
    args = parser.parse_args()

    if args.children_per_case < 1:
        raise ValueError("--children-per-case must be positive")
    if len(args.rounds) != 3 or len(set(args.rounds)) != 3:
        raise ValueError("Level 1 freeze requires exactly three distinct rounds")

    source_records = []
    seed_root = args.state_root / "seed_sources"
    for name in SOURCE_STARTS:
        source = seed_root / name
        require_file(source / "CMakeLists.txt", f"prepared source start {name}")
        source_records.append(
            {
                "source_start_id": name.removesuffix("_dpl_evolve"),
                "state_path": portable(source, args.state_root),
                "tree_sha256": tree_digest(source),
                "hash_scope": "all source files excluding .git metadata",
            }
        )

    round_records = []
    reviews = []
    for round_id in args.rounds:
        record, review = freeze_round(args.state_root, round_id, args.children_per_case)
        round_records.append(record)
        reviews.append((record, review))
    if {record["case"] for record in round_records} != set(EXPECTED_CASES):
        raise ValueError("Level 1 rounds do not cover the exact three calibration cases")

    lines = [
        "# Frozen ReviewDSE Level 1 Evidence",
        "",
        "Status: complete fresh public reconstruction; immutable input to Level 2.",
        "",
        "The main paper does not report the author-time Level 1 Student breadth, and",
        "the corresponding author-run packet was not retained. This packet therefore",
        "records a fresh run of the public reconstruction profile; it is not presented",
        "as an exact replay of the missing author-time search process.",
        "",
        f"Calibration breadth: {args.children_per_case} Students per case, one iteration.",
        "Teacher: GPT-5.5 xhigh. Students: GPT-5.4 xhigh. Runtime gate: 2x.",
        "",
        "## Complete Source Starts",
        "",
    ]
    for source in source_records:
        lines.append(
            f"- `{source['source_start_id']}`: tree SHA-256 "
            f"`{source['tree_sha256']}`; `{source['state_path']}`"
        )
    for record, review in reviews:
        lines.extend(
            [
                "",
                f"## `{record['case']}` Teacher Review",
                "",
                f"Evidence id: `{record['teacher_review']['evidence_id']}`",
                "",
                review,
            ]
        )
    packet_text = "\n".join(lines).rstrip() + "\n"
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(packet_text, encoding="utf-8")

    manifest_output = args.manifest_output or args.output.with_suffix(".json")
    payload = {
        "schema_version": 1,
        "status": "complete_fresh_public_reconstruction",
        "author_time_level1_packet_retained": False,
        "author_exact_student_breadth": None,
        "public_reconstruction_profile": {
            "students_per_case": args.children_per_case,
            "iterations": 1,
            "teacher": {"model": "gpt-5.5", "reasoning_effort": "xhigh"},
            "student": {"model": "gpt-5.4", "reasoning_effort": "xhigh"},
            "runtime_gate": 2.0,
        },
        "packet": {"path": args.output.name, "sha256": sha256(args.output)},
        "source_starts": source_records,
        "rounds": round_records,
    }
    manifest_output.write_text(json.dumps(payload, indent=2) + "\n", encoding="utf-8")
    print(
        f"[PASS] froze {len(round_records)} real Teacher-reviewed Level 1 rounds: "
        f"{args.output}"
    )
    print(f"[PASS] wrote machine-readable Level 1 manifest: {manifest_output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
