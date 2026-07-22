#!/usr/bin/env python3
"""Compute Table 5 composability deltas from fresh fixed-source replay metrics."""

from __future__ import annotations

import argparse
import csv
from pathlib import Path


def number(row: dict[str, str], field: str) -> float:
    value = row.get(field, "")
    if value == "":
        raise ValueError(f"{row.get('row_id')}/{row.get('role')} has no {field}")
    return float(value)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--fresh-runs", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    with args.fresh_runs.open(newline="", encoding="utf-8") as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))

    grouped: dict[str, dict[str, dict[str, str]]] = {}
    for row in rows:
        grouped.setdefault(row["row_id"], {})[row["role"]] = row
    output = []
    for row_id in ("aes_dense_n45", "jpeg_dense_n45", "swerv_dense_n45"):
        roles = grouped.get(row_id, {})
        if set(roles) != {"selected", "reference"}:
            raise ValueError(f"{row_id}: expected selected/reference fresh runs")
        selected, reference = roles["selected"], roles["reference"]
        for role, row in roles.items():
            if row.get("status") != "PASS":
                raise ValueError(f"{row_id}/{role}: fresh evaluator status is {row.get('status')!r}")
            for field in ("H_g", "H_lg", "H_ip", "H_f"):
                number(row, field)
        hlg_selected = number(selected, "H_lg")
        hlg_reference = number(reference, "H_lg")
        hf_selected = number(selected, "H_f")
        hf_reference = number(reference, "H_f")
        hlg_delta = (hlg_selected / hlg_reference - 1.0) * 100.0
        hf_delta = (hf_selected / hf_reference - 1.0) * 100.0
        verdict = "counterexample" if hlg_delta < 0.0 and hf_delta > 0.0 else "not_reproduced"
        output.append(
            {
                "row_id": row_id,
                "case": selected["case"],
                "H_lg_selected": hlg_selected,
                "H_lg_reference": hlg_reference,
                "delta_H_lg_percent": hlg_delta,
                "H_f_selected": hf_selected,
                "H_f_reference": hf_reference,
                "delta_H_f_percent": hf_delta,
                "verdict": verdict,
            }
        )

    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(output)
    failures = [row["row_id"] for row in output if row["verdict"] != "counterexample"]
    if failures:
        raise ValueError("fresh full-flow counterexample not reproduced: " + ", ".join(failures))
    print(f"[PASS] 3/3 complete fresh trajectories reproduce the stage-local counterexamples: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
