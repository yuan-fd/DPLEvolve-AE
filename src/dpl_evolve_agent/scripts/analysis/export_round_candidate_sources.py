#!/usr/bin/env python3
"""Materialize per-iteration student source commits for matrix evaluation."""
from __future__ import annotations

import argparse
import json
import subprocess
from pathlib import Path


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError, KeyError, ValueError) as exc:
        return {}


def git_ref_exists(source_repo: Path, source_ref: str) -> bool:
    check = subprocess.run(
        ["git", "-C", str(source_repo), "rev-parse", "--verify", source_ref],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return check.returncode == 0


def git_archive(source_repo: Path, source_ref: str, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    archive = subprocess.Popen(
        ["git", "-C", str(source_repo), "archive", "--format=tar", source_ref],
        stdout=subprocess.PIPE,
    )
    assert archive.stdout is not None
    extract = subprocess.run(["tar", "-x", "-C", str(target)], stdin=archive.stdout)
    archive.stdout.close()
    archive_rc = archive.wait()
    if archive_rc:
        raise SystemExit(
            f"[ERROR] git archive failed for {source_repo}@{source_ref}"
        )
    if extract.returncode:
        raise SystemExit(f"[ERROR] tar extraction failed for {target}")


def resolve_source_ref(
    source_repo: Path, primary_ref: str | None, fallback_ref: str | None
) -> str | None:
    for ref in (primary_ref, fallback_ref):
        if ref and git_ref_exists(source_repo, ref):
            return ref
    return None


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export Teacher-round student source commits to plain trees."
    )
    parser.add_argument("--round-dir", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--student")
    parser.add_argument("--iteration")
    args = parser.parse_args()

    round_dir = args.round_dir.resolve()
    output_root = args.output_root.resolve()
    students_dir = round_dir / "students"
    if not students_dir.is_dir():
        raise SystemExit(f"[ERROR] Missing students directory: {students_dir}")

    print("student\titeration\tsource_repo\tsource_ref\tmaterialized_src")
    for student_root in sorted(students_dir.glob("student_*")):
        if args.student and student_root.name != args.student:
            continue
        lineage = load_json(student_root / "lineage.json")
        source_repo = student_root / "workspace" / "variant" / "dpl_evolve"
        for item in lineage.get("iterations", []):
            iteration = str(item.get("iteration") or "")
            if not iteration.startswith("iter_"):
                continue
            if args.iteration and iteration != args.iteration:
                continue
            source_candidate_ref = item.get("source_candidate_ref")
            source_commit = item.get("source_commit")
            source_ref = resolve_source_ref(
                source_repo, source_candidate_ref, source_commit
            )
            if not source_ref:
                record = (
                    student_root
                    / iteration
                    / "artifacts"
                    / "source_commit.json"
                )
                payload = load_json(record)
                source_candidate_ref = payload.get("source_candidate_ref")
                source_commit = payload.get("source_commit")
                source_ref = resolve_source_ref(
                    source_repo, source_candidate_ref, source_commit
                )
            if not source_ref:
                continue
            target = output_root / student_root.name / iteration / "dpl_evolve"
            if not (target / "CMakeLists.txt").is_file():
                git_archive(source_repo, str(source_ref), target)
            print(
                "\t".join(
                    [
                        student_root.name,
                        iteration,
                        str(source_repo),
                        str(source_ref),
                        str(target),
                    ]
                )
            )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
