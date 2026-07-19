#!/usr/bin/env python3
"""Verify stage-local HPWL counterexamples against paper Table 5."""

from __future__ import annotations

import csv
import json
import math
from pathlib import Path


ROOT = Path(__file__).resolve().parent
EXPECTED = ROOT / "expected" / "table5.json"


def delta(candidate: float, reference: float) -> float:
    return (candidate - reference) / reference * 100.0


def main() -> int:
    with (ROOT / "inputs" / "counterexamples.tsv").open(
        encoding="utf-8", newline=""
    ) as stream:
        rows = list(csv.DictReader(stream, delimiter="\t"))
    expected = {
        row["case"]: row
        for row in json.loads(EXPECTED.read_text(encoding="utf-8"))["rows"]
    }
    if {row["case"] for row in rows} != set(expected) or len(rows) != 3:
        raise ValueError("Expected exactly the three paper Table 5 cases")

    output = []
    for row in rows:
        claim = expected[row["case"]]
        legalization_delta = delta(float(row["Hlg_pick"]), float(row["Hlg_ref"]))
        final_delta = delta(float(row["Hf_pick"]), float(row["Hf_ref"]))
        if not math.isclose(
            legalization_delta,
            float(claim["legalization_delta_percent"]),
            rel_tol=0.0,
            abs_tol=0.01,
        ):
            raise ValueError(f"{row['case']}: legalization delta mismatch")
        if not math.isclose(
            final_delta,
            float(claim["final_delta_percent"]),
            rel_tol=0.0,
            abs_tol=0.01,
        ):
            raise ValueError(f"{row['case']}: final delta mismatch")
        output.append(
            {
                "case": row["case"],
                "legalization_delta_percent": legalization_delta,
                "final_delta_percent": final_delta,
                "stage_improves_final_regresses": legalization_delta < 0 < final_delta,
                "evidence_status": "matches_paper",
            }
        )

    output_dir = ROOT / "output"
    output_dir.mkdir(exist_ok=True)
    (output_dir / "summary.json").write_text(
        json.dumps({"rows": output, "matched": len(output)}, indent=2) + "\n",
        encoding="utf-8",
    )
    print("[PASS] 3/3 stage-local counterexamples match paper Table 5")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
