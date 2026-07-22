#!/usr/bin/env python3
"""Summarize the 27 freshly executed Table 6 fixed/ReviewDSE runs."""

from __future__ import annotations

import argparse
import csv
import json
from pathlib import Path


def load_rows(path: Path) -> list[dict[str, str]]:
    if not path.is_file():
        raise ValueError(f"missing matrix result: {path}")
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--matrix-root", required=True, type=Path)
    parser.add_argument("--experiment-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()

    contract = json.loads(args.experiment_manifest.read_text(encoding="utf-8"))
    expected = {
        (row["case"], f"paper_table6_{row['case']}_{row['pattern'].replace('.', '_')}"): row
        for row in contract["table6"]["rows"]
    }
    programs = [
        ("diamond", "fixed_diamond"),
        ("negotiation", "fixed_negotiation"),
        ("evolved_negotiation_selected", "reviewdse"),
        ("evolved_negotiation_default_fail_probe8", "reviewdse"),
        ("evolved_negotiation_bpquad_center_probe", "reviewdse"),
    ]
    output_rows = []
    for program, role in programs:
        path = args.matrix_root / f"table6_{program}" / "results.tsv"
        for result in load_rows(path):
            key = (result["case"], result["flow_variant"])
            spec = expected.get(key)
            if spec is None:
                raise ValueError(f"unexpected Table 6 result row: {key}")
            if role == "reviewdse" and spec["program"] != program:
                raise ValueError(
                    f"wrong ReviewDSE source for {key}: expected {spec['program']}, got {program}"
                )
            metrics_path = Path(result.get("candidate_metrics", ""))
            metrics = {}
            if metrics_path.is_file():
                metrics = json.loads(metrics_path.read_text(encoding="utf-8"))
            legality = metrics.get("legality") or {}
            flow_status = metrics.get("flow_status") or metrics.get("status") or ""
            violations = legality.get("placement_violations", "")
            if not metrics_path.is_file():
                observed = "invalid"
            elif "timeout" in str(flow_status).lower():
                observed = "timeout"
            elif (
                result.get("status") == "PASS"
                and str(flow_status).lower() == "ok"
                and str(violations) in ("0", "0.0")
            ):
                observed = "pass"
            elif (
                str(flow_status).lower() == "ok"
                and str(violations) not in ("", "0", "0.0")
            ):
                observed = "fail"
            elif str(flow_status).lower() in ("flow_failed", "failed", "fail"):
                observed = "fail"
            else:
                observed = "invalid"
            expected_role = program if role != "reviewdse" else "reviewdse"
            expected_status = spec["expected"][expected_role]
            output_rows.append(
                {
                    "case": spec["case"],
                    "pattern": spec["pattern"],
                    "role": role,
                    "program": program,
                    "matrix_status": result.get("status", ""),
                    "flow_status": flow_status,
                    "placement_violations": violations,
                    "observed": observed,
                    "expected": expected_status,
                    "verdict": "match" if observed == expected_status else "mismatch",
                    "H_g": result.get("hpwl_global_micron", ""),
                    "H_lg": result.get("hpwl_legalized_micron", ""),
                    "H_ip": result.get("hpwl_after_improve_micron", ""),
                    "H_f": result.get("hpwl_after_micron", ""),
                    "runtime_seconds": result.get("runtime_seconds", ""),
                    "metrics_json": str(metrics_path),
                }
            )

    if len(output_rows) != 27:
        raise ValueError(f"expected 27 Table 6 executions, found {len(output_rows)}")
    fields = list(output_rows[0])
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(output_rows)
    mismatches = [f"{row['case']}/{row['pattern']}/{row['role']}" for row in output_rows if row["verdict"] != "match"]
    if mismatches:
        raise ValueError("fresh Table 6 outcomes differ from paper: " + ", ".join(mismatches))
    print(f"[PASS] summarized 27 fresh Table 6 runs: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
