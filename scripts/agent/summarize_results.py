#!/usr/bin/env python3
"""DPLEvolve AE — Agent Results Summarizer.

Reads structured experiment output (metrics.json, suite_runs.tsv, etc.)
and produces machine-readable summaries for table generation.
"""

from __future__ import annotations

import argparse
import csv
import json
import sys
from pathlib import Path
from typing import Any


def find_metrics_files(base_dir: Path) -> list[Path]:
    """Find all metrics.json files under a directory tree."""
    return sorted(base_dir.rglob("metrics.json"))


def find_suite_tsv(base_dir: Path) -> list[Path]:
    """Find all suite_runs.tsv files."""
    return sorted(base_dir.rglob("suite_runs.tsv"))


def read_metrics(path: Path) -> dict[str, Any]:
    """Read a metrics.json file."""
    with path.open(encoding="utf-8") as f:
        return json.load(f)


def read_tsv(path: Path) -> list[dict[str, str]]:
    """Read a TSV file into a list of dicts."""
    with path.open(encoding="utf-8") as f:
        reader = csv.DictReader(f, delimiter="\t")
        return list(reader)


def summarize_baseline_suite(results_dir: Path) -> dict[str, Any]:
    """Summarize baseline suite results into a table-ready format."""
    metrics_files = find_metrics_files(results_dir)
    suite_files = find_suite_tsv(results_dir)

    summary: dict[str, Any] = {
        "total_runs": len(metrics_files),
        "successful_runs": 0,
        "failed_runs": 0,
        "cases": {},
    }

    for mf in metrics_files:
        try:
            m = read_metrics(mf)
        except (json.JSONDecodeError, OSError):
            summary["failed_runs"] += 1
            continue

        if m.get("status") == "ok":
            summary["successful_runs"] += 1
        else:
            summary["failed_runs"] += 1

        case_id = Path(mf).parent.name
        hpwl = m.get("hpwl_stages", {}).get("final_micron")
        if case_id and hpwl is not None:
            summary["cases"][case_id] = {
                "final_hpwl": hpwl,
                "global_hpwl": m.get("hpwl_stages", {}).get("global_micron"),
                "instance_count": m.get("design_metrics", {}).get("instance_count"),
                "violations": m.get("legality", {}).get("placement_violations", ""),
            }

    if suite_files:
        try:
            rows = read_tsv(suite_files[0])
            summary["suite_table"] = rows
        except (csv.Error, OSError):
            pass

    return summary


def print_baseline_table(summary: dict[str, Any]) -> None:
    """Print baseline results as a formatted table."""
    print("\n=== Baseline Suite Summary ===\n")
    print(f"Total runs:      {summary['total_runs']}")
    print(f"Successful:      {summary['successful_runs']}")
    print(f"Failed:          {summary['failed_runs']}")
    print()

    if "suite_table" in summary:
        print("Suite Table (from suite_runs.tsv):")
        print("-" * 80)
        rows = summary["suite_table"]
        if rows:
            headers = list(rows[0].keys())
            print("\t".join(headers))
            for row in rows:
                print("\t".join(str(row.get(h, "")) for h in headers))
        print("-" * 80)
        print()

    if summary["cases"]:
        print("Per-Case HPWL Summary:")
        print("-" * 60)
        print(f"{'Case':<30} {'Final HPWL':>15}")
        print("-" * 60)
        for case_id, info in sorted(summary["cases"].items()):
            print(f"{case_id:<30} {info['final_hpwl']:>15.1f}")
        print("-" * 60)


def main() -> int:
    parser = argparse.ArgumentParser(
        description="Summarize DPLEvolve experiment results."
    )
    parser.add_argument(
        "--results-dir",
        type=Path,
        help="Directory containing experiment results.",
    )
    parser.add_argument(
        "--format",
        choices=["table", "json", "csv"],
        default="table",
        help="Output format.",
    )
    parser.add_argument(
        "--experiment",
        choices=["baseline", "smoke", "evolve", "bo"],
        default="baseline",
        help="Experiment type.",
    )
    args = parser.parse_args()

    results_dir = args.results_dir
    if results_dir is None:
        # Try to find results automatically
        candidates = [
            Path("results/reproduced"),
            Path("../OpenROAD-flow-scripts/flow/reports"),
        ]
        results_dir = None
        for c in candidates:
            if c.exists():
                results_dir = c
                break

    if results_dir is None or not results_dir.exists():
        print(
            "[ERROR] No results directory found. "
            "Run experiments first or use --results-dir.",
            file=sys.stderr,
        )
        return 1

    summary = summarize_baseline_suite(results_dir)

    if args.format == "json":
        print(json.dumps(summary, indent=2, default=str))
    elif args.format == "csv":
        if summary["cases"]:
            writer = csv.writer(sys.stdout)
            writer.writerow(["case", "final_hpwl", "global_hpwl", "instance_count"])
            for case_id, info in sorted(summary["cases"].items()):
                writer.writerow(
                    [
                        case_id,
                        info["final_hpwl"],
                        info["global_hpwl"],
                        info["instance_count"],
                    ]
                )
    else:
        print_baseline_table(summary)

    return 0


if __name__ == "__main__":
    sys.exit(main())
