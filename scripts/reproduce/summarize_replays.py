#!/usr/bin/env python3
"""Summarize freshly executed fixed-source replay matrices."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


FIELDS = [
    "experiment",
    "row_id",
    "role",
    "case",
    "pattern",
    "status",
    "H_g",
    "H_lg",
    "H_ip",
    "H_f",
    "avg_displacement",
    "max_displacement",
    "runtime_seconds",
    "metrics_json",
]


def first_row(path: Path) -> dict[str, str]:
    with path.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    if len(rows) != 1:
        raise ValueError(f"expected one result in {path}, found {len(rows)}")
    return rows[0]


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    with args.manifest.open(newline="", encoding="utf-8") as stream:
        runs = list(csv.DictReader(stream, delimiter="\t"))

    output_rows = []
    for run in runs:
        result_path = Path(run["results_tsv"])
        result = first_row(result_path)
        metrics_path = Path(result.get("candidate_metrics", ""))
        if not metrics_path.is_file():
            raise ValueError(f"fresh replay metrics are missing: {metrics_path}")
        metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
        violations = (metrics.get("legality") or {}).get("placement_violations")
        if result.get("status") != "PASS" or metrics.get("status") != "ok":
            raise ValueError(f"fresh replay did not finish cleanly: {result_path}")
        if str(violations) not in ("0", "0.0"):
            raise ValueError(f"fresh replay is not explicitly legal: {metrics_path}")
        output_rows.append(
            {
                "experiment": run["experiment"],
                "row_id": run["row_id"],
                "role": run["role"],
                "case": result.get("case", run.get("case", "")),
                "pattern": run.get("pattern", ""),
                "status": result.get("status", ""),
                "H_g": result.get("hpwl_global_micron", ""),
                "H_lg": result.get("hpwl_legalized_micron", ""),
                "H_ip": result.get("hpwl_after_improve_micron", ""),
                "H_f": result.get("hpwl_after_micron", ""),
                "avg_displacement": result.get("avg_displacement_micron", ""),
                "max_displacement": result.get("max_displacement_micron", ""),
                "runtime_seconds": result.get("runtime_seconds", ""),
                "metrics_json": str(metrics_path),
            }
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=FIELDS, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(output_rows)
    print(f"[PASS] summarized {len(output_rows)} fresh EDA runs: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
