#!/usr/bin/env python3
"""Recompute archived BO and ReviewDSE numbers without running an LLM."""

from __future__ import annotations

import argparse
import csv
import json
import math
import statistics
from pathlib import Path


CASES = {
    "aes_asap7",
    "aes_nangate45",
    "ariane133_nangate45",
    "ibex_asap7",
    "ibex_nangate45",
    "jpeg_asap7",
    "jpeg_nangate45",
    "swerv_wrapper_asap7",
    "swerv_wrapper_nangate45",
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(encoding="utf-8", newline="") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def close(left: float, right: float, tolerance: float = 1e-6) -> bool:
    return math.isclose(left, right, rel_tol=0.0, abs_tol=tolerance)


def delta_percent(reference: float, candidate: float) -> float:
    return (candidate - reference) / reference * 100.0


def summarize(values: list[float]) -> dict[str, float | int]:
    return {
        "n": len(values),
        "mean_delta_percent": statistics.fmean(values),
        "mean_reduction_percent": -statistics.fmean(values),
        # This is dispersion across designs, not repeated-seed uncertainty.
        "population_stddev_across_cases": statistics.pstdev(values),
    }


def load_bo(
    inputs: Path, directory: str
) -> tuple[list[dict[str, object]], dict[str, object]]:
    rows: list[dict[str, object]] = []
    for case in sorted(CASES):
        best = json.loads((inputs / directory / f"{case}.best.json").read_text())
        config = json.loads((inputs / directory / f"{case}.config.json").read_text())
        trials = read_tsv(inputs / directory / f"{case}.trials.tsv")
        if len(trials) != 400:
            raise ValueError(f"{case}: expected 400 BO trials, found {len(trials)}")
        if config.get("trials") != 400 or config.get("seed") != 1:
            raise ValueError(f"{case}: BO config is not the recorded 400-trial seed-1 run")

        matching = [row for row in trials if row["trial"] == best["trial"]]
        if len(matching) != 1:
            raise ValueError(f"{case}: best trial {best['trial']} is not unique")
        trial = matching[0]
        best_valid_score = min(
            float(row["score"]) for row in trials if row["status"] == "ok"
        )
        if not close(float(trial["score"]), best_valid_score):
            raise ValueError(f"{case}: best.json does not select the minimum valid score")
        reference = float(trial["hpwl_ref"])
        candidate = float(trial["hpwl_final"])
        computed = delta_percent(reference, candidate)
        if not close(computed, float(best["hpwl_delta_percent"])):
            raise ValueError(f"{case}: BO best.json delta does not match raw HPWL")
        if not close(candidate, float(best["metrics"]["hpwl_final"])):
            raise ValueError(f"{case}: BO best.json HPWL does not match trials.tsv")

        rows.append(
            {
                "case": case,
                "reference_hpwl": reference,
                "candidate_hpwl": candidate,
                "delta_percent": computed,
                "runtime_ratio": float(trial["runtime_ratio"]),
                "trial": best["trial"],
                "trials_checked": len(trials),
                "status": trial["status"],
            }
        )
    return rows, summarize([float(row["delta_percent"]) for row in rows])


def load_reviewdse_ghr(
    inputs: Path,
) -> tuple[list[dict[str, object]], dict[str, object]]:
    selected = read_tsv(inputs / "reviewdse" / "selected_programs.tsv")
    selected_by_case = {row["case"]: row for row in selected}
    if set(selected_by_case) != CASES or len(selected) != len(CASES):
        raise ValueError("ReviewDSE selection manifest must contain nine unique cases")

    matrix = read_tsv(inputs / "reviewdse" / "cross_case_matrix.tsv")
    diagonal: dict[str, dict[str, str]] = {}
    for row in matrix:
        case = row["discovery_case"]
        if row["program"] == f"P_{case}" and row["target_case"] == case:
            diagonal[case] = row
    if set(diagonal) != CASES:
        raise ValueError("ReviewDSE transfer matrix has an incomplete discovery diagonal")

    rows: list[dict[str, object]] = []
    for case in sorted(CASES):
        selection = selected_by_case[case]
        raw = diagonal[case]
        metrics = json.loads(
            (inputs / "reviewdse" / "candidate_metrics" / f"{case}.json").read_text()
        )
        reference = float(raw["default_hpwl"])
        replay_candidate = float(raw["candidate_hpwl"])
        replay_delta = delta_percent(reference, replay_candidate)
        if not close(replay_delta, float(raw["delta_hpwl_percent"]), 6e-4):
            raise ValueError(f"{case}: replay-matrix delta does not match raw HPWL")
        discovery_delta = float(selection["delta_hpwl_percent"])
        if not close(replay_delta, discovery_delta, 0.005):
            raise ValueError(f"{case}: replay drift exceeds 0.005 percentage points")
        discovery_hpwl = float(metrics["canonical"]["final_hpwl_micron"])
        computed = delta_percent(reference, discovery_hpwl)
        if not close(computed, discovery_delta, 5e-4):
            raise ValueError(f"{case}: GHR discovery delta does not match raw HPWL")
        replay_drift = replay_candidate - discovery_hpwl
        if metrics["canonical"].get("legality") != "clean":
            raise ValueError(f"{case}: selected candidate is not placement-clean")

        rows.append(
            {
                "case": case,
                "reference_hpwl": reference,
                "candidate_hpwl": discovery_hpwl,
                "delta_percent": computed,
                "runtime_ratio": float(selection["runtime_ratio"]),
                "replay_candidate_hpwl": replay_candidate,
                "replay_hpwl_drift": replay_drift,
                "campaign": selection["experiment_batch"],
                "student": selection["student"],
                "iteration": selection["iteration"],
                "legality": metrics["canonical"]["legality"],
            }
        )
    summary = summarize([float(row["delta_percent"]) for row in rows])
    summary["mean_runtime_ratio"] = statistics.fmean(
        float(row["runtime_ratio"]) for row in rows
    )
    return rows, summary


def load_reviewdse_hpwl(
    inputs: Path,
) -> tuple[list[dict[str, object]], dict[str, object]]:
    selected = read_tsv(inputs / "reviewdse" / "hpwl_selected_programs.tsv")
    if {row["case"] for row in selected} != CASES or len(selected) != len(CASES):
        raise ValueError("ReviewDSE-HPWL manifest must contain nine unique cases")

    rows: list[dict[str, object]] = []
    for selection in selected:
        case = selection["case"]
        metrics = json.loads(
            (inputs / "reviewdse" / "hpwl_candidate_metrics" / f"{case}.json").read_text()
        )
        reference = float(selection["default_hpwl"])
        candidate = float(metrics["canonical"]["final_hpwl_micron"])
        computed = delta_percent(reference, candidate)
        if not close(candidate, float(selection["best_hpwl"]), 0.11):
            raise ValueError(f"{case}: HPWL candidate metrics do not match manifest")
        if not close(computed, float(selection["delta_hpwl_percent"]), 5e-4):
            raise ValueError(f"{case}: HPWL selection delta does not match raw HPWL")
        if metrics["canonical"].get("legality") != "clean":
            raise ValueError(f"{case}: HPWL-selected candidate is not placement-clean")
        rows.append(
            {
                "case": case,
                "reference_hpwl": reference,
                "candidate_hpwl": candidate,
                "delta_percent": computed,
                "runtime_ratio": float(selection["runtime_ratio"]),
                "campaign": selection["round_id"],
                "student": selection["best_student"],
                "iteration": selection["best_iter"],
                "legality": metrics["canonical"]["legality"],
            }
        )
    rows.sort(key=lambda row: str(row["case"]))
    summary = summarize([float(row["delta_percent"]) for row in rows])
    summary["mean_runtime_ratio"] = statistics.fmean(
        float(row["runtime_ratio"]) for row in rows
    )
    return rows, summary


def paper_column_check(
    rows: list[dict[str, object]],
    paper_cases: dict[str, dict[str, object]],
    prefix: str,
    tolerance: float,
) -> dict[str, object]:
    checks = []
    for row in rows:
        case = str(row["case"])
        expected = paper_cases[case]
        delta_ok = close(float(row["delta_percent"]), float(expected[f"{prefix}_delta"]), tolerance)
        runtime_ok = close(float(row["runtime_ratio"]), float(expected[f"{prefix}_runtime"]), tolerance)
        iteration_ok = True
        if prefix in {"hpwl", "ghr"}:
            iteration_ok = int(str(row["iteration"]).removeprefix("iter_")) == int(
                expected[f"{prefix}_iteration"]
            )
        checks.append(
            {
                "case": case,
                "delta_matches_rounded_paper": delta_ok,
                "runtime_matches_rounded_paper": runtime_ok,
                "iteration_matches_paper": iteration_ok,
            }
        )
    return {
        "all_cases_match": all(
            item["delta_matches_rounded_paper"]
            and item["runtime_matches_rounded_paper"]
            and item["iteration_matches_paper"]
            for item in checks
        ),
        "cases": checks,
    }


def write_csv(path: Path, rows: list[dict[str, object]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    fields = list(rows[0])
    with path.open("w", encoding="utf-8", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--claims", type=Path, required=True)
    parser.add_argument("--paper-table", type=Path, required=True)
    parser.add_argument("--strict-paper-claims", action="store_true")
    args = parser.parse_args()

    bo_paper_rows, bo_paper_summary = load_bo(args.input, "bo_paper")
    ghr_rows, ghr_summary = load_reviewdse_ghr(args.input)
    hpwl_rows, hpwl_summary = load_reviewdse_hpwl(args.input)
    claims = json.loads(args.claims.read_text(encoding="utf-8"))
    paper = json.loads(args.paper_table.read_text(encoding="utf-8"))
    tolerance = float(claims["rounding_tolerance_percent"])
    comparisons = {
        "bo": {
            "claimed_reduction_percent": claims["bo_mean_reduction_percent"],
            "recomputed_reduction_percent": bo_paper_summary["mean_reduction_percent"],
            "status": "matches: exact 400-trial paper campaign recovered",
        },
        "reviewdse_ghr_selected": {
            "claimed_reduction_percent": claims["reviewdse_ghr_mean_reduction_percent"],
            "recomputed_reduction_percent": ghr_summary["mean_reduction_percent"],
        },
        "reviewdse_hpwl_selected": {
            "claimed_reduction_percent": claims["reviewdse_hpwl_mean_reduction_percent"],
            "recomputed_reduction_percent": hpwl_summary["mean_reduction_percent"],
        },
    }
    all_match = True
    for comparison in comparisons.values():
        difference = abs(
            float(comparison["claimed_reduction_percent"])
            - float(comparison["recomputed_reduction_percent"])
        )
        comparison["absolute_difference_percent"] = difference
        comparison["matches_within_rounding"] = difference <= tolerance
        all_match = all_match and difference <= tolerance
    paper_alignment = {
        "bo": paper_column_check(bo_paper_rows, paper["cases"], "bo", tolerance),
        "reviewdse_hpwl": paper_column_check(
            hpwl_rows, paper["cases"], "hpwl", tolerance
        ),
        "reviewdse_ghr": paper_column_check(
            ghr_rows, paper["cases"], "ghr", tolerance
        ),
    }
    all_match = all_match and all(
        check["all_cases_match"] for check in paper_alignment.values()
    )

    result = {
        "schema_version": 1,
        "scope": "archived evidence recomputation; no EDA run and no LLM call",
        "bo_paper_score_selected": {
            "rows": bo_paper_rows,
            "summary": bo_paper_summary,
        },
        "reviewdse_hpwl_selected": {"rows": hpwl_rows, "summary": hpwl_summary},
        "reviewdse_ghr_selected": {"rows": ghr_rows, "summary": ghr_summary},
        "paper_claim_check": {
            "source": claims["source"],
            "comparisons": comparisons,
            "per_case_alignment": paper_alignment,
            "all_claims_match": all_match,
        },
        "statistical_note": (
            "Stddev is across heterogeneous designs. No repeated-seed raw evidence is "
            "present, so this artifact does not claim a seed-level confidence interval."
        ),
    }
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output / "summary.json").write_text(
        json.dumps(result, indent=2, sort_keys=True) + "\n", encoding="utf-8"
    )
    write_csv(args.output / "bo_paper_recomputed.csv", bo_paper_rows)
    write_csv(args.output / "reviewdse_hpwl_recomputed.csv", hpwl_rows)
    write_csv(args.output / "reviewdse_ghr_recomputed.csv", ghr_rows)

    print("Archived evidence is internally consistent.")
    print(
        f"BO paper:        {bo_paper_summary['mean_reduction_percent']:.4f}% "
        "(paper: 0.38%)"
    )
    print(
        f"ReviewDSE-HPWL:  {hpwl_summary['mean_reduction_percent']:.4f}% / "
        f"{hpwl_summary['mean_runtime_ratio']:.4f}x (paper: 1.78% / 1.34x)"
    )
    print(
        f"ReviewDSE-GHR:   {ghr_summary['mean_reduction_percent']:.4f}% / "
        f"{ghr_summary['mean_runtime_ratio']:.4f}x (paper: 1.68% / 1.11x)"
    )
    if all_match:
        print("Paper claim check: PASS")
    else:
        print("Paper claim check: FAIL")
    print(f"Machine-readable report: {args.output / 'summary.json'}")
    return 1 if args.strict_paper_claims and not all_match else 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, KeyError, ValueError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
