#!/usr/bin/env python3
"""Compare 27 freshly executed cut-row replays with paper Table 6."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True, type=Path)
    parser.add_argument("--experiment-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    contract = json.loads(args.experiment_manifest.read_text(encoding="utf-8"))
    expected = {
        (row["case"], row["pattern"], role): (row, row["expected"][role])
        for row in contract["table6"]["rows"]
        for role in ("diamond", "negotiation", "reviewdse")
    }
    with args.results.open(newline="", encoding="utf-8") as stream:
        fresh = list(csv.DictReader(stream, delimiter="\t"))

    output_rows: list[dict[str, str]] = []
    seen: set[tuple[str, str, str]] = set()
    for result in fresh:
        key = (result["case"], result["pattern"], result["role"])
        if key in seen:
            raise ValueError(f"duplicate Table 6 execution: {key}")
        seen.add(key)
        if key not in expected:
            raise ValueError(f"unexpected Table 6 execution: {key}")
        spec, expected_status = expected[key]
        observed = result["status"].lower()
        if observed not in {"pass", "fail", "timeout"}:
            observed = "invalid"
        output_rows.append({
            "case": spec.get("paper_case", spec["case"]),
            "pattern": spec.get("paper_pattern", spec["pattern"]),
            "data_case": spec["case"],
            "data_pattern": spec["pattern"],
            "role": result["role"],
            "program": spec["program"] if result["role"] == "reviewdse" else result["role"],
            "observed": observed,
            "expected": expected_status,
            "verdict": "match" if observed == expected_status else "mismatch",
            "exit_code": result.get("exit_code", ""),
            "runtime_seconds": result.get("runtime_seconds", ""),
            "metrics_json": result.get("metrics_json", ""),
            "log": result.get("log", ""),
        })

    missing = sorted(set(expected) - seen)
    if missing:
        raise ValueError(f"expected 27 executions; missing {len(missing)}: {missing}")
    if len(output_rows) != 27:
        raise ValueError(f"expected 27 Table 6 executions, found {len(output_rows)}")

    output_rows.sort(key=lambda row: (row["data_case"], row["data_pattern"], row["role"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output_rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(output_rows)
    mismatches = [f"{r['data_case']}/{r['data_pattern']}/{r['role']}" for r in output_rows if r["verdict"] != "match"]
    if mismatches:
        raise ValueError("fresh Table 6 outcomes differ from paper: " + ", ".join(mismatches))
    print(f"[PASS] summarized 27 fresh Table 6 runs: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
