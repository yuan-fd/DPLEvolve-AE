#!/usr/bin/env python3
"""DPLEvolve AE — Experiment Config Validator.

Performs semantic validation for every supported AE experiment type.

Usage:
    python3 scripts/shared/validate_config.py [config_file]
    python3 scripts/shared/validate_config.py --all
"""

from __future__ import annotations

import json
import sys
from pathlib import Path

try:
    import yaml
except ImportError:
    print("[ERROR] PyYAML is required. Install with: pip install PyYAML==6.0.3")
    sys.exit(1)


# ── Semantic validation rules (beyond JSON Schema) ──────────────────────────

PAPER_CASES = {
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


def flattened_cases(cfg: dict) -> set[str]:
    cases = cfg.get("cases", {})
    if not isinstance(cases, dict):
        return set(cases) if isinstance(cases, list) else set()
    return {
        f"{design}_{platform}"
        for platform, designs in cases.items()
        if isinstance(designs, list)
        for design in designs
    }

def validate_smoke_config(cfg: dict) -> list[str]:
    """Validate smoke test config semantics."""
    errors = []
    if cfg.get("case") != "aes_nangate45":
        errors.append("smoke config: 'case' must be 'aes_nangate45'")
    expected = cfg.get("expected", {})
    required_metrics = [
        "instance_count", "instance_area_micron2",
        "global_hpwl_micron", "final_hpwl_micron",
    ]
    for m in required_metrics:
        if m not in expected:
            errors.append(f"smoke config: missing expected metric '{m}'")
    # instance_count is defined in the reproduction lock file
    # (artifacts/04-aes-smoke/expected/ae_reproduction_lock.json).
    # This validator only checks that the field exists in the config.
    if "instance_count" not in expected:
        errors.append("smoke config: missing required metric 'instance_count'")
    return errors


def validate_baseline_config(cfg: dict) -> list[str]:
    """Validate baseline config semantics."""
    errors = []
    cases = cfg.get("cases", {})
    nangate45 = cases.get("nangate45", [])
    asap7 = cases.get("asap7", [])
    total = len(nangate45) + len(asap7)
    if total < 1:
        errors.append("baseline config: no cases defined")
    lines = cfg.get("baseline_lines", [])
    if len(lines) < 1:
        errors.append("baseline config: no baseline lines defined")
    if flattened_cases(cfg) != PAPER_CASES:
        errors.append("baseline config: cases do not match the launcher paper set")
    return errors


def validate_evolve_config(cfg: dict) -> list[str]:
    """Validate evolve DSE config semantics."""
    errors = []
    arch = cfg.get("architecture", {})
    l2 = arch.get("level2", {})
    if l2.get("num_students", 0) < 1:
        errors.append("evolve config: num_students must be >= 1")
    if l2.get("teacher_model", "") == "":
        errors.append("evolve config: teacher_model not specified")
    search = cfg.get("search", {})
    if search.get("max_rounds", 0) < 1:
        errors.append("evolve config: max_rounds must be >= 1")
    if search.get("max_rounds") != 10 or search.get("candidates_per_round") != 4:
        errors.append("evolve config: paper uses 10 iterations x 4 students")
    if search.get("start_kind") != "per_case_level1_selection":
        errors.append("evolve config: paper uses per-case Level 1 source starts")
    selected = search.get("selected_start_kinds", {})
    selected_cases = {
        case for start_cases in selected.values() if isinstance(start_cases, list)
        for case in start_cases
    }
    if selected_cases != PAPER_CASES:
        errors.append("evolve config: selected start kinds must cover all paper cases")
    if flattened_cases(cfg) != PAPER_CASES:
        errors.append("evolve config: cases do not match evolve_9case")
    return errors


def validate_bo_config(cfg: dict) -> list[str]:
    """Validate the no-LLM BO experiment against its real launcher defaults."""
    errors = []
    if not cfg.get("search_space"):
        errors.append("BO config: search_space is empty")
    if cfg.get("budget", {}).get("iterations_per_case") != 400:
        errors.append("BO config: launcher and reference evidence use 400 trials/case")
    if flattened_cases(cfg) != PAPER_CASES:
        errors.append("BO config: cases do not match bo_9case")
    return errors


VALIDATORS = {
    "smoke": validate_smoke_config,
    "baseline": validate_baseline_config,
    "evolve_dse": validate_evolve_config,
    "bo_search": validate_bo_config,
}


def load_yaml(path: Path) -> dict:
    """Load a YAML file, returning a dict."""
    with open(path, encoding="utf-8") as f:
        data = yaml.safe_load(f)
    if not isinstance(data, dict):
        raise ValueError(f"YAML root must be a mapping, got {type(data).__name__}")
    return data


def validate_config(path: Path) -> bool:
    """Validate a single config file. Returns True if valid."""
    print(f"  Validating: {path.name}")
    try:
        cfg = load_yaml(path)
    except (yaml.YAMLError, ValueError) as e:
        print(f"    [FAIL] YAML parse error: {e}")
        return False

    experiment = cfg.get("experiment", "")
    print(f"    Experiment type: {experiment}")

    errors = []
    # Basic structural checks
    if "cases" in cfg:
        cases = cfg["cases"]
        if isinstance(cases, dict):
            total_cases = sum(len(v) for v in cases.values() if isinstance(v, list))
            print(f"    Cases defined: {total_cases}")
            if total_cases == 0:
                errors.append("no cases defined")

    # Run experiment-specific validator
    if experiment in VALIDATORS:
        errors.extend(VALIDATORS[experiment](cfg))
    else:
        errors.append(f"unsupported or missing experiment type: {experiment!r}")

    if errors:
        for e in errors:
            print(f"    [FAIL] {e}")
        return False

    print("    [OK]")
    return True


def main() -> int:
    if len(sys.argv) < 2:
        print("Usage: validate_config.py [config_file] | --all")
        print()
        print("Examples:")
        print("  python3 scripts/shared/validate_config.py artifacts/04-aes-smoke/config/aes_nangate45.yaml")
        print("  python3 scripts/shared/validate_config.py --all")
        return 1

    if sys.argv[1] == "--all":
        ae_root = Path(__file__).resolve().parent.parent.parent
        artifacts_dir = ae_root / "artifacts"
        yaml_files = sorted(artifacts_dir.glob("*/config/*.yaml"))
        if not yaml_files:
            print("[WARN] No YAML config files found.")
            return 0

        print(f"=== Validating {len(yaml_files)} config files ===\n")
        all_ok = True
        for yf in yaml_files:
            if not validate_config(yf):
                all_ok = False
            print()
        if all_ok:
            print("=== All configs valid ===")
            return 0
        else:
            print("=== Some configs have errors ===")
            return 1
    else:
        path = Path(sys.argv[1])
        if not path.exists():
            print(f"[ERROR] File not found: {path}")
            return 1
        ok = validate_config(path)
        return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
