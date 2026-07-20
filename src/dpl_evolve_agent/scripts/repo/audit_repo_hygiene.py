#!/usr/bin/env python3
"""Repository hygiene checks for the GitHub-facing control repo.

This keeps default agent context honest: patch/diff artifacts must be
classified, active problem surfaces must match the current framework, and
tracked files must not contain local-only paths.
"""
from __future__ import annotations

import re
import subprocess
import sys
from pathlib import Path


AGENT_ROOT = Path(__file__).resolve().parents[2]
PATCH_AUDIT = AGENT_ROOT / "patches" / "PATCH_AUDIT.yaml"
PROBLEMS_ROOT = AGENT_ROOT / "problems"

EXPECTED_PROBLEM_SURFACE = {
    "tools/OpenROAD/src/dpl_evolve/CMakeLists.txt",
    "tools/OpenROAD/src/dpl_evolve/include/dpl_evolve/Opendp.h",
    "tools/OpenROAD/src/dpl_evolve/src/DifferentialGuidance.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/EvolveContext.h",
    "tools/OpenROAD/src/dpl_evolve/src/EvolveLegalizer.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/EvolveLegalizer.h",
    "tools/OpenROAD/src/dpl_evolve/src/EvolveNegotiationRepair.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmCommon.h",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmFullLegalization.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmGuidance.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmRowAssignment.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmTechPenalty.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/LegalmTechPenalty.h",
    "tools/OpenROAD/src/dpl_evolve/src/Opendp.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/Opendp.i",
    "tools/OpenROAD/src/dpl_evolve/src/Opendp.tcl",
    "tools/OpenROAD/src/dpl_evolve/src/Optdp.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/PlacementDRC.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/PlacementDRC.h",
    "tools/OpenROAD/src/dpl_evolve/src/StudentAlgorithm.cpp",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_abu.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_abu.h",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_displacement.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_displacement.h",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_hpwl.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_hpwl.h",
    "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_objective.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_generator.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_global.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_global.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_manager.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_manager.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_mis.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_mis.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_orient.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_orient.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_random.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_random.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_reorder.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_reorder.h",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_vertical.cxx",
    "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_vertical.h",
}

LOCAL_CONTEXT_PATTERNS = tuple(
    pattern
    for pattern in (
        str(Path.home()),
        "/tmp/r6patch",
        "/opt/conda",
        "/opt/EDAs",
        "Agenticflow",
        "local_backups/",
    )
    if pattern and pattern != "."
)

LOCAL_CONTEXT_ALLOWED_FILES = {
    ".gitignore",
    "scripts/repo/audit_repo_hygiene.py",
}

FORBIDDEN_TRACKED_PREFIXES = (
    "baseline/dreamplace_vendor/",
)

ROLE_NAMESPACE_READMES = (
    "scripts/ae/README.md",
    "scripts/workspace/README.md",
    "scripts/evaluator/README.md",
    "scripts/matrix/README.md",
    "scripts/analysis/README.md",
    "scripts/calibration/README.md",
    "scripts/bo/README.md",
    "scripts/repo/README.md",
    "scripts/orchestration/README.md",
)

TOP_LEVEL_SCRIPT_ALLOWLIST = {
    "scripts/README.md",
    "scripts/runtime_env.sh",
    "scripts/optimize_case_with_codex.py",
    "scripts/run_codex_exec.py",
}

AUDIT_SELF_FILES = {
    "scripts/repo/audit_repo_hygiene.py",
}


def git_ls_files(*patterns: str) -> list[str]:
    proc = subprocess.run(
        ["git", "-C", str(AGENT_ROOT), "ls-files", *patterns],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in proc.stdout.splitlines() if line.strip()]


def git_visible_files() -> list[str]:
    proc = subprocess.run(
        [
            "git",
            "-C",
            str(AGENT_ROOT),
            "ls-files",
            "--cached",
            "--others",
            "--exclude-standard",
        ],
        check=True,
        capture_output=True,
        text=True,
    )
    return [line for line in proc.stdout.splitlines() if line.strip()]


def audited_paths() -> set[str]:
    text = PATCH_AUDIT.read_text(encoding="utf-8")
    return set(re.findall(r"^\s*-\s+path:\s+(.+?)\s*$", text, flags=re.MULTILINE))


def problem_yaml_files() -> list[Path]:
    return sorted(PROBLEMS_ROOT.glob("*/problem.yaml"))


def problem_header_values(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith(" ") or raw.startswith("-") or ":" not in raw:
            continue
        key, value = raw.split(":", 1)
        values[key.strip()] = value.strip().strip("'\"")
    return values


def problem_patch_surface(path: Path) -> set[str]:
    lines = path.read_text(encoding="utf-8").splitlines()
    surface: set[str] = set()
    in_surface = False
    for line in lines:
        if line.strip() == "patch_surface:":
            in_surface = True
            continue
        if not in_surface:
            continue
        if line and not line.startswith(" ") and not line.startswith("-"):
            break
        stripped = line.strip()
        if stripped.startswith("- "):
            surface.add(stripped[2:].strip())
    return surface


def tracked_text_files() -> list[str]:
    return [
        path
        for path in git_visible_files()
        if (AGENT_ROOT / path).exists()
        and not path.endswith((".patch", ".diff", ".pdf", ".pyc"))
    ]


def script_layout_errors() -> list[str]:
    errors: list[str] = []
    top_level_scripts = sorted(
        path
        for path in git_visible_files()
        if path.startswith("scripts/")
        and path.count("/") == 1
        and (AGENT_ROOT / path).is_file()
    )
    for path in top_level_scripts:
        if path not in TOP_LEVEL_SCRIPT_ALLOWLIST:
            errors.append(f"{path}: top-level scripts are restricted to stable shims")
    return errors


def visible_patch_files() -> set[str]:
    tracked = set(git_ls_files("*.patch", "*.diff"))
    discovered: set[str] = set()
    for pattern in ("*.patch", "*.diff"):
        for path in AGENT_ROOT.rglob(pattern):
            rel = path.relative_to(AGENT_ROOT).as_posix()
            if rel.startswith((".git/", ".dpl_evolve_state/", "local_backups/")):
                continue
            discovered.add(rel)
    return {path for path in tracked | discovered if (AGENT_ROOT / path).exists()}


def main() -> int:
    visible_patch_artifacts = visible_patch_files()
    audit_paths = audited_paths()
    missing = sorted(visible_patch_artifacts - audit_paths)
    stale = sorted(
        path
        for path in audit_paths
        if path.endswith((".patch", ".diff")) and path not in visible_patch_artifacts
    )
    visible_research = sorted(
        path for path in git_ls_files("family_variants/**") if "/research_patches/" in path
    )
    forbidden_tracked = sorted(
        path
        for path in git_ls_files()
        if (AGENT_ROOT / path).exists()
        and any(path.startswith(prefix) for prefix in FORBIDDEN_TRACKED_PREFIXES)
    )
    missing_role_readmes = [
        path for path in ROLE_NAMESPACE_READMES if not (AGENT_ROOT / path).is_file()
    ]
    script_errors = script_layout_errors()
    problem_errors: list[str] = []
    for path in problem_yaml_files():
        header = problem_header_values(path)
        for required in ("design", "platform", "design_config"):
            if not header.get(required):
                problem_errors.append(f"{path.relative_to(AGENT_ROOT)} missing {required}")
        surface = problem_patch_surface(path)
        if surface != EXPECTED_PROBLEM_SURFACE:
            problem_errors.append(
                f"{path.relative_to(AGENT_ROOT)} has stale patch_surface: {sorted(surface)}"
            )

    local_context_refs: list[str] = []
    for rel_path in tracked_text_files():
        if rel_path in AUDIT_SELF_FILES:
            continue
        path = AGENT_ROOT / rel_path
        try:
            text = path.read_text(encoding="utf-8")
        except UnicodeDecodeError:
            continue
        if rel_path not in LOCAL_CONTEXT_ALLOWED_FILES:
            for pattern in LOCAL_CONTEXT_PATTERNS:
                if pattern in text:
                    local_context_refs.append(f"{rel_path}: contains {pattern}")

    ok = True
    if missing:
        ok = False
        print("[FAIL] Tracked patch/diff files missing from patches/PATCH_AUDIT.yaml:")
        for path in missing:
            print(f"  - {path}")
    if stale:
        ok = False
        print("[FAIL] PATCH_AUDIT.yaml lists patch/diff files that are not tracked:")
        for path in stale:
            print(f"  - {path}")
    if visible_research:
        ok = False
        print("[FAIL] Visible family_variants research_patches are not allowed:")
        for path in visible_research:
            print(f"  - {path}")
    if forbidden_tracked:
        ok = False
        print("[FAIL] Forbidden tracked files that pollute default context:")
        for path in forbidden_tracked:
            print(f"  - {path}")
    if missing_role_readmes:
        ok = False
        print("[FAIL] Missing script role README files:")
        for path in missing_role_readmes:
            print(f"  - {path}")
    if script_errors:
        ok = False
        print("[FAIL] Script layout issues:")
        for item in script_errors:
            print(f"  - {item}")
    if problem_errors:
        ok = False
        print("[FAIL] Problem registry issues:")
        for item in problem_errors:
            print(f"  - {item}")
    if local_context_refs:
        ok = False
        print("[FAIL] Tracked files contain local-only paths/context:")
        for item in local_context_refs:
            print(f"  - {item}")
    if ok:
        print("[OK] Repo hygiene checks passed.")
        return 0
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
