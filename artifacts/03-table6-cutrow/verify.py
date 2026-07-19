#!/usr/bin/env python3
"""Verify archived cut-row summaries against paper Table 6."""

from __future__ import annotations

import csv
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXPECTED = ROOT / "expected" / "table6.json"


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def main() -> int:
    expected = json.loads(EXPECTED.read_text(encoding="utf-8"))["rows"]
    fixed = {
        (row["case"], row["pattern"]): row
        for row in read_tsv(ROOT / "inputs" / "fixed_routes.tsv")
    }
    reviewdse = {
        (row["case"], row["pattern"]): row
        for row in read_tsv(ROOT / "inputs" / "reviewdse.tsv")
    }

    expected_keys = {(row["case"], row["pattern"]) for row in expected}
    if set(fixed) != expected_keys or set(reviewdse) != expected_keys:
        raise ValueError("Table 6 inputs must contain exactly the nine paper rows")

    output = []
    for claim in expected:
        key = (claim["case"], claim["pattern"])
        baseline = fixed[key]
        evolved = reviewdse[key]
        checks = {
            "diamond": baseline["diamond_status"] == claim["fixed_diamond"],
            "negotiation": baseline["negotiation_status"] == claim["fixed_negotiation"],
            "reviewdse_status": evolved["status"] == claim["reviewdse_status"],
            "reviewdse_runtime": math.isclose(
                float(evolved["runtime_seconds"]),
                float(claim["reviewdse_runtime_seconds"]),
                rel_tol=0.0,
                abs_tol=0.051,
            ),
            "strict_legality": evolved["check_result"] == "clean",
        }
        if not all(checks.values()):
            failed = ", ".join(name for name, passed in checks.items() if not passed)
            raise ValueError(f"{key}: evidence mismatch: {failed}")
        output.append(
            {
                "case": claim["case"],
                "pattern": claim["pattern"],
                "fixed_diamond": baseline["diamond_status"],
                "fixed_negotiation": baseline["negotiation_status"],
                "reviewdse_status": evolved["status"],
                "reviewdse_runtime_seconds": float(evolved["runtime_seconds"]),
                "candidate_archive": evolved["candidate_archive"],
                "evidence_status": "matches_paper",
            }
        )

    output_dir = ROOT / "output"
    output_dir.mkdir(exist_ok=True)
    (output_dir / "summary.json").write_text(
        json.dumps({"rows": output, "matched": len(output)}, indent=2) + "\n",
        encoding="utf-8",
    )
    with (output_dir / "table6.csv").open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output[0]), lineterminator="\n")
        writer.writeheader()
        writer.writerows(output)

    print("[PASS] 9/9 archived cut-row rows match paper Table 6")
    print(f"Output: {output_dir / 'table6.csv'}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
