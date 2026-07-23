#!/usr/bin/env python3
"""Validate 27 fresh cut-row replays against paper Table 6 evidence."""
from __future__ import annotations

import argparse
import csv
import json
import math
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parents[2]
REFERENCE_CASE_LABEL = {
    "ariane133_placebatch": "Ariane133 N45",
    "swerv_wrapper_dense2": "SWERV dense N45",
    "bp_quad_placebatch": "BPQUAD",
}


def read_tsv(path: Path) -> list[dict[str, str]]:
    with path.open(newline="", encoding="utf-8") as stream:
        return list(csv.DictReader(stream, delimiter="\t"))


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        raise ValueError(f"missing fresh metrics: {path}")
    return json.loads(path.read_text(encoding="utf-8"))


def number(value: Any) -> float | None:
    if value in (None, ""):
        return None
    try:
        result = float(value)
    except (TypeError, ValueError):
        return None
    return result if math.isfinite(result) else None


def close(observed: float | None, expected: float | None, relative: float) -> bool:
    if observed is None or expected is None:
        return False
    return math.isclose(observed, expected, rel_tol=relative, abs_tol=1e-6)


def metrics_contract(path: Path, observed_status: str) -> tuple[dict[str, Any], list[str]]:
    data = load_json(path)
    hpwl = data.get("hpwl") or {}
    displacement = data.get("displacement") or {}
    legality = data.get("legality") or {}
    problems: list[str] = []
    status = str(data.get("status") or "missing")
    if observed_status == "pass":
        if status != "ok":
            problems.append("metrics_status_not_ok")
        if hpwl.get("source") != "openroad_dpl_log":
            problems.append("noncanonical_hpwl_source")
        if number(hpwl.get("before_micron")) is None:
            problems.append("missing_hpwl_before")
        if number(hpwl.get("after_micron")) is None:
            problems.append("missing_hpwl_after")
        if number(displacement.get("average_displacement_micron")) is None:
            problems.append("missing_average_displacement")
        if number(displacement.get("max_displacement_micron")) is None:
            problems.append("missing_max_displacement")
        try:
            check_ok = int(legality.get("check_status")) == 0
        except (TypeError, ValueError):
            check_ok = False
        if not check_ok:
            problems.append("strict_legality_not_clean")
    elif observed_status == "timeout" and status != "timeout":
        problems.append("timeout_metrics_status_mismatch")
    elif observed_status == "fail" and status == "ok":
        problems.append("failed_row_has_ok_metrics")
    return data, problems


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--results", required=True, type=Path)
    parser.add_argument("--experiment-manifest", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--hpwl-relative-tolerance", type=float, default=0.001)
    parser.add_argument("--runtime-relative-tolerance", type=float, default=0.35)
    args = parser.parse_args()

    contract = json.loads(args.experiment_manifest.read_text(encoding="utf-8"))
    timeout_cap = int(contract["table6"]["timeout_seconds"])
    expected = {
        (row["case"], row["pattern"], role): (row, row["expected"][role])
        for row in contract["table6"]["rows"]
        for role in ("diamond", "negotiation", "reviewdse")
    }
    review_reference = {
        (row["case"], row["pattern"]): row
        for row in read_tsv(ROOT / "artifacts/03-table6-cutrow/inputs/reviewdse.tsv")
    }
    fixed_reference = {
        (row["case"], row["pattern"]): row
        for row in read_tsv(ROOT / "artifacts/03-table6-cutrow/inputs/fixed_routes.tsv")
    }
    fresh = read_tsv(args.results)

    output_rows: list[dict[str, Any]] = []
    by_key: dict[tuple[str, str, str], dict[str, Any]] = {}
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
        problems: list[str] = []
        if int(float(result.get("timeout_seconds") or 0)) != timeout_cap:
            problems.append("timeout_cap_not_7200")
        metrics_path = Path(result.get("metrics_json", ""))
        metrics, metric_problems = metrics_contract(metrics_path, observed)
        problems.extend(metric_problems)
        hpwl = metrics.get("hpwl") or {}
        runtime = number(metrics.get("runtime_seconds"))
        hpwl_before = number(hpwl.get("before_micron"))
        hpwl_after = number(hpwl.get("after_micron"))
        delta_percent = number(hpwl.get("delta_percent"))
        expected_hpwl_before = None
        expected_hpwl_after = None
        expected_runtime = None
        paper_key = (
            REFERENCE_CASE_LABEL[spec["case"]],
            spec.get("paper_pattern", spec["pattern"]),
        )
        if result["role"] == "reviewdse":
            retained = review_reference[paper_key]
            expected_hpwl_before = number(retained.get("hpwl_before"))
            expected_hpwl_after = number(retained.get("hpwl_after"))
            expected_runtime = number(retained.get("runtime_seconds"))
        elif expected_status == "pass":
            retained = fixed_reference[paper_key]
            expected_hpwl_after = number(retained.get(f"{result['role']}_hpwl_after"))
            expected_runtime = number(retained.get(f"{result['role']}_runtime_seconds"))

        if observed == "pass" and expected_hpwl_after is not None:
            if not close(hpwl_after, expected_hpwl_after, args.hpwl_relative_tolerance):
                problems.append("hpwl_outside_tolerance")
        if observed == "pass" and expected_hpwl_before is not None:
            if not close(hpwl_before, expected_hpwl_before, args.hpwl_relative_tolerance):
                problems.append("input_hpwl_outside_tolerance")
        if observed == "pass" and expected_runtime is not None:
            if not close(runtime, expected_runtime, args.runtime_relative_tolerance):
                problems.append("runtime_outside_tolerance")
        if observed == "timeout" and not close(runtime, float(timeout_cap), 0.0):
            problems.append("timeout_runtime_not_cap")
        if observed != expected_status:
            problems.append("status_mismatch")

        row: dict[str, Any] = {
            "case": spec.get("paper_case", spec["case"]),
            "pattern": spec.get("paper_pattern", spec["pattern"]),
            "data_case": spec["case"],
            "data_pattern": spec["pattern"],
            "role": result["role"],
            "program": spec["program"] if result["role"] == "reviewdse" else result["role"],
            "observed": observed,
            "expected": expected_status,
            "runtime_seconds": "" if runtime is None else runtime,
            "expected_runtime_seconds": "" if expected_runtime is None else expected_runtime,
            "hpwl_before_micron": "" if hpwl_before is None else hpwl_before,
            "hpwl_after_micron": "" if hpwl_after is None else hpwl_after,
            "expected_hpwl_after_micron": "" if expected_hpwl_after is None else expected_hpwl_after,
            "delta_percent": "" if delta_percent is None else delta_percent,
            "timeout_seconds": timeout_cap,
            "metrics_contract": "pass" if not metric_problems else "fail",
            "verdict": "match" if not problems else "mismatch",
            "problems": ",".join(problems),
            "exit_code": result.get("exit_code", ""),
            "metrics_json": str(metrics_path),
            "log": result.get("log", ""),
            "legal_fixed_role": "",
            "qor_improvement_percent": "",
            "speedup": "",
        }
        output_rows.append(row)
        by_key[key] = row

    missing = sorted(set(expected) - seen)
    if missing:
        raise ValueError(f"expected 27 executions; missing {len(missing)}: {missing}")
    if len(output_rows) != 27:
        raise ValueError(f"expected 27 Table 6 executions, found {len(output_rows)}")

    for spec in contract["table6"]["rows"]:
        base = (spec["case"], spec["pattern"])
        review = by_key[(*base, "reviewdse")]
        legal_roles = [
            role for role in ("diamond", "negotiation")
            if by_key[(*base, role)]["observed"] == "pass"
        ]
        if legal_roles and review["observed"] == "pass":
            role = legal_roles[0]
            fixed = by_key[(*base, role)]
            fixed_hpwl = number(fixed["hpwl_after_micron"])
            review_hpwl = number(review["hpwl_after_micron"])
            fixed_runtime = number(fixed["runtime_seconds"])
            review_runtime = number(review["runtime_seconds"])
            review["legal_fixed_role"] = role
            if fixed_hpwl and review_hpwl is not None:
                review["qor_improvement_percent"] = 100.0 * (fixed_hpwl - review_hpwl) / fixed_hpwl
            if fixed_runtime and review_runtime:
                review["speedup"] = fixed_runtime / review_runtime

    output_rows.sort(key=lambda row: (row["data_case"], row["data_pattern"], row["role"]))
    args.output.parent.mkdir(parents=True, exist_ok=True)
    with args.output.open("w", newline="", encoding="utf-8") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(output_rows[0]), delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(output_rows)
    mismatches = [
        f"{row['data_case']}/{row['data_pattern']}/{row['role']}({row['problems']})"
        for row in output_rows if row["verdict"] != "match"
    ]
    if mismatches:
        raise ValueError("fresh Table 6 evidence differs from paper/tolerance: " + ", ".join(mismatches))
    print(f"[PASS] 27 fresh Table 6 runs match status, canonical HPWL, runtime, legality, and timeout contracts: {args.output}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except (OSError, ValueError, KeyError, json.JSONDecodeError, csv.Error) as exc:
        raise SystemExit(f"[ERROR] {exc}") from None
