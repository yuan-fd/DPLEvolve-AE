#!/usr/bin/env python3
"""Pure metric-level eligibility checks for fresh DPLEvolve candidates.

Artifact/source provenance is checked by the Teacher loop.  This module owns
the evaluator-facing half of the contract so reporting, promotion, and paper
post-processing can all use the same verdict.
"""
from __future__ import annotations

from typing import Any


RUNTIME_LIMIT = 2.0
REQUIRED_STAGES = (
    "global_micron",
    "legalized_micron",
    "after_improve_micron",
    "final_micron",
)


def _present_number(value: Any) -> bool:
    if value is None or value == "":
        return False
    try:
        float(value)
    except (TypeError, ValueError):
        return False
    return True


def metric_eligibility(
    summary: dict[str, Any], *, runtime_limit: float = RUNTIME_LIMIT
) -> dict[str, Any]:
    """Return a machine-readable fresh-run metric verdict.

    The runtime comparison deliberately has no rounded-time epsilon.  The
    paper states a 2x gate, so fresh experiments must satisfy ratio <= 2.0.
    Historical rows remain archive evidence and are not grandfathered here.
    """
    canonical = summary.get("canonical") or {}
    stages = summary.get("stages") or {}
    headline = summary.get("headline_vs_openroad_default") or {}
    problems: list[str] = []

    if summary.get("status") != "ok":
        problems.append("run_status_not_ok")
    if summary.get("legalize_exit_status") not in (0, "0"):
        problems.append("legalize_exit_nonzero")
    if canonical.get("legality") != "clean":
        problems.append("placement_not_clean")
    if canonical.get("hpwl_source") in (None, "", "cell_bbox_proxy"):
        problems.append("noncanonical_hpwl")
    if not _present_number(canonical.get("final_hpwl_micron")):
        problems.append("missing_final_hpwl")
    if not _present_number(canonical.get("runtime_seconds")):
        problems.append("missing_runtime")
    if not _present_number(canonical.get("avg_displacement_micron")):
        problems.append("missing_average_displacement")
    if not _present_number(canonical.get("max_displacement_micron")):
        problems.append("missing_max_displacement")

    for name in REQUIRED_STAGES:
        if not _present_number(stages.get(name)):
            problems.append(f"missing_stage_{name}")

    runtime_ratio = headline.get("runtime_ratio")
    if not _present_number(runtime_ratio):
        problems.append("missing_reference_runtime_ratio")
    elif float(runtime_ratio) > float(runtime_limit):
        problems.append("runtime_gate_exceeded")

    counter_lines = [
        str(line).strip()
        for line in (summary.get("log_counter_lines") or [])
        if str(line).strip()
    ]
    if not counter_lines:
        problems.append("mechanism_liveness_unproven")

    return {
        "schema_version": 1,
        "scope": "fresh_candidate_metrics",
        "eligible": not problems,
        "problems": problems,
        "runtime_gate": {
            "limit": float(runtime_limit),
            "ratio": float(runtime_ratio) if _present_number(runtime_ratio) else None,
            "policy": "exact_ratio_lte_limit",
        },
        "required_stages": list(REQUIRED_STAGES),
        "mechanism_liveness": {
            "observed": bool(counter_lines),
            "signals": counter_lines,
        },
    }
