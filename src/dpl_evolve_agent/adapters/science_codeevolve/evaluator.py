"""
Thin evaluator wrapper for the local dpl_evolve control plane.

This module does not replace the baseline harness. It only:
- resolves the correct project-standard commands
- applies local run/build locking
- enforces timeout and memory budgets
- captures compact failure tails for debugging
- optionally builds OpenROAD
- runs either one baseline line or the full suite
- loads the resulting metrics JSON
- writes a compact evaluator JSON
"""
from __future__ import annotations

import argparse
import csv
import json
import math
import os
import re
import subprocess
import sys
from pathlib import Path
from typing import Any, Dict, Iterable, List

AGENT_ROOT = Path(__file__).resolve().parents[2]
SCRIPT_DIR = AGENT_ROOT / "scripts"
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))
if str(SCRIPT_DIR) not in sys.path:
    sys.path.insert(0, str(SCRIPT_DIR))

from scripts.repo.case_registry import get_case
from runtime_paths import (
    clean_subprocess_env,
    load_local_env_sh,
    reexec_with_module,
    resolve_runtime_paths,
)
from scripts.teacher_loop.common import CANONICAL_LINES

VALUE_REFERENCE_LINE = "openroad_dpl_flow"
HPWL_RUNTIME_GAIN_FORMULA = (
    "G_HR = 100 * (HPWL_ref - HPWL_sol) / HPWL_ref "
    "- P(runtime_sol / runtime_ref), where P(r)=0 for r <= 1.10 "
    "and P(2.0)=1.0 percentage point"
)
# Runtime is a value check, not the primary objective: 2x runtime requires
# 1 percentage point of reference-normalized final-HPWL improvement to break even.
RUNTIME_DEADBAND_RATIO = 1.10
RUNTIME_PENALTY_AT_2X = 1.0

load_local_env_sh(AGENT_ROOT)
try:
    import yaml
except ModuleNotFoundError:
    reexec_with_module("yaml", __file__, sys.argv)
    raise

if __package__ in (None, ""):
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from guarded_exec import CommandResult, run_guarded_command
    from lock import DirectoryLockError
else:
    from .guarded_exec import CommandResult, run_guarded_command
    from .lock import DirectoryLockError


def runtime_paths():
    return resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="evaluator.py",
    )


def load_problem(problem_yaml: Path) -> Dict[str, Any]:
    return yaml.safe_load(problem_yaml.read_text(encoding="utf-8"))


def infer_design_config(problem: Dict[str, Any]) -> str:
    explicit = problem.get("design_config")
    if explicit:
        return explicit
    raise ValueError("problem.yaml must provide design_config")


def default_flow_variant(problem: Dict[str, Any], mode: str) -> str:
    design = problem.get("design", "design")
    platform = problem.get("platform", "platform")
    return f"eval_{mode}_{platform}_{design}"


def shell_join(args: Iterable[str]) -> str:
    return " ".join(subprocess.list2cmdline([arg]) for arg in args)


def baseline_root(agent_root: Path) -> Path:
    return agent_root / "baseline"


def resolve_runtime_path(
    root: Path,
    explicit_value: str | None,
    budget: Dict[str, Any],
    budget_key: str,
    default_path: Path,
) -> Path:
    value = explicit_value if explicit_value else budget.get(budget_key)
    if not value:
        return default_path
    path = Path(os.path.expandvars(os.path.expanduser(str(value))))
    if not path.is_absolute():
        if path.parts and path.parts[0] == root.name:
            return (root.parent / path).resolve()
        if path.exists() or path.parent.exists():
            return path.resolve()
        path = root / path
    return path.resolve()


def build_command(
    agent_root: Path,
    threads: int,
    build_mode: str,
    build_dir: Path,
    install_root: Path,
    openroad_binary_path: Path,
) -> List[str]:
    if build_mode != "openroad_only":
        raise ValueError(f"Unsupported build_mode: {build_mode}")
    return [
        str(agent_root / "scripts" / "workspace" / "build_openroad_only.sh"),
        "--threads",
        str(threads),
        "--build-dir",
        str(build_dir),
        "--install-root",
        str(install_root),
        "--openroad-binary",
        str(openroad_binary_path),
    ]


def line_command(
    agent_root: Path,
    line: str,
    design_config: str,
    flow_variant: str,
    run_tag: str,
    threads: int,
    openroad_binary: Path,
) -> List[str]:
    base = [
        str(baseline_root(agent_root) / "run_baseline.sh"),
        "--line",
        line,
        "--design-config",
        design_config,
        "--flow-variant",
        flow_variant,
        "--run-tag",
        run_tag,
        "--threads",
        str(threads),
        "--openroad-binary",
        str(openroad_binary),
    ]
    if line in CANONICAL_LINES:
        return base
    raise ValueError(f"Unsupported line: {line}")


def suite_command(
    agent_root: Path,
    design_config: str,
    flow_variant: str,
    threads: int,
    tag_prefix: str,
    openroad_binary: Path,
) -> List[str]:
    cmd = [
        str(baseline_root(agent_root) / "run_baseline_suite.sh"),
        "--design-config",
        design_config,
        "--flow-variant",
        flow_variant,
        "--threads",
        str(threads),
        "--openroad-binary",
        str(openroad_binary),
    ]
    if tag_prefix:
        cmd.extend(["--tag-prefix", tag_prefix])
    return cmd


def report_root(root: Path, problem: Dict[str, Any], flow_variant: str) -> Path:
    return (
        root
        / "flow"
        / "reports"
        / problem["platform"]
        / problem["design"]
        / flow_variant
        / "dpl_evolve_baseline"
    )


def metrics_path_for_run(report_dir: Path, run_tag: str) -> Path:
    return report_dir / run_tag / "metrics.json"


def load_metrics(metrics_path: Path) -> Dict[str, Any]:
    return json.loads(metrics_path.read_text(encoding="utf-8"))


def canonical_hpwl(metrics: Dict[str, Any]) -> Dict[str, Any]:
    hpwl = metrics.get("hpwl")
    if not isinstance(hpwl, dict) or hpwl.get("source") == "cell_bbox_proxy":
        hpwl = metrics.get("hpwl_openroad_log")
    if not isinstance(hpwl, dict):
        hpwl = {}
    return hpwl


def is_clean_placement(metrics: Dict[str, Any]) -> bool:
    violations = str(metrics.get("legality", {}).get("placement_violations", ""))
    return violations.strip().lower() in {"", "0", "clean", "none"}


def canonical_line(metrics: Dict[str, Any]) -> str:
    manifest = metrics.get("manifest", {})
    line = manifest.get("line")
    if line:
        return str(line)
    legalization = metrics.get("legalization", {})
    return str(legalization.get("legalizer_mode", ""))


def canonical_run_tag(metrics: Dict[str, Any], metrics_path: Path) -> str:
    manifest = metrics.get("manifest", {})
    return str(manifest.get("run_tag", metrics_path.parent.name))


def discover_best_clean_canonical_line(
    report_dir: Path,
) -> tuple[str | None, float | None, Path | None]:
    best_label: str | None = None
    best_hpwl: float | None = None
    best_path: Path | None = None
    for metrics_path in sorted(report_dir.glob("*/metrics.json")):
        metrics = load_metrics(metrics_path)
        line = canonical_line(metrics)
        if line not in CANONICAL_LINES:
            continue
        tag = canonical_run_tag(metrics, metrics_path)
        if (
            f"baseline_probe_{line}" not in tag
            or "_student_" in tag
            or "_iter_" in tag
            or not is_clean_placement(metrics)
        ):
            continue
        after = canonical_hpwl(metrics).get("after_micron")
        if after is None:
            continue
        after_value = float(after)
        if best_hpwl is None or after_value < best_hpwl:
            best_label = line
            best_hpwl = after_value
            best_path = metrics_path
    return best_label, best_hpwl, best_path


def discover_value_reference_line(
    report_dir: Path,
) -> tuple[str | None, float | None, float | None, Path | None]:
    """Return the fixed OpenROAD default-Diamond value-gain reference if present."""
    best_fallback: tuple[str | None, float | None, float | None, Path | None] = (
        None,
        None,
        None,
        None,
    )
    for metrics_path in sorted(report_dir.glob("*/metrics.json")):
        metrics = load_metrics(metrics_path)
        if not is_clean_placement(metrics):
            continue
        line = canonical_line(metrics)
        if line not in CANONICAL_LINES:
            continue
        tag = canonical_run_tag(metrics, metrics_path)
        if (
            f"baseline_probe_{line}" not in tag
            or "_student_" in tag
            or "_iter_" in tag
        ):
            continue
        after = _float_or_none(canonical_hpwl(metrics).get("after_micron"))
        runtime_s = _float_or_none(metrics.get("runtime_seconds"))
        if after is None or runtime_s in (None, 0):
            continue
        if line == VALUE_REFERENCE_LINE:
            return line, after, runtime_s, metrics_path
        if best_fallback[1] is None or after < float(best_fallback[1]):
            best_fallback = (line, after, runtime_s, metrics_path)
    return best_fallback


def hpwl_delta_percent(hpwl: Dict[str, Any]) -> float | None:
    if hpwl.get("delta_percent") is not None:
        return float(hpwl["delta_percent"])
    before = hpwl.get("before_micron")
    delta = hpwl.get("delta_micron")
    if before in (None, 0) or delta is None:
        return None
    before_value = float(before)
    if before_value == 0.0:
        return None
    return float(delta) / before_value * 100.0


def _float_or_none(value: Any) -> float | None:
    if value is None or value == "":
        return None
    try:
        return float(value)
    except (TypeError, ValueError):
        return None


def hpwl_runtime_gain_summary(
    *,
    hpwl_after_micron: Any,
    hpwl_global_micron: Any,
    runtime_seconds: Any,
    baseline_hpwl_after_micron: float | None,
    baseline_runtime_seconds: float | None,
) -> Dict[str, float] | None:
    hpwl_sol = _float_or_none(hpwl_after_micron)
    hpwl_global = _float_or_none(hpwl_global_micron)
    runtime_sol = _float_or_none(runtime_seconds)
    if (
        hpwl_sol in (None, 0)
        or runtime_sol in (None, 0)
        or baseline_hpwl_after_micron in (None, 0)
        or baseline_runtime_seconds in (None, 0)
    ):
        return None
    assert hpwl_sol is not None and runtime_sol is not None
    assert baseline_hpwl_after_micron is not None and baseline_runtime_seconds is not None
    if hpwl_global in (None, 0):
        hpwl_global = float(baseline_hpwl_after_micron)
    assert hpwl_global is not None
    runtime_ratio = runtime_sol / float(baseline_runtime_seconds)
    hpwl_gain_percent = (
        100.0
        * (float(baseline_hpwl_after_micron) - hpwl_sol)
        / float(baseline_hpwl_after_micron)
    )
    if runtime_ratio <= RUNTIME_DEADBAND_RATIO:
        runtime_penalty = 0.0
    else:
        runtime_penalty = RUNTIME_PENALTY_AT_2X * (
            math.sqrt(runtime_ratio) - math.sqrt(RUNTIME_DEADBAND_RATIO)
        ) / (math.sqrt(2.0) - math.sqrt(RUNTIME_DEADBAND_RATIO))
    gain = hpwl_gain_percent - runtime_penalty
    return {
        "gain_hr": gain,
        "score": gain,
        "H": hpwl_gain_percent,
        "R": runtime_penalty,
        "H_raw": hpwl_gain_percent,
        "R_raw": runtime_penalty,
        "hpwl_gain_percent": hpwl_gain_percent,
        "runtime_penalty_pp": runtime_penalty,
        "hpwl_global_micron": hpwl_global,
        "runtime_ratio": runtime_ratio,
    }


def _last_float(pattern: str, text: str) -> float | None:
    matches = re.findall(pattern, text, flags=re.MULTILINE)
    return _float_or_none(matches[-1]) if matches else None


def _first_float(pattern: str, text: str) -> float | None:
    match = re.search(pattern, text, flags=re.MULTILINE)
    return _float_or_none(match.group(1)) if match else None


def hpwl_stage_summary(metrics: Dict[str, Any], metrics_path: Path | None = None) -> Dict[str, Any]:
    hpwl = canonical_hpwl(metrics)
    stages = metrics.get("hpwl_stages")
    if not isinstance(stages, dict):
        stages = {}

    hpwl_global = _float_or_none(stages.get("global_micron"))
    hpwl_legalized = _float_or_none(stages.get("legalized_micron"))
    hpwl_after_improve = _float_or_none(stages.get("after_improve_micron"))
    hpwl_final = _float_or_none(stages.get("final_micron"))

    log_value = stages.get("log") or hpwl.get("log")
    log_path = Path(str(log_value)) if log_value else None
    if log_path is not None and not log_path.is_absolute() and metrics_path is not None:
        log_path = metrics_path.parent / log_path
    if log_path is not None and log_path.is_file():
        text = log_path.read_text(encoding="utf-8", errors="replace")
        hpwl_global = hpwl_global or _first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_global = hpwl_global or _first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_legalized = hpwl_legalized or _last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_after_improve = hpwl_after_improve or _last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
        hpwl_final = hpwl_final or _last_float(
            r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text
        )

    hpwl_global = hpwl_global if hpwl_global is not None else _float_or_none(hpwl.get("before_micron"))
    hpwl_final = hpwl_final if hpwl_final is not None else _float_or_none(hpwl.get("after_micron"))
    delta_legalization = (
        None if hpwl_global is None or hpwl_legalized is None else hpwl_legalized - hpwl_global
    )
    delta_improve = (
        None
        if hpwl_legalized is None or hpwl_after_improve is None
        else hpwl_after_improve - hpwl_legalized
    )
    delta_final = (
        None if hpwl_global is None or hpwl_final is None else hpwl_final - hpwl_global
    )

    def pct(delta: float | None) -> float | None:
        if delta is None or hpwl_global in (None, 0):
            return None
        return delta / hpwl_global * 100.0

    return {
        "HPWLg": hpwl_global,
        "HPWL_legalized": hpwl_legalized,
        "HPWL_after_improve": hpwl_after_improve,
        "HPWL_final": hpwl_final,
        "delta_HPWL_legalization": delta_legalization,
        "delta_HPWL_improve": delta_improve,
        "delta_HPWL_final": delta_final,
        "delta_HPWL_legalization_percent": pct(delta_legalization),
        "delta_HPWL_improve_percent": pct(delta_improve),
        "delta_HPWL_final_percent": pct(delta_final),
    }


def summarize_metrics(
    metrics: Dict[str, Any],
    *,
    metrics_path: Path | None = None,
    baseline_label: str | None = None,
    baseline_hpwl_after_micron: float | None = None,
    baseline_metrics_path: Path | None = None,
    value_reference_label: str | None = None,
    value_reference_hpwl_after_micron: float | None = None,
    value_reference_runtime_seconds: float | None = None,
    value_reference_metrics_path: Path | None = None,
) -> Dict[str, Any]:
    manifest = metrics.get("manifest", {})
    legalization = metrics.get("legalization", {})
    hpwl = canonical_hpwl(metrics)
    stage_hpwl = hpwl_stage_summary(metrics, metrics_path)
    engine = manifest.get("engine")
    legalizer_mode = legalization.get("legalizer_mode")
    confirmed_paths = {
        "openroad_dpl_flow": "openroad_dpl_flow",
        "openroad_dpl_negotiation": "openroad_dpl_negotiation",
        "evolve_default": "evolve_default",
    }
    engine_path_confirmed = confirmed_paths.get(str(legalizer_mode))
    if engine_path_confirmed is None:
        engine_path_confirmed = "unknown"
    hpwl_after = hpwl.get("after_micron")
    delta_vs_baseline = None
    delta_vs_baseline_pct = None
    if hpwl_after is not None and baseline_hpwl_after_micron not in (None, 0):
        delta_vs_baseline = float(hpwl_after) - float(baseline_hpwl_after_micron)
        delta_vs_baseline_pct = delta_vs_baseline / float(baseline_hpwl_after_micron) * 100.0
    summary = {
        "engine": engine,
        "engine_path_confirmed": engine_path_confirmed,
        "track": manifest.get("track"),
        "command_set": manifest.get("command_set"),
        "legalization_status": legalization.get("status"),
        "placement_violations": metrics.get("legality", {}).get("placement_violations"),
        "hpwl_source": hpwl.get("source"),
        "hpwl_before_micron": hpwl.get("before_micron"),
        "hpwl_after_micron": hpwl_after,
        "hpwl_delta_micron": hpwl.get("delta_micron"),
        "hpwl_delta_percent": hpwl_delta_percent(hpwl),
        "hpwl_stages": stage_hpwl,
        "runtime_seconds": metrics.get("runtime_seconds"),
        "setup_ws": metrics.get("timing_metrics", {}).get("setup_ws"),
        "setup_tns": metrics.get("timing_metrics", {}).get("setup_tns"),
        "hold_ws": metrics.get("timing_metrics", {}).get("hold_ws"),
        "hold_tns": metrics.get("timing_metrics", {}).get("hold_tns"),
        "total_power": metrics.get("power_metrics", {}).get("total"),
        "avg_displacement_micron": metrics.get("displacement", {}).get("average_displacement_micron"),
        "max_displacement_micron": metrics.get("displacement", {}).get("max_displacement_micron"),
    }
    gain = hpwl_runtime_gain_summary(
        hpwl_after_micron=hpwl_after,
        hpwl_global_micron=stage_hpwl.get("HPWLg"),
        runtime_seconds=metrics.get("runtime_seconds"),
        baseline_hpwl_after_micron=value_reference_hpwl_after_micron,
        baseline_runtime_seconds=value_reference_runtime_seconds,
    )
    if gain is not None:
        summary["hpwl_runtime_gain"] = {
            "formula": HPWL_RUNTIME_GAIN_FORMULA,
            "gain_hr": gain["gain_hr"],
            "score": gain["score"],
            "H": gain["H"],
            "R": gain["R"],
            "H_raw": gain["H_raw"],
            "R_raw": gain["R_raw"],
            "hpwl_gain_percent": gain["hpwl_gain_percent"],
            "runtime_penalty_pp": gain["runtime_penalty_pp"],
            "runtime_ratio": gain["runtime_ratio"],
            "reference_label": value_reference_label,
            "reference_hpwl_after_micron": value_reference_hpwl_after_micron,
            "reference_runtime_seconds": value_reference_runtime_seconds,
            "reference_metrics_path": None
            if value_reference_metrics_path is None
            else str(value_reference_metrics_path),
        }
    if baseline_hpwl_after_micron is not None:
        summary.update(
            {
                "baseline_label": baseline_label,
                "baseline_hpwl_after_micron": baseline_hpwl_after_micron,
                "baseline_metrics_path": None
                if baseline_metrics_path is None
                else str(baseline_metrics_path),
                "hpwl_delta_vs_baseline_micron": delta_vs_baseline,
                "hpwl_delta_vs_baseline_percent": delta_vs_baseline_pct,
            }
        )
    return summary


def load_suite_summary(summary_tsv: Path) -> List[Dict[str, str]]:
    with summary_tsv.open("r", encoding="utf-8", newline="") as fh:
        return list(csv.DictReader(fh, delimiter="\t"))


def load_budget(config_path: Path) -> Dict[str, Any]:
    if not config_path.exists():
        return {}
    config = yaml.safe_load(config_path.read_text(encoding="utf-8")) or {}
    return config.get("BUDGET_CONFIG", {})


def resolve_timeout(
    explicit_value: int | None,
    budget: Dict[str, Any],
    budget_key: str,
) -> int | None:
    if explicit_value is not None:
        return explicit_value
    value = budget.get(budget_key)
    if value in (None, "", 0):
        return None
    return int(value)


def resolve_memory_limit_bytes(
    explicit_mb: int | None,
    budget: Dict[str, Any],
    budget_key: str,
) -> int | None:
    if explicit_mb is not None:
        return explicit_mb * 1024 * 1024
    value = budget.get(budget_key)
    if value in (None, "", 0):
        return None
    return int(value) * 1024 * 1024


def run_dir_for_lock(
    root: Path,
    problem: Dict[str, Any],
    flow_variant: str,
    mode: str,
    run_tag: str | None = None,
) -> Path:
    base = report_root(root, problem, flow_variant)
    if mode == "suite":
        return base
    assert run_tag is not None
    return base / run_tag


def build_dir_for_lock(install_root: Path) -> Path:
    return install_root


def execute_step(
    cmd: List[str],
    cwd: Path,
    *,
    timeout_seconds: int | None,
    max_memory_bytes: int | None,
    monitor_interval_seconds: float,
    lock_dir: Path | None,
    env: Dict[str, str] | None = None,
) -> CommandResult:
    return run_guarded_command(
        cmd,
        cwd,
        timeout_seconds=timeout_seconds,
        max_memory_bytes=max_memory_bytes,
        monitor_interval_seconds=monitor_interval_seconds,
        env=env,
        lock_dir=lock_dir,
    )


def evaluate(args: argparse.Namespace) -> Dict[str, Any]:
    runtime = runtime_paths()
    root = runtime.orfs_root
    agent_root = runtime.agent_root
    problem = load_problem(args.problem)
    design_config = infer_design_config(problem)
    flow_variant = args.flow_variant or default_flow_variant(problem, args.mode)
    out_path = args.out.resolve()
    out_path.parent.mkdir(parents=True, exist_ok=True)
    budget = load_budget(args.config)
    build_timeout = resolve_timeout(args.build_timeout_sec, budget, "build_timeout_sec")
    eval_timeout = resolve_timeout(args.eval_timeout_sec, budget, "eval_timeout_sec")
    build_mem_bytes = resolve_memory_limit_bytes(
        args.build_mem_limit_mb, budget, "build_mem_limit_mb"
    )
    eval_mem_bytes = resolve_memory_limit_bytes(
        args.eval_mem_limit_mb, budget, "eval_mem_limit_mb"
    )
    monitor_interval_seconds = float(
        args.monitor_interval_sec
        if args.monitor_interval_sec is not None
        else budget.get("monitor_interval_sec", 0.5)
    )
    build_mode = str(budget.get("build_mode", args.build_mode or "openroad_only"))
    openroad_build_dir = resolve_runtime_path(
        root,
        args.openroad_build_dir,
        budget,
        "openroad_build_dir",
        root / "tools" / "OpenROAD" / "build",
    )
    openroad_install_root = resolve_runtime_path(
        root,
        args.openroad_install_root,
        budget,
        "openroad_install_root",
        root / "tools" / "install" / "OpenROAD",
    )
    openroad_bin = resolve_runtime_path(
        root,
        args.openroad_binary,
        budget,
        "openroad_binary",
        openroad_install_root / "bin" / "openroad",
    )
    explicit_openroad_binary = bool(args.openroad_binary)
    if explicit_openroad_binary:
        build_cmd: List[str] = []
        build_command_text = "skipped: explicit --openroad-binary"
    elif args.skip_build:
        build_cmd = []
        build_command_text = "skipped: --skip-build"
    else:
        build_cmd = build_command(
            agent_root,
            args.threads,
            build_mode,
            openroad_build_dir,
            openroad_install_root,
            openroad_bin,
        )
        build_command_text = shell_join(build_cmd)
    build_env = clean_subprocess_env()
    run_env = clean_subprocess_env()
    result: Dict[str, Any] = {
        "problem": problem.get("name"),
        "mode": args.mode,
        "design_config": design_config,
        "flow_variant": flow_variant,
        "build_mode": build_mode,
        "commands": {
            "build": build_command_text,
        },
        "budgets": {
            "build_timeout_sec": build_timeout,
            "eval_timeout_sec": eval_timeout,
            "build_mem_limit_mb": None
            if build_mem_bytes is None
            else build_mem_bytes // (1024 * 1024),
            "eval_mem_limit_mb": None
            if eval_mem_bytes is None
            else eval_mem_bytes // (1024 * 1024),
            "monitor_interval_sec": monitor_interval_seconds,
        },
        "paths": {
            "repo_root": str(root),
            "agent_root": str(agent_root),
            "state_root": str(runtime.state_root),
            "baseline_root": str(baseline_root(agent_root)),
            "openroad_build_dir": str(openroad_build_dir),
            "openroad_install_root": str(openroad_install_root),
            "openroad_binary": str(openroad_bin),
            "report_root": str(report_root(root, problem, flow_variant)),
        },
        "explicit_openroad_binary": explicit_openroad_binary,
        "command_results": {},
    }

    if args.mode == "suite":
        run_cmd = suite_command(
            agent_root,
            design_config,
            flow_variant,
            args.threads,
            args.tag_prefix,
            openroad_bin,
        )
        result["commands"]["run"] = shell_join(run_cmd)
        summary_tsv = report_root(root, problem, flow_variant) / "suite_runs.tsv"
        result["expected_summary_tsv"] = str(summary_tsv)
    else:
        run_tag = args.run_tag or args.line
        run_cmd = line_command(
            agent_root,
            args.line,
            design_config,
            flow_variant,
            run_tag,
            args.threads,
            openroad_bin,
        )
        result["line"] = args.line
        result["run_tag"] = run_tag
        result["commands"]["run"] = shell_join(run_cmd)
        result["expected_metrics_path"] = str(
            metrics_path_for_run(report_root(root, problem, flow_variant), run_tag)
        )

    if args.dry_run:
        result["status"] = "dry_run"
        return result

    try:
        if not args.use_existing:
            if not args.skip_build and build_cmd:
                build_result = execute_step(
                    build_cmd,
                    root,
                    timeout_seconds=build_timeout,
                    max_memory_bytes=build_mem_bytes,
                    monitor_interval_seconds=monitor_interval_seconds,
                    lock_dir=build_dir_for_lock(openroad_install_root),
                    env=build_env,
                )
                result["command_results"]["build"] = build_result.to_dict()
                if build_result.returncode != 0:
                    result["status"] = "build_failed"
                    return result

            run_result = execute_step(
                run_cmd,
                root,
                timeout_seconds=eval_timeout,
                max_memory_bytes=eval_mem_bytes,
                monitor_interval_seconds=monitor_interval_seconds,
                lock_dir=run_dir_for_lock(
                    root,
                    problem,
                    flow_variant,
                    args.mode,
                    result.get("run_tag"),
                ),
                env=run_env,
            )
            result["command_results"]["run"] = run_result.to_dict()
            if run_result.returncode != 0:
                result["status"] = "run_failed"
                return result

        if args.mode == "suite":
            summary_rows = load_suite_summary(summary_tsv)
            evaluated_rows = []
            loaded_rows: list[tuple[Dict[str, str], Path, Dict[str, Any] | None]] = []
            for row in summary_rows:
                metrics_path = Path(row["metrics_json"])
                if metrics_path.exists():
                    loaded_rows.append((row, metrics_path, load_metrics(metrics_path)))
                else:
                    loaded_rows.append((row, metrics_path, None))

            baseline_label: str | None = None
            baseline_hpwl: float | None = None
            baseline_path: Path | None = None
            value_reference_label: str | None = None
            value_reference_hpwl: float | None = None
            value_reference_runtime: float | None = None
            value_reference_path: Path | None = None
            for row, metrics_path, metrics_data in loaded_rows:
                if metrics_data is None or not is_clean_placement(metrics_data):
                    continue
                after = canonical_hpwl(metrics_data).get("after_micron")
                if after is None:
                    continue
                after_value = float(after)
                if baseline_hpwl is None or after_value < baseline_hpwl:
                    baseline_label = row["line_id"]
                    baseline_hpwl = after_value
                    baseline_path = metrics_path
                runtime_value = _float_or_none(metrics_data.get("runtime_seconds"))
                if (
                    row["line_id"] == VALUE_REFERENCE_LINE
                    and runtime_value not in (None, 0)
                ):
                    value_reference_label = row["line_id"]
                    value_reference_hpwl = after_value
                    value_reference_runtime = runtime_value
                    value_reference_path = metrics_path
            if value_reference_label is None:
                value_reference_label = baseline_label
                value_reference_hpwl = baseline_hpwl
                if baseline_path is not None:
                    baseline_data = load_metrics(baseline_path)
                    value_reference_runtime = _float_or_none(
                        baseline_data.get("runtime_seconds")
                    )
                value_reference_path = baseline_path

            for row, metrics_path, metrics_data in loaded_rows:
                summary = (
                    {"status": "missing_metrics"}
                    if metrics_data is None
                    else summarize_metrics(
                        metrics_data,
                        metrics_path=metrics_path,
                        baseline_label=baseline_label,
                        baseline_hpwl_after_micron=baseline_hpwl,
                        baseline_metrics_path=baseline_path,
                        value_reference_label=value_reference_label,
                        value_reference_hpwl_after_micron=value_reference_hpwl,
                        value_reference_runtime_seconds=value_reference_runtime,
                        value_reference_metrics_path=value_reference_path,
                    )
                )
                evaluated_rows.append(
                    {
                        "line_id": row["line_id"],
                        "run_tag": row["run_tag"],
                        "metrics_json": row["metrics_json"],
                        "summary": summary,
                    }
                )
            result["status"] = "ok"
            result["suite_summary_tsv"] = str(summary_tsv)
            result["runs"] = evaluated_rows
            return result

        metrics_path = metrics_path_for_run(report_root(root, problem, flow_variant), result["run_tag"])
        metrics = load_metrics(metrics_path)
        baseline_label, baseline_hpwl, baseline_path = discover_best_clean_canonical_line(
            report_root(root, problem, flow_variant)
        )
        (
            value_reference_label,
            value_reference_hpwl,
            value_reference_runtime,
            value_reference_path,
        ) = discover_value_reference_line(report_root(root, problem, flow_variant))
        result["status"] = "ok"
        result["metrics_path"] = str(metrics_path)
        result["summary"] = summarize_metrics(
            metrics,
            metrics_path=metrics_path,
            baseline_label=baseline_label,
            baseline_hpwl_after_micron=baseline_hpwl,
            baseline_metrics_path=baseline_path,
            value_reference_label=value_reference_label,
            value_reference_hpwl_after_micron=value_reference_hpwl,
            value_reference_runtime_seconds=value_reference_runtime,
            value_reference_metrics_path=value_reference_path,
        )
        return result
    except DirectoryLockError as exc:
        result["status"] = "locked"
        result["error"] = str(exc)
        return result
    except FileNotFoundError as exc:
        result["status"] = "missing_artifact"
        result["error"] = str(exc)
        return result


def main() -> None:
    ap = argparse.ArgumentParser()
    ap.add_argument(
        "--case",
        default="gcd_nangate45",
        help="Case id under problems/. This is the normal public entrypoint.",
    )
    ap.add_argument(
        "--problem",
        type=Path,
        help="Internal/debug override for a problem.yaml path. Prefer --case.",
    )
    ap.add_argument("--out", required=True, type=Path)
    ap.add_argument(
        "--config",
        type=Path,
        default=None,
    )
    ap.add_argument("--mode", choices=["single", "suite"], default="single")
    ap.add_argument(
        "--line",
        choices=[
            *CANONICAL_LINES,
        ],
        default="evolve_default",
    )
    ap.add_argument("--flow-variant")
    ap.add_argument("--run-tag")
    ap.add_argument("--tag-prefix", default="")
    ap.add_argument("--threads", type=int, default=8)
    ap.add_argument("--skip-build", action="store_true")
    ap.add_argument("--use-existing", action="store_true")
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--build-timeout-sec", type=int)
    ap.add_argument("--eval-timeout-sec", type=int)
    ap.add_argument("--build-mem-limit-mb", type=int)
    ap.add_argument("--eval-mem-limit-mb", type=int)
    ap.add_argument("--monitor-interval-sec", type=float)
    ap.add_argument(
        "--build-mode",
        choices=["openroad_only"],
    )
    ap.add_argument("--openroad-build-dir")
    ap.add_argument("--openroad-install-root")
    ap.add_argument("--openroad-binary")
    args = ap.parse_args()
    if args.problem is None:
        args.problem = get_case(args.case).problem_path
    if args.config is None:
        args.config = runtime_paths().agent_root / "configs" / "codex_smoke.yaml"

    result = evaluate(args)
    args.out.write_text(json.dumps(result, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
