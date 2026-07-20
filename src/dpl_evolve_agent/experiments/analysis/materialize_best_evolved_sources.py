#!/usr/bin/env python3
"""Materialize best evolved source programs from an article source table."""

from __future__ import annotations

import argparse
import csv
import json
import subprocess
from pathlib import Path


def load_json(path: Path) -> dict:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError, KeyError, ValueError) as exc:
        return {}


def git_output(repo: Path, *args: str, check: bool = True) -> str:
    result = subprocess.run(
        ["git", "-C", str(repo), *args],
        check=False,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
    )
    if check and result.returncode != 0:
        raise SystemExit(
            f"[ERROR] git -C {repo} {' '.join(args)} failed:\n{result.stderr}"
        )
    return result.stdout.strip()


def ref_has_tree(repo: Path, ref: str | None) -> bool:
    if not ref:
        return False
    result = subprocess.run(
        ["git", "-C", str(repo), "rev-parse", "--verify", f"{ref}^{{tree}}"],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.DEVNULL,
    )
    return result.returncode == 0


def archive_ref(repo: Path, ref: str, target: Path) -> None:
    target.mkdir(parents=True, exist_ok=True)
    archive = subprocess.Popen(
        ["git", "-C", str(repo), "archive", "--format=tar", ref],
        stdout=subprocess.PIPE,
    )
    assert archive.stdout is not None
    extract = subprocess.run(["tar", "-x", "-C", str(target)], stdin=archive.stdout)
    archive.stdout.close()
    archive_rc = archive.wait()
    if archive_rc != 0:
        raise SystemExit(f"[ERROR] git archive failed for {repo}@{ref}")
    if extract.returncode != 0:
        raise SystemExit(f"[ERROR] tar extraction failed for {target}")


def resolve_table_path(raw_path: str, source_table: Path) -> Path:
    """Resolve source-table paths across old and current article layouts."""
    path = Path(raw_path)
    if path.is_absolute():
        return path

    candidates = [
        Path.cwd() / path,
        source_table.parent / path,
        source_table.parent.parent / path,
        source_table.parent.parent.parent / path,
        source_table.parent.parent.parent.parent / path,
    ]
    for candidate in candidates:
        if candidate.exists():
            return candidate.resolve()
    return (source_table.parent.parent.parent / path).resolve()


def candidate_repos(record_path: Path, payload: dict) -> list[Path]:
    repos: list[Path] = []
    raw_repo = payload.get("source_repo")
    if raw_repo:
        repos.append(Path(raw_repo))
    student_root = record_path.parents[2]
    repos.append(student_root / "workspace" / "variant" / "dpl_evolve")
    deduped: list[Path] = []
    seen: set[str] = set()
    for repo in repos:
        key = str(repo)
        if key not in seen:
            deduped.append(repo)
            seen.add(key)
    return deduped


def resolve_source(record_path: Path) -> tuple[Path, str, dict]:
    payload = load_json(record_path)
    refs = [
        payload.get("source_candidate_ref"),
        payload.get("source_commit"),
        payload.get("source_branch"),
    ]
    for repo in candidate_repos(record_path, payload):
        if not (repo / ".git").exists() and not (repo / ".git").is_file():
            continue
        for ref in refs:
            if ref_has_tree(repo, ref):
                commit = git_output(repo, "rev-parse", ref)
                return repo, commit, payload
    raise RuntimeError(f"could not resolve archived source tree from {record_path}")


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Export best evolved source programs selected by the article table."
    )
    parser.add_argument("--source-table", required=True, type=Path)
    parser.add_argument("--output-root", required=True, type=Path)
    parser.add_argument("--manifest", required=True, type=Path)
    args = parser.parse_args()

    args.output_root.mkdir(parents=True, exist_ok=True)
    args.manifest.parent.mkdir(parents=True, exist_ok=True)

    with args.source_table.open(encoding="utf-8", newline="") as stream:
        rows = [
            row
            for row in csv.DictReader(stream, delimiter="\t")
            if row.get("method") == "OpenROAD Evolve"
        ]
    if not rows:
        raise SystemExit(f"[ERROR] No OpenROAD Evolve rows in {args.source_table}")

    fields = [
        "program",
        "discovery_case",
        "source_repo",
        "source_commit",
        "materialized_src",
        "metrics_summary",
        "round",
    ]
    skipped: list[tuple[str, str]] = []
    with args.manifest.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t")
        writer.writeheader()
        for row in rows:
            discovery_case = row["case"]
            program = f"P_{discovery_case}"
            source_field = row.get("source") or row.get("path") or ""
            if not source_field:
                raise SystemExit(
                    "[ERROR] OpenROAD Evolve source row has neither 'source' nor 'path' column"
                )
            metrics_summary = resolve_table_path(source_field, args.source_table)
            record_path = metrics_summary.parent / "source_commit.json"
            if not record_path.is_file():
                raise SystemExit(f"[ERROR] Missing source commit record: {record_path}")
            try:
                repo, commit, _payload = resolve_source(record_path)
            except RuntimeError as exc:
                skipped.append((program, str(exc)))
                print(f"[WARN] skip {program}: {exc}")
                continue
            target = args.output_root / program / "dpl_evolve"
            if not (target / "CMakeLists.txt").is_file():
                archive_ref(repo, commit, target)
            writer.writerow(
                {
                    "program": program,
                    "discovery_case": discovery_case,
                    "source_repo": str(repo),
                    "source_commit": commit,
                    "materialized_src": str(target),
                    "metrics_summary": str(metrics_summary),
                    "round": row.get("round", ""),
                }
            )
            print(f"{program}\t{discovery_case}\t{commit}\t{target}")
    if skipped:
        skipped_path = args.manifest.with_suffix(".skipped.tsv")
        with skipped_path.open("w", encoding="utf-8", newline="") as stream:
            writer = csv.writer(stream, delimiter="\t")
            writer.writerow(["program", "reason"])
            writer.writerows(skipped)
        print(f"[WARN] skipped {len(skipped)} unrecoverable source rows: {skipped_path}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
