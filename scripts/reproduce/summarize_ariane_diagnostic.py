#!/usr/bin/env python3
"""Verify and summarize six fresh Ariane warm-start diagnostic replays."""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
from collections import defaultdict
from pathlib import Path


EXPECTED_MEANS = {
    "missed_handoff_sourceTopK": (1.5165369894134857, 1.0667049349977362),
    "level1_guided_handoff": (-3.2595354540577093, 0.7063805704676281),
}


def tree_digest(root: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(item for item in root.rglob("*") if item.is_file()):
        digest.update(path.relative_to(root).as_posix().encode())
        digest.update(b"\0")
        digest.update(hashlib.sha256(path.read_bytes()).digest())
    return digest.hexdigest()


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def verify_retained(path: Path, manifest: Path) -> None:
    expected_by_name = {}
    for line in manifest.read_text(encoding="utf-8").splitlines():
        if not line.strip():
            continue
        expected, name = line.split(maxsplit=1)
        expected_by_name[name.lstrip("* ")] = expected
    expected = expected_by_name.get(path.name)
    if not expected:
        raise ValueError(f"retained Ariane TSV is absent from manifest: {path.name}")
    actual = file_sha256(path)
    if actual != expected:
        raise ValueError(f"retained Ariane TSV checksum mismatch: {path}")

    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    candidates = [row for row in rows if row.get("iteration") != "mean"]
    values: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for row in candidates:
        if row.get("legality") != "clean":
            raise ValueError(f"retained Ariane row is not legal: {row.get('run_tag')}")
        values[row["group"]].append(
            (
                float(row["delta_hpwl_vs_default_percent"]),
                float(row["runtime_ratio_vs_default"]),
            )
        )
    for group, expected_pair in EXPECTED_MEANS.items():
        group_values = values[group]
        expected_count = 4 if group == "missed_handoff_sourceTopK" else 2
        if len(group_values) != expected_count:
            raise ValueError(f"retained Ariane group {group} has {len(group_values)} rows")
        actual_pair = (
            sum(value[0] for value in group_values) / len(group_values),
            sum(value[1] for value in group_values) / len(group_values),
        )
        if any(abs(actual - expected) > 1e-12 for actual, expected in zip(actual_pair, expected_pair)):
            raise ValueError(f"retained Ariane group mean mismatch: {group}")
    print("[PASS] retained Ariane diagnostic checksum and group means verified")


def load_config(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 6 or len({row["label"] for row in rows}) != 6:
        raise ValueError("Ariane diagnostic contract must contain six unique sources")
    counts = defaultdict(int)
    for row in rows:
        counts[row["group"]] += 1
    if dict(counts) != {
        "missed_handoff_sourceTopK": 4,
        "level1_guided_handoff": 2,
    }:
        raise ValueError(f"unexpected Ariane diagnostic group counts: {dict(counts)}")
    return rows


def verify_sources(rows: list[dict[str, str]], source_root: Path) -> None:
    for row in rows:
        source = source_root / row["label"] / "dpl_evolve"
        actual = tree_digest(source) if source.is_dir() else "missing"
        if actual != row["tree_sha256"]:
            raise ValueError(
                f"{row['label']} source digest mismatch: expected "
                f"{row['tree_sha256']}, got {actual}"
            )
    print("[PASS] six Ariane diagnostic source trees match their SHA-256 digests")


def metric_pair(path: Path) -> tuple[float, float]:
    payload = json.loads(path.read_text(encoding="utf-8"))
    if payload.get("status") != "ok":
        raise ValueError(f"metrics status is not ok: {path}")
    violations = (payload.get("legality") or {}).get("placement_violations")
    if str(violations) not in ("0", "0.0"):
        raise ValueError(f"metrics are not explicitly legal: {path}")
    hpwl = payload.get("hpwl") or payload.get("hpwl_openroad_log") or {}
    final = hpwl.get("after_micron")
    if final is None:
        final = (payload.get("hpwl_stages") or {}).get("final_micron")
    runtime = payload.get("runtime_seconds")
    if final is None or runtime is None:
        raise ValueError(f"metrics lack final HPWL/runtime: {path}")
    return float(final), float(runtime)


def load_run_manifest(path: Path) -> dict[str, Path]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    result = {row["label"]: Path(row["results_tsv"]) for row in rows}
    if len(result) != 6:
        raise ValueError(f"fresh run manifest must contain six rows, found {len(result)}")
    return result


def fresh_result(path: Path) -> tuple[float, float, Path]:
    with path.open(encoding="utf-8", newline="") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1 or rows[0].get("status") != "PASS":
        raise ValueError(f"expected one PASS replay row: {path}")
    row = rows[0]
    metrics_path = Path(row.get("candidate_metrics", ""))
    hpwl, runtime = metric_pair(metrics_path)
    return hpwl, runtime, metrics_path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True, type=Path)
    parser.add_argument("--source-root", required=True, type=Path)
    parser.add_argument("--check-sources", action="store_true")
    parser.add_argument("--retained-tsv", type=Path)
    parser.add_argument("--retained-manifest", type=Path)
    parser.add_argument("--run-manifest", type=Path)
    parser.add_argument("--default-metrics", type=Path)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--delta-tolerance-pp", type=float, default=0.25)
    parser.add_argument("--runtime-ratio-tolerance", type=float, default=0.20)
    args = parser.parse_args()

    config = load_config(args.config)
    verify_sources(config, args.source_root)
    if bool(args.retained_tsv) != bool(args.retained_manifest):
        raise ValueError("--retained-tsv and --retained-manifest must be supplied together")
    if args.retained_tsv:
        verify_retained(args.retained_tsv, args.retained_manifest)
    if args.check_sources and args.run_manifest is None:
        return 0
    if not args.run_manifest or not args.default_metrics or not args.output:
        raise ValueError(
            "fresh summary requires --run-manifest, --default-metrics, and --output"
        )

    default_hpwl, default_runtime = metric_pair(args.default_metrics)
    runs = load_run_manifest(args.run_manifest)
    output_rows: list[dict[str, object]] = []
    values: dict[str, list[tuple[float, float]]] = defaultdict(list)
    for spec in config:
        label = spec["label"]
        hpwl, runtime, metrics_path = fresh_result(runs[label])
        delta = 100.0 * (hpwl - default_hpwl) / default_hpwl
        runtime_ratio = runtime / default_runtime
        values[spec["group"]].append((delta, runtime_ratio))
        output_rows.append(
            {
                "label": label,
                "group": spec["group"],
                "kind": "candidate",
                "final_hpwl_micron": hpwl,
                "delta_percent": delta,
                "runtime_seconds": runtime,
                "runtime_ratio": runtime_ratio,
                "expected_delta_percent": spec["expected_delta_percent"],
                "expected_runtime_ratio": spec["expected_runtime_ratio"],
                "metrics_json": str(metrics_path),
                "verdict": "fresh",
            }
        )

    failures = []
    for group, expected in EXPECTED_MEANS.items():
        group_values = values[group]
        mean_delta = sum(item[0] for item in group_values) / len(group_values)
        mean_runtime = sum(item[1] for item in group_values) / len(group_values)
        delta_ok = abs(mean_delta - expected[0]) <= args.delta_tolerance_pp
        runtime_ok = abs(mean_runtime - expected[1]) <= args.runtime_ratio_tolerance
        verdict = "match" if delta_ok and runtime_ok else "mismatch"
        if verdict != "match":
            failures.append(group)
        output_rows.append(
            {
                "label": f"{group}_mean",
                "group": group,
                "kind": "mean",
                "final_hpwl_micron": "",
                "delta_percent": mean_delta,
                "runtime_seconds": "",
                "runtime_ratio": mean_runtime,
                "expected_delta_percent": expected[0],
                "expected_runtime_ratio": expected[1],
                "metrics_json": "",
                "verdict": verdict,
            }
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(
            stream,
            fieldnames=list(output_rows[0]),
            delimiter="\t",
            lineterminator="\n",
        )
        writer.writeheader()
        writer.writerows(output_rows)
    if failures:
        raise ValueError(
            "fresh Ariane diagnostic exceeds configured scientific tolerance: "
            + ", ".join(failures)
        )
    print(f"[PASS] fresh Ariane diagnostic reproduced both group means: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, csv.Error, json.JSONDecodeError) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
