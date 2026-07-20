#!/usr/bin/env python3
"""Ray Tune black-box optimization for DPL flow parameters.

This runner is intentionally black-box: it does not edit or relink OpenROAD.
It samples Tcl command parameters with Ray Tune, runs the strict baseline
harness, reads canonical metrics.json, and optimizes legal trials for the
lowest final HPWL.  Runtime-aware `G_HR` is reported as a value check, not as
the search objective.
"""

from __future__ import annotations

import argparse
import csv
import hashlib
import json
import math
import os
import shutil
import subprocess
import sys
import tempfile
from dataclasses import dataclass
from pathlib import Path
from typing import Any

SCRIPT_DIR = Path(__file__).resolve().parent
AGENT_ROOT = SCRIPT_DIR.parents[1]
sys.path.insert(0, str(AGENT_ROOT))
from runtime_paths import ensure_python_module, load_local_env_sh  # noqa: E402

load_local_env_sh(AGENT_ROOT)
ensure_python_module("yaml", __file__, sys.argv)

import yaml

DEFAULT_SPACE = AGENT_ROOT / "configs" / "bo_search_spaces" / "openroad_dpl_native.yaml"
RUNTIME_DEADBAND_RATIO = 1.10
RUNTIME_PENALTY_AT_2X = 1.0


@dataclass(frozen=True)
class ParamSpec:
    name: str
    stage: str
    flag: str
    kind: str
    min_value: float | None = None
    max_value: float | None = None
    choices: tuple[Any, ...] = ()
    default: Any = None
    active_if: dict[str, tuple[Any, ...]] | None = None


def require_ray_tune() -> tuple[Any, Any, Any, Any, Any]:
    """Import Ray Tune dependencies; fail hard if the BO environment is absent."""

    try:
        import ray
        from ray import train, tune
        from ray.air import RunConfig
        from ray.tune import TuneConfig
        from ray.tune.search import ConcurrencyLimiter
        import optuna
        from ray.tune.search.optuna import OptunaSearch
    except Exception as exc:
        raise SystemExit(
            "Ray Tune BO is a required dependency for scripts/bo/bo_tune_case.py.\n"
            "Install it with:\n"
            "  dpl_evolve_agent/scripts/bo/setup_raytune_venv.sh\n"
            "Then run with:\n"
            "  dpl_evolve_agent/.venv_raytune/bin/python "
            "dpl_evolve_agent/scripts/bo/bo_tune_case.py ...\n"
            f"Import error: {exc!r}"
        ) from exc
    return (
        ray,
        train,
        tune,
        RunConfig,
        (TuneConfig, ConcurrencyLimiter, OptunaSearch, optuna.samplers.TPESampler),
    )


def load_yaml(path: Path) -> dict[str, Any]:
    with path.open("r", encoding="utf-8") as fh:
        return yaml.safe_load(fh) or {}


def parse_space(path: Path) -> tuple[dict[str, Any], list[ParamSpec]]:
    data = load_yaml(path)
    specs: list[ParamSpec] = []
    for raw in data.get("parameters", []):
        if raw.get("enabled", True) is False:
            continue
        choices = tuple(raw.get("choices") or ())
        specs.append(
            ParamSpec(
                name=str(raw["name"]),
                stage=str(raw["stage"]),
                flag=str(raw["flag"]),
                kind=str(raw["type"]),
                min_value=float(raw["min"]) if "min" in raw else None,
                max_value=float(raw["max"]) if "max" in raw else None,
                choices=choices,
                default=raw.get("default"),
                active_if={
                    str(key): tuple(value if isinstance(value, list) else [value])
                    for key, value in (raw.get("active_if") or {}).items()
                }
                or None,
            )
        )
    if not specs:
        raise SystemExit(f"No enabled parameters in search space: {path}")
    return data, specs


def default_params(specs: list[ParamSpec]) -> dict[str, Any]:
    params: dict[str, Any] = {}
    for spec in specs:
        if spec.default is not None:
            params[spec.name] = spec.default
        elif spec.kind == "categorical":
            params[spec.name] = spec.choices[0]
        elif spec.kind == "flag":
            params[spec.name] = 0
        elif spec.kind == "int":
            assert spec.min_value is not None and spec.max_value is not None
            params[spec.name] = int(round((spec.min_value + spec.max_value) / 2.0))
        elif spec.kind == "float":
            assert spec.min_value is not None and spec.max_value is not None
            params[spec.name] = (spec.min_value + spec.max_value) / 2.0
        else:
            raise ValueError(f"Unsupported parameter type: {spec.kind}")
    return params


def normalize_value(spec: ParamSpec, value: Any) -> Any:
    if hasattr(value, "item"):
        value = value.item()
    if spec.kind == "flag":
        return int(value)
    if spec.kind == "int":
        return int(value)
    if spec.kind == "float":
        return float(value)
    return value


def normalize_params(specs: list[ParamSpec], params: dict[str, Any]) -> dict[str, Any]:
    defaults = default_params(specs)
    return {
        spec.name: normalize_value(spec, params.get(spec.name, defaults[spec.name]))
        for spec in specs
    }


def params_fingerprint(params: dict[str, Any]) -> str:
    payload = json.dumps(params, sort_keys=True, separators=(",", ":"))
    return hashlib.sha1(payload.encode("utf-8")).hexdigest()[:12]


def shell_list(values: list[str]) -> str:
    rendered: list[str] = []
    for value in values:
        value = str(value)
        if value == "":
            rendered.append("{}")
        elif any(ch.isspace() for ch in value) or any(ch in value for ch in "{}[]$;\\\""):
            if value.count("{") == value.count("}") and "\\" not in value:
                rendered.append("{" + value + "}")
            else:
                escaped = (
                    value.replace("\\", "\\\\")
                    .replace("{", "\\{")
                    .replace("}", "\\}")
                )
                rendered.append("{" + escaped + "}")
        else:
            rendered.append(value)
    return " ".join(rendered)


def format_flag_value(value: Any) -> str:
    if isinstance(value, bool):
        return "1" if value else "0"
    return str(value)


def build_stage_args(
    specs: list[ParamSpec],
    params: dict[str, Any],
    env_prefix: str = "DPL_EVOLVE",
) -> dict[str, str]:
    params = normalize_params(specs, params)
    detail: list[str] = []
    improve: list[str] = []
    global_swap: list[str] = []
    optimize: list[str] = []
    for spec in specs:
        value = params[spec.name]
        if spec.active_if:
            active = all(params.get(name) in allowed for name, allowed in spec.active_if.items())
            if not active:
                continue
        if value is None or value == "" or value == "omit":
            continue
        if spec.stage == "meta":
            continue
        if spec.kind == "flag":
            if int(value) != 0:
                if not spec.flag:
                    raise ValueError(f"flag parameter has empty flag: {spec.name}")
                if spec.stage == "detail":
                    detail.append(spec.flag)
                elif spec.stage == "improve":
                    improve.append(spec.flag)
                elif spec.stage == "optimize":
                    optimize.append(spec.flag)
                else:
                    raise ValueError(f"flag parameters do not support stage {spec.stage}: {spec.name}")
            continue
        if spec.stage == "detail":
            detail.extend([spec.flag, format_flag_value(value)])
        elif spec.stage == "improve":
            improve.extend([spec.flag, format_flag_value(value)])
        elif spec.stage == "improve_global_swap":
            global_swap.extend([spec.flag, format_flag_value(value)])
        elif spec.stage == "optimize":
            optimize.extend([spec.flag, format_flag_value(value)])
        else:
            raise ValueError(f"Unknown parameter stage for {spec.name}: {spec.stage}")
    if global_swap:
        improve.extend(["-global_swap_args", shell_list(global_swap)])
    prefix = env_prefix.strip().upper()
    if not prefix:
        raise ValueError("env_prefix must not be empty")
    return {
        f"{prefix}_DETAIL_ARGS": shell_list(detail),
        f"{prefix}_IMPROVE_ARGS": shell_list(improve),
        f"{prefix}_OPTIMIZE_ARGS": shell_list(optimize),
    }


def build_tune_space(specs: list[ParamSpec], tune: Any) -> dict[str, Any]:
    space: dict[str, Any] = {}
    for spec in specs:
        if spec.kind == "categorical":
            space[spec.name] = tune.choice(list(spec.choices))
        elif spec.kind == "flag":
            space[spec.name] = tune.choice([0, 1])
        elif spec.kind == "int":
            assert spec.min_value is not None and spec.max_value is not None
            space[spec.name] = tune.randint(int(spec.min_value), int(spec.max_value) + 1)
        elif spec.kind == "float":
            assert spec.min_value is not None and spec.max_value is not None
            space[spec.name] = tune.uniform(float(spec.min_value), float(spec.max_value))
        else:
            raise ValueError(f"Unsupported parameter type for {spec.name}: {spec.kind}")
    return space


def override_param(
    specs_by_name: dict[str, ParamSpec],
    params: dict[str, Any],
    name: str,
    value: Any,
) -> None:
    spec = specs_by_name.get(name)
    if spec is None:
        return
    if spec.kind == "categorical":
        if value in spec.choices:
            params[name] = value
        return
    if spec.kind == "flag":
        params[name] = 1 if int(value) else 0
        return
    if spec.kind == "int":
        assert spec.min_value is not None and spec.max_value is not None
        params[name] = int(min(max(int(value), int(spec.min_value)), int(spec.max_value)))
        return
    if spec.kind == "float":
        assert spec.min_value is not None and spec.max_value is not None
        params[name] = float(min(max(float(value), spec.min_value), spec.max_value))


def apply_overrides(
    specs_by_name: dict[str, ParamSpec],
    base: dict[str, Any],
    overrides: dict[str, Any],
) -> dict[str, Any]:
    params = dict(base)
    for name, value in overrides.items():
        override_param(specs_by_name, params, name, value)
    return params


def build_anchor_points(specs: list[ParamSpec], strategy: str) -> list[dict[str, Any]]:
    """Build deterministic DSE seed points before TPE exploitation.

    The anchors are mechanism-shaped rather than case-specific.  They make the
    first trials cover distinct public DPL control paths before the BO sampler
    starts concentrating around promising regions.
    """

    if strategy == "none":
        return []
    defaults = default_params(specs)
    specs_by_name = {spec.name: spec for spec in specs}
    raw_points: list[dict[str, Any]] = [dict(defaults)]
    if strategy == "mechanism":
        raw_points.extend(
            [
                {
                    "detail_max_displacement": 512,
                    "detail_incremental": 0,
                    "detail_use_negotiation": 0,
                    "improve_max_displacement": 1024,
                    "improve_enable_extra_dpl": 0,
                },
                {
                    "detail_max_displacement": 128,
                    "detail_incremental": 1,
                    "detail_use_negotiation": 0,
                    "improve_max_displacement": "omit",
                    "improve_enable_extra_dpl": 0,
                    "global_swap_passes": 2,
                    "global_swap_tolerance": 0.01,
                    "global_swap_tradeoff": 0.4,
                    "global_swap_area_weight": 0.4,
                    "global_swap_pin_weight": 0.6,
                    "global_swap_congestion_user_weight": 35.0,
                    "global_swap_sampling_moves": 150,
                    "global_swap_normalization_interval": 1000,
                    "global_swap_profiling_excess": 1.10,
                    "global_swap_budget_multipliers": "1.50 1.25 1.10 1.04",
                },
                {
                    "detail_max_displacement": "omit",
                    "detail_incremental": 0,
                    "detail_use_negotiation": 1,
                    "improve_max_displacement": 512,
                    "improve_enable_extra_dpl": 0,
                    "global_swap_passes": 2,
                    "global_swap_tolerance": 0.02,
                    "global_swap_tradeoff": 0.5,
                    "global_swap_area_weight": 0.6,
                    "global_swap_pin_weight": 0.8,
                    "global_swap_congestion_user_weight": 45.0,
                    "global_swap_sampling_moves": 200,
                    "global_swap_normalization_interval": 1000,
                    "global_swap_profiling_excess": 1.15,
                    "global_swap_budget_multipliers": "1.50 1.25 1.10 1.04",
                },
                {
                    "detail_max_displacement": 1024,
                    "detail_incremental": 0,
                    "detail_use_negotiation": 0,
                    "improve_max_displacement": 1024,
                    "improve_enable_extra_dpl": 1,
                    "global_swap_passes": 3,
                    "global_swap_tolerance": 0.03,
                    "global_swap_tradeoff": 0.65,
                    "global_swap_area_weight": 0.8,
                    "global_swap_pin_weight": 0.9,
                    "global_swap_congestion_user_weight": 55.0,
                    "global_swap_sampling_moves": 400,
                    "global_swap_normalization_interval": 800,
                    "global_swap_profiling_excess": 1.20,
                    "global_swap_budget_multipliers": "1.75 1.35 1.15 1.05",
                },
                {
                    "detail_max_displacement": 256,
                    "detail_incremental": 0,
                    "detail_use_negotiation": 0,
                    "improve_max_displacement": 256,
                    "improve_enable_extra_dpl": 0,
                    "global_swap_passes": 1,
                    "global_swap_tolerance": 0.015,
                    "global_swap_tradeoff": 0.25,
                    "global_swap_area_weight": 0.3,
                    "global_swap_pin_weight": 0.9,
                    "global_swap_congestion_user_weight": 25.0,
                    "global_swap_sampling_moves": 100,
                    "global_swap_normalization_interval": 1500,
                    "global_swap_profiling_excess": 1.05,
                    "global_swap_budget_multipliers": "1.25 1.10 1.04",
                },
            ]
        )
    elif strategy != "default":
        raise SystemExit(f"Unsupported --anchor-strategy: {strategy}")

    anchors: list[dict[str, Any]] = []
    seen: set[str] = set()
    for point in raw_points:
        params = apply_overrides(specs_by_name, defaults, point)
        params = normalize_params(specs, params)
        fp = params_fingerprint(params)
        if fp not in seen:
            anchors.append(params)
            seen.add(fp)
    return anchors


def hpwl_metrics(metrics: dict[str, Any]) -> dict[str, Any]:
    return metrics.get("hpwl") or metrics.get("hpwl_openroad_log") or metrics["hpwl_proxy"]


def hpwl_global_micron(metrics: dict[str, Any], reference_hpwl: float) -> float:
    stages = metrics.get("hpwl_stages") or {}
    global_value = stages.get("global_micron")
    if global_value not in (None, ""):
        return float(global_value)
    hpwl = hpwl_metrics(metrics)
    before_value = hpwl.get("before_micron")
    if before_value not in (None, ""):
        return float(before_value)
    return float(reference_hpwl)


def is_clean(metrics: dict[str, Any]) -> bool:
    if metrics.get("status") != "ok":
        return False
    legality = metrics.get("legality") or {}
    violations = str(legality.get("placement_violations", "") or "").strip()
    return violations == ""


def load_metrics(path: Path) -> dict[str, Any]:
    return json.loads(path.read_text(encoding="utf-8"))


def runtime_penalty_pp(
    runtime_ratio: float,
    *,
    runtime_deadband_ratio: float = RUNTIME_DEADBAND_RATIO,
    runtime_penalty_at_2x: float = RUNTIME_PENALTY_AT_2X,
) -> float:
    """Return runtime penalty in percentage points.

    Runtime is a value check, not a reward source.  Faster-than-reference
    runtime and small runtime noise within the deadband receive no bonus.
    Only material slowdowns reduce G_HR.
    """

    if runtime_ratio <= runtime_deadband_ratio:
        return 0.0
    denominator = math.sqrt(2.0) - math.sqrt(runtime_deadband_ratio)
    if denominator <= 0.0:
        return 0.0
    return runtime_penalty_at_2x * (
        math.sqrt(runtime_ratio) - math.sqrt(runtime_deadband_ratio)
    ) / denominator


def score_candidate(
    *,
    candidate: dict[str, Any],
    reference: dict[str, Any],
    hpwl_weight: float,
    runtime_deadband_ratio: float,
    runtime_penalty_at_2x: float,
    fail_score: float,
) -> tuple[float, dict[str, float | str]]:
    if not is_clean(candidate):
        return fail_score, {"status": "fail"}
    hpwl_c = float(hpwl_metrics(candidate)["after_micron"])
    hpwl_r = float(hpwl_metrics(reference)["after_micron"])
    hpwl_g = hpwl_global_micron(candidate, hpwl_r)
    runtime_c = float(candidate.get("runtime_seconds") or 0.0)
    runtime_r = float(reference.get("runtime_seconds") or 0.0)
    if hpwl_r == 0.0 or runtime_c <= 0.0 or runtime_r <= 0.0:
        return fail_score, {"status": "bad_metrics"}
    runtime_ratio_value = runtime_c / runtime_r
    hpwl_gain_percent = 100.0 * (hpwl_r - hpwl_c) / hpwl_r
    runtime_penalty = runtime_penalty_pp(
        runtime_ratio_value,
        runtime_deadband_ratio=runtime_deadband_ratio,
        runtime_penalty_at_2x=runtime_penalty_at_2x,
    )
    gain = hpwl_gain_percent - runtime_penalty
    # Primary BO objective: lower final HPWL is better.  Runtime-aware G_HR is
    # kept as a value diagnostic so fast-but-worse trials do not hide the best
    # HPWL point.
    score = hpwl_c
    return score, {
        "status": "ok",
        "score_objective": "min_hpwl_final",
        "hpwl_final": hpwl_c,
        "hpwl_ref": hpwl_r,
        "hpwl_global": hpwl_g,
        "runtime": runtime_c,
        "runtime_ref": runtime_r,
        "gain_hr": gain,
        "hpwl_gain_percent": hpwl_gain_percent,
        "runtime_penalty_pp": runtime_penalty,
        "runtime_deadband_ratio": runtime_deadband_ratio,
        "runtime_penalty_at_2x": runtime_penalty_at_2x,
    }


def run_baseline(
    *,
    agent_root: Path,
    case_id: str,
    flow_variant: str,
    line: str,
    run_tag: str,
    threads: int,
    timeout_seconds: int | None,
    openroad_binary: str | None,
    extra_env: dict[str, str] | None = None,
    dry_run: bool = False,
) -> tuple[int, Path | None, str]:
    cmd = [
        str(agent_root / "baseline" / "run_baseline.sh"),
        "--line",
        line,
        "--case",
        case_id,
        "--flow-variant",
        flow_variant,
        "--run-tag",
        run_tag,
        "--threads",
        str(threads),
    ]
    if timeout_seconds:
        cmd.extend(["--legalize-timeout-seconds", str(timeout_seconds)])
    if openroad_binary:
        cmd.extend(["--openroad-binary", openroad_binary])
    env = os.environ.copy()
    if extra_env:
        env.update({key: value for key, value in extra_env.items() if value})
    if dry_run:
        return 0, None, "DRY_RUN " + " ".join(cmd)
    proc = subprocess.run(
        cmd,
        cwd=str(agent_root.parent),
        env=env,
        text=True,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        check=False,
    )
    metrics_path: Path | None = None
    for line_text in proc.stdout.splitlines():
        if line_text.strip().startswith("metrics:"):
            raw = line_text.split(":", 1)[1].strip()
            metrics_path = Path(raw)
            if not metrics_path.is_absolute():
                metrics_path = (agent_root.parent / "OpenROAD-flow-scripts" / "flow" / raw).resolve()
    return proc.returncode, metrics_path, proc.stdout


def write_summary_tsv(path: Path, trials: list[dict[str, Any]], specs: list[ParamSpec]) -> None:
    fields = [
        "trial",
        "status",
        "score",
        "hpwl_final",
        "hpwl_ref",
        "hpwl_delta_percent",
        "runtime",
        "runtime_ratio",
        "gain_hr",
        "hpwl_gain_percent",
        "runtime_penalty_pp",
        "legalize_exit_status",
        "metrics_path",
        *[spec.name for spec in specs],
    ]
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8", newline="") as fh:
        writer = csv.DictWriter(fh, fieldnames=fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        for row in trials:
            params = row.get("params") or {}
            metrics = row.get("metrics") or {}
            flat = {
                "trial": row.get("trial"),
                "status": row.get("status"),
                "score": row.get("score"),
                "hpwl_final": metrics.get("hpwl_final"),
                "hpwl_ref": metrics.get("hpwl_ref"),
                "hpwl_delta_percent": row.get("hpwl_delta_percent"),
                "runtime": metrics.get("runtime"),
                "runtime_ratio": row.get("runtime_ratio"),
                "gain_hr": metrics.get("gain_hr"),
                "hpwl_gain_percent": metrics.get("hpwl_gain_percent"),
                "runtime_penalty_pp": metrics.get("runtime_penalty_pp"),
                "legalize_exit_status": row.get("legalize_exit_status"),
                "metrics_path": row.get("metrics_path"),
            }
            flat.update(params)
            writer.writerow(flat)


def write_jsonl(path: Path, rows: list[dict[str, Any]]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as fh:
        for row in rows:
            fh.write(json.dumps(row, sort_keys=True) + "\n")


def write_best_json(path: Path, trials: list[dict[str, Any]]) -> dict[str, Any] | None:
    ok_trials = [trial for trial in trials if trial.get("status") == "ok"]
    if not ok_trials:
        return None
    best = min(
        ok_trials,
        key=lambda trial: float((trial.get("metrics") or {}).get("hpwl_final") or trial["score"]),
    )
    path.write_text(json.dumps(best, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return best


def write_best_value_json(path: Path, trials: list[dict[str, Any]]) -> dict[str, Any] | None:
    ok_trials = [trial for trial in trials if trial.get("status") == "ok"]
    value_trials = [
        trial
        for trial in ok_trials
        if (trial.get("metrics") or {}).get("gain_hr") is not None
    ]
    if not value_trials:
        return None
    best = max(
        value_trials,
        key=lambda trial: (
            float((trial.get("metrics") or {}).get("gain_hr") or float("-inf")),
            -float((trial.get("metrics") or {}).get("hpwl_final") or float("inf")),
        ),
    )
    path.write_text(json.dumps(best, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return best


def run_trial_once(
    *,
    agent_root: Path,
    case_id: str,
    flow_variant: str,
    line: str,
    run_id: str,
    trial_label: str,
    specs: list[ParamSpec],
    params: dict[str, Any],
    reference: dict[str, Any],
    hpwl_weight: float,
    runtime_deadband_ratio: float,
    runtime_penalty_at_2x: float,
    fail_score: float,
    threads: int,
    timeout_seconds: int | None,
    openroad_binary: str | None,
    env_prefix: str,
) -> dict[str, Any]:
    params = normalize_params(specs, params)
    fp = params_fingerprint(params)
    env_args = build_stage_args(specs, params, env_prefix=env_prefix)
    run_tag = f"{run_id}_trial_{trial_label}_{fp}"
    rc, metrics_path, stdout = run_baseline(
        agent_root=agent_root,
        case_id=case_id,
        flow_variant=flow_variant,
        line=line,
        run_tag=run_tag,
        threads=threads,
        timeout_seconds=timeout_seconds,
        openroad_binary=openroad_binary,
        extra_env=env_args,
    )
    status = "fail"
    score = fail_score
    score_detail: dict[str, Any] = {"status": "missing_metrics"}
    legalize_exit_status = rc
    hpwl_delta_percent: float | str = ""
    runtime_ratio: float | str = ""
    if metrics_path is not None and metrics_path.is_file():
        metrics = load_metrics(metrics_path)
        legalize_exit_status = metrics.get("legalize_exit_status", rc)
        score, score_detail = score_candidate(
            candidate=metrics,
            reference=reference,
            hpwl_weight=hpwl_weight,
            runtime_deadband_ratio=runtime_deadband_ratio,
            runtime_penalty_at_2x=runtime_penalty_at_2x,
            fail_score=fail_score,
        )
        status = str(score_detail.get("status", "fail"))
        if status == "ok":
            hpwl_delta_percent = (
                (float(score_detail["hpwl_final"]) - float(score_detail["hpwl_ref"]))
                / float(score_detail["hpwl_ref"])
                * 100.0
            )
            runtime_ratio = float(score_detail["runtime"]) / float(score_detail["runtime_ref"])
    row: dict[str, Any] = {
        "trial": trial_label,
        "fingerprint": fp,
        "status": status,
        "score": score,
        "metrics": score_detail,
        "hpwl_delta_percent": hpwl_delta_percent,
        "runtime_ratio": runtime_ratio,
        "legalize_exit_status": legalize_exit_status,
        "metrics_path": str(metrics_path) if metrics_path else "",
        "run_tag": run_tag,
        "params": params,
        "stage_args": env_args,
    }
    if rc != 0:
        row["stdout_tail"] = "\n".join(stdout.splitlines()[-80:])
    return row


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--case", required=True, dest="case_id")
    parser.add_argument("--flow-variant", default=None)
    parser.add_argument("--space", type=Path, default=DEFAULT_SPACE)
    parser.add_argument("--run-id", default=None)
    parser.add_argument("--line", default=None, help="Override line from search-space YAML.")
    parser.add_argument("--reference-line", default=None)
    parser.add_argument("--trials", type=int, default=100)
    parser.add_argument("--max-concurrent-trials", type=int, default=4)
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument(
        "--startup-trials",
        type=int,
        default=None,
        help=(
            "Number of initial random/Sobol-like TPE startup trials before "
            "model-guided exploitation. Default: min(25, max(10, trials/4))."
        ),
    )
    parser.add_argument(
        "--anchor-strategy",
        choices=("mechanism", "default", "none"),
        default="mechanism",
        help=(
            "Deterministic DSE seed points before TPE. 'mechanism' covers "
            "generic DPL mechanisms; 'default' runs only the OpenROAD anchor; "
            "'none' lets TPE sample all points."
        ),
    )
    parser.add_argument(
        "--tpe-candidates",
        type=int,
        default=64,
        help="Number of TPE expected-improvement candidates per suggestion.",
    )
    parser.add_argument("--threads", type=int, default=10)
    parser.add_argument("--ray-cpus", type=int, default=None)
    parser.add_argument("--legalize-timeout-seconds", type=int, default=None)
    parser.add_argument("--openroad-binary", default=None)
    parser.add_argument("--dry-run", action="store_true")
    parser.add_argument("--overwrite", action="store_true")
    return parser.parse_args()


def resolve_flow_variant(case_id: str, flow_variant: str | None) -> str:
    if flow_variant is not None:
        return flow_variant
    from importlib.machinery import SourceFileLoader

    registry = SourceFileLoader(
        "case_registry",
        str(AGENT_ROOT / "scripts" / "repo" / "case_registry.py"),
    ).load_module()
    problem_path = registry.get_case(case_id).problem_path
    problem = load_yaml(problem_path)
    return str(problem.get("recommended_flow_variant") or "DENSE")


def main() -> int:
    args = parse_args()
    load_local_env_sh(AGENT_ROOT)
    for key in list(os.environ):
        if key.startswith("BASH_FUNC_"):
            os.environ.pop(key, None)
    ray, train, tune, RunConfig, tune_bits = require_ray_tune()
    TuneConfig, ConcurrencyLimiter, OptunaSearch, TPESampler = tune_bits

    if args.trials < 1:
        raise SystemExit("--trials must be positive")
    if args.max_concurrent_trials < 1:
        raise SystemExit("--max-concurrent-trials must be positive")
    if args.tpe_candidates < 1:
        raise SystemExit("--tpe-candidates must be positive")
    if args.startup_trials is None:
        startup_trials = min(25, max(10, args.trials // 4))
    else:
        startup_trials = args.startup_trials
    startup_trials = min(max(0, startup_trials), args.trials)

    space, specs = parse_space(args.space)
    objective = space.get("objective") or {}
    env_prefix = str(space.get("env_prefix") or "DPL_EVOLVE")
    line = args.line or str(space.get("line") or "openroad_dpl_flow")
    reference_line = args.reference_line or str(objective.get("reference_line") or "openroad_dpl_flow")
    hpwl_weight = float(objective.get("hpwl_weight", 1.0))
    runtime_deadband_ratio = float(objective.get("runtime_deadband_ratio", RUNTIME_DEADBAND_RATIO))
    runtime_penalty_at_2x = float(
        objective.get("runtime_penalty_at_2x", RUNTIME_PENALTY_AT_2X)
    )
    fail_score = float(objective.get("fail_score", 1e9))
    flow_variant = resolve_flow_variant(args.case_id, args.flow_variant)

    state_root = Path(os.environ.get("DPL_EVOLVE_STATE_ROOT", AGENT_ROOT / ".dpl_evolve_state")).resolve()
    run_id = args.run_id or f"bo_ray_{args.case_id}_{line}_seed{args.seed}"
    run_root = state_root / "bo_runs" / run_id
    if run_root.exists() and args.overwrite:
        shutil.rmtree(run_root)
    if run_root.exists() and not args.dry_run:
        raise SystemExit(f"BO run directory already exists; use --overwrite or a new --run-id: {run_root}")
    run_root.mkdir(parents=True, exist_ok=True)
    trials_jsonl = run_root / "trials.jsonl"
    trials_tsv = run_root / "trials.tsv"
    best_json = run_root / "best.json"
    best_value_json = run_root / "best_value.json"
    config_snapshot = run_root / "config.json"
    trial_rows_dir = run_root / "trial_rows"

    anchor_points = build_anchor_points(specs, args.anchor_strategy)
    default_trial_params = default_params(specs)
    default_env_args = build_stage_args(specs, default_trial_params, env_prefix=env_prefix)
    config_snapshot.write_text(
        json.dumps(
            {
                "case": args.case_id,
                "flow_variant": flow_variant,
                "line": line,
                "reference_line": reference_line,
                "env_prefix": env_prefix,
                "space": str(args.space),
                "trials": args.trials,
                "max_concurrent_trials": args.max_concurrent_trials,
                "seed": args.seed,
                "dse_strategy": {
                    "search_algorithm": "Optuna TPESampler",
                    "anchor_strategy": args.anchor_strategy,
                    "anchor_count": len(anchor_points),
                    "anchor_fingerprints": [params_fingerprint(point) for point in anchor_points],
                    "startup_trials": startup_trials,
                    "tpe_candidates": args.tpe_candidates,
                    "constant_liar": True,
                    "multivariate": True,
                    "group": True,
                    "notes": (
                        "Evaluate the OpenROAD default anchor first, spend the "
                        "remaining deterministic anchors on mechanism-shaped "
                        "coverage, spend the startup budget on broad mixed "
                        "discrete/continuous exploration, then exploit "
                        "promising parameter regions with parallel-aware TPE."
                    ),
                },
                "threads": args.threads,
                "ray_cpus": args.ray_cpus,
                "legalize_timeout_seconds": args.legalize_timeout_seconds,
                "openroad_binary": args.openroad_binary,
                "score": {
                    "formula": "score = HPWL_final; lower is better",
                    "gain_formula": "G_HR = 100*(HPWL_ref - HPWL_sol)/HPWL_ref - P(runtime_sol/runtime_ref); P(r)=0 when r<=runtime_deadband_ratio, otherwise runtime_penalty_at_2x*(sqrt(r)-sqrt(runtime_deadband_ratio))/(sqrt(2)-sqrt(runtime_deadband_ratio))",
                    "runtime_deadband_ratio": runtime_deadband_ratio,
                    "runtime_penalty_at_2x": runtime_penalty_at_2x,
                    "runtime_break_even": "Runtime speedups and <=deadband runtime noise receive no bonus; 2x runtime requires runtime_penalty_at_2x reference-normalized final-HPWL percentage points of improvement to be value-positive. This does not replace raw HPWL as the BO objective.",
                    "legacy_hpwl_weight_ignored": hpwl_weight,
                    "fail_score": fail_score,
                    "best_json": "best.json is HPWL-best only; best_value.json is G_HR-best for analysis only.",
                },
                "default_params": default_trial_params,
                "default_stage_args": default_env_args,
            },
            indent=2,
            sort_keys=True,
        )
        + "\n",
        encoding="utf-8",
    )

    if args.dry_run:
        ref_rc, _ref_metrics_path, ref_stdout = run_baseline(
            agent_root=AGENT_ROOT,
            case_id=args.case_id,
            flow_variant=flow_variant,
            line=reference_line,
            run_tag=f"{run_id}_reference_{reference_line}",
            threads=args.threads,
            timeout_seconds=args.legalize_timeout_seconds,
            openroad_binary=args.openroad_binary,
            dry_run=True,
        )
        cand_rc, _cand_metrics_path, cand_stdout = run_baseline(
            agent_root=AGENT_ROOT,
            case_id=args.case_id,
            flow_variant=flow_variant,
            line=line,
            run_tag=f"{run_id}_trial_default_{params_fingerprint(default_trial_params)}",
            threads=args.threads,
            timeout_seconds=args.legalize_timeout_seconds,
            openroad_binary=args.openroad_binary,
            extra_env=default_env_args,
            dry_run=True,
        )
        assert ref_rc == 0 and cand_rc == 0
        print("[DRY_RUN] Ray Tune backend is required and importable.")
        print("[DRY_RUN] reference")
        print(ref_stdout)
        print("[DRY_RUN] default candidate env")
        print(json.dumps(default_env_args, indent=2, sort_keys=True))
        print("[DRY_RUN] default candidate")
        print(cand_stdout)
        print("[DRY_RUN] dse_strategy")
        print(
            json.dumps(
                {
                    "default_anchor": True,
                    "anchor_count": len(anchor_points),
                    "anchor_strategy": args.anchor_strategy,
                    "anchor_fingerprints": [params_fingerprint(point) for point in anchor_points],
                    "startup_trials": startup_trials,
                    "tpe_candidates": args.tpe_candidates,
                    "sampler": "Optuna TPESampler",
                    "constant_liar": True,
                    "multivariate": True,
                    "group": True,
                },
                indent=2,
                sort_keys=True,
            )
        )
        print("[DRY_RUN] tune_space")
        print(json.dumps({spec.name: spec.kind for spec in specs}, indent=2, sort_keys=True))
        return 0

    ref_tag = f"{run_id}_reference_{reference_line}"
    ref_rc, ref_metrics_path, ref_stdout = run_baseline(
        agent_root=AGENT_ROOT,
        case_id=args.case_id,
        flow_variant=flow_variant,
        line=reference_line,
        run_tag=ref_tag,
        threads=args.threads,
        timeout_seconds=args.legalize_timeout_seconds,
        openroad_binary=args.openroad_binary,
        dry_run=False,
    )
    if ref_rc != 0 or ref_metrics_path is None or not ref_metrics_path.is_file():
        sys.stderr.write(ref_stdout)
        raise SystemExit(f"Reference run failed: {reference_line} rc={ref_rc}")
    reference = load_metrics(ref_metrics_path)
    if not is_clean(reference):
        raise SystemExit(f"Reference run is not clean: {ref_metrics_path}")

    print(f"[INFO] bo_run={run_id}")
    print(f"[INFO] case={args.case_id} flow_variant={flow_variant} line={line}")
    print(f"[INFO] reference={reference_line} metrics={ref_metrics_path}")
    print(f"[INFO] max_concurrent_trials={args.max_concurrent_trials} threads_per_trial={args.threads}")
    trial_rows_dir.mkdir(parents=True, exist_ok=True)

    ray_tmp = Path(tempfile.gettempdir()) / f"dpl_bo_ray_{hashlib.sha1(str(run_root).encode()).hexdigest()[:12]}"
    if ray_tmp.exists():
        shutil.rmtree(ray_tmp)
    ray_tmp.mkdir(parents=True, exist_ok=True)
    ray.init(
        num_cpus=args.ray_cpus or max(1, args.max_concurrent_trials),
        include_dashboard=False,
        ignore_reinit_error=True,
        _temp_dir=str(ray_tmp),
    )

    def trainable(config: dict[str, Any]) -> None:
        context = train.get_context()
        trial_label = context.get_trial_id()
        row = run_trial_once(
            agent_root=AGENT_ROOT,
            case_id=args.case_id,
            flow_variant=flow_variant,
            line=line,
            run_id=run_id,
            trial_label=trial_label,
            specs=specs,
            params=config,
            reference=reference,
            hpwl_weight=hpwl_weight,
            runtime_deadband_ratio=runtime_deadband_ratio,
            runtime_penalty_at_2x=runtime_penalty_at_2x,
            fail_score=fail_score,
            threads=args.threads,
            timeout_seconds=args.legalize_timeout_seconds,
            openroad_binary=args.openroad_binary,
            env_prefix=env_prefix,
        )
        row_path = trial_rows_dir / f"{trial_label}.json"
        row_path.write_text(json.dumps(row, indent=2, sort_keys=True) + "\n", encoding="utf-8")
        metrics = row.get("metrics") or {}
        train.report(
            {
                "score": float(row["score"]),
                "status_ok": 1 if row["status"] == "ok" else 0,
                "hpwl_final": float(metrics.get("hpwl_final", 0.0) or 0.0),
                "hpwl_delta_percent": float(row.get("hpwl_delta_percent") or 0.0),
                "gain_hr": float(metrics.get("gain_hr", 0.0) or 0.0),
                "runtime_ratio": float(row.get("runtime_ratio") or 0.0),
                "legalize_exit_status": int(row.get("legalize_exit_status") or 0),
            }
        )

    sampler = TPESampler(
        seed=args.seed,
        n_startup_trials=startup_trials,
        n_ei_candidates=args.tpe_candidates,
        multivariate=True,
        group=True,
        constant_liar=True,
        warn_independent_sampling=False,
    )
    search_alg = OptunaSearch(
        metric="score",
        mode="min",
        points_to_evaluate=anchor_points,
        sampler=sampler,
        seed=args.seed,
    )
    search_alg = ConcurrencyLimiter(search_alg, max_concurrent=args.max_concurrent_trials)
    tuner = tune.Tuner(
        trainable,
        param_space=build_tune_space(specs, tune),
        tune_config=TuneConfig(
            metric="score",
            mode="min",
            num_samples=args.trials,
            search_alg=search_alg,
        ),
        run_config=RunConfig(
            name=run_id,
            storage_path=str(run_root / "ray_results"),
            verbose=1,
        ),
    )
    result_grid = tuner.fit()

    trials: list[dict[str, Any]] = []
    for row_path in sorted(trial_rows_dir.glob("*.json")):
        trials.append(json.loads(row_path.read_text(encoding="utf-8")))
    trials.sort(key=lambda row: str(row.get("trial", "")))
    write_jsonl(trials_jsonl, trials)
    write_summary_tsv(trials_tsv, trials, specs)
    best = write_best_json(best_json, trials)
    best_value = write_best_value_json(best_value_json, trials)

    ok_trials = [trial for trial in trials if trial.get("status") == "ok"]
    print(f"[INFO] completed_trials={len(trials)} ok_trials={len(ok_trials)}")
    print(f"[INFO] trials_tsv={trials_tsv}")
    if best is not None:
        metrics = best.get("metrics") or {}
        print(f"[INFO] best_hpwl={float(metrics.get('hpwl_final') or best['score']):.6f}")
        if metrics.get("gain_hr") is not None:
            print(f"[INFO] best_hpwl_gain_hr={float(metrics['gain_hr']):.6f}")
        print(f"[INFO] best_hpwl_metrics={best.get('metrics_path')}")
        print(f"[INFO] best_hpwl_params={json.dumps(best['params'], sort_keys=True)}")
    else:
        print("[WARN] no clean legal HPWL trial found")
    if best_value is not None:
        metrics = best_value.get("metrics") or {}
        print(f"[INFO] best_value_gain_hr={float(metrics.get('gain_hr') or 0.0):.6f}")
        print(f"[INFO] best_value_hpwl={float(metrics.get('hpwl_final') or best_value['score']):.6f}")
        print(f"[INFO] best_value_metrics={best_value.get('metrics_path')}")
    elif best is not None:
        print("[WARN] no clean legal value trial found")
    ray.shutdown()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
