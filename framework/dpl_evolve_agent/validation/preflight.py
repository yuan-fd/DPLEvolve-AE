#!/usr/bin/env python3
"""Preflight diff-scope validation for evolved dpl_evolve source patches.

The active workflow lets a student change legalization/detailed placement,
improve-placement internals, and their handoff inside a private `src/dpl_evolve`
tree. Downstream optimize-mirroring must remain callable for evaluation, but it
is not an active mutation target. The preflight gate protects the control plane
and classic OpenROAD `dpl` implementation, not a stale single-file patch model.
"""
from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import resolve_runtime_paths


FORBIDDEN_PREFIXES = (
    "flow/",
    "baseline/",
    "scripts/",
    "adapters/",
    "knowledge/",
    "problems/",
    "configs/",
    "tasks/",
    ".github/",
    "tools/OpenROAD/src/dpl/",
)

FORBIDDEN_EXACT = {
    ".gitmodules",
    "flow/Makefile",
    "Makefile",
}

IGNORED_EXACT = {
    # ORFS records OpenROAD as a submodule/gitlink.  Prepared workspaces often
    # show only this pointer at the ORFS root while the real source diff lives
    # inside the OpenROAD checkout or private variant tree.
    "tools/OpenROAD",
}


def _run_git(root: Path, args: list[str]) -> list[str]:
    completed = subprocess.run(
        ["git", "-C", str(root), *args],
        check=False,
        capture_output=True,
        text=True,
    )
    if completed.returncode != 0:
        return []
    return [line.strip() for line in completed.stdout.splitlines() if line.strip()]


def changed_files(root: Path) -> list[str]:
    files: set[str] = set()
    files.update(_run_git(root, ["diff", "--name-only"]))
    files.update(_run_git(root, ["diff", "--cached", "--name-only"]))
    files.update(_run_git(root, ["ls-files", "--others", "--exclude-standard"]))
    return sorted(files)


def _norm_repo_path(path: str) -> str | None:
    if not path:
        return None
    expanded = os.path.expandvars(path)
    if expanded.startswith("$"):
        return None
    candidate = Path(expanded)
    if candidate.is_absolute():
        return None
    return str(candidate)


def allowed_files_from_packet(packet: dict[str, Any]) -> set[str]:
    surface = packet.get("selected_patch_surface", {})
    allowed: set[str] = set()
    target = _norm_repo_path(str(surface.get("file") or packet.get("target_file") or ""))
    if target:
        allowed.add(target)

    for key in ("header_files", "extra_files"):
        for raw_path in surface.get(key, []) or []:
            normalized = _norm_repo_path(str(raw_path))
            if normalized:
                allowed.add(normalized)

    for raw_path in packet.get("patch_surface", []) or []:
        normalized = _norm_repo_path(str(raw_path))
        if normalized:
            allowed.add(normalized)
    return allowed


def allows_dpl_evolve_tree(packet: dict[str, Any]) -> bool:
    """Return whether this packet represents the active full-flow framework."""

    if bool(packet.get("allow_dpl_evolve_tree_patch")):
        return True
    packet_type = str(packet.get("packet_type") or "")
    if packet_type in {"constrained_framework_patch", "full_flow_framework_patch"}:
        return True
    symbol = str((packet.get("selected_patch_surface") or {}).get("symbol") or "")
    return "legalize/improve" in symbol or "full-flow" in symbol


def _is_forbidden(path: str) -> bool:
    if path in FORBIDDEN_EXACT:
        return True
    return any(path.startswith(prefix) for prefix in FORBIDDEN_PREFIXES)


def run_preflight(*, orfs_root: Path, packet: dict[str, Any]) -> dict[str, Any]:
    files = changed_files(orfs_root)
    allowed = allowed_files_from_packet(packet)
    allow_dpl_evolve_tree = allows_dpl_evolve_tree(packet)
    surface = packet.get("selected_patch_surface", {})
    family_variant_required = bool(surface.get("family_variant_required"))
    violations: list[dict[str, str]] = []
    warnings: list[str] = []

    for path in files:
        if path in IGNORED_EXACT:
            continue
        if _is_forbidden(path):
            violations.append(
                {
                    "path": path,
                    "reason": "forbidden control-plane, flow, or classic dpl path",
                }
            )
        elif (
            path.startswith("tools/OpenROAD/src/dpl_evolve/")
            and allow_dpl_evolve_tree
        ):
            continue
        elif path.startswith("tools/OpenROAD/src/dpl_evolve/") and path not in allowed:
            violations.append(
                {
                    "path": path,
                    "reason": "outside declared dpl_evolve patch surface",
                }
            )
        elif path not in allowed:
            violations.append(
                {
                    "path": path,
                    "reason": "outside declared patch surface",
                }
            )

    if not files and not family_variant_required:
        violations.append({"path": "", "reason": "no source diff produced by bounded patch"})
    if family_variant_required:
        warnings.append(
            "materialized variant source may live outside ORFS git diff; outer loop must ensure variant source and binary paths are explicit"
        )

    ok = not violations
    return {
        "ok": ok,
        "status": "ok" if ok else "failed",
        "changed_files": files,
        "allowed_files": sorted(allowed),
        "allow_dpl_evolve_tree": allow_dpl_evolve_tree,
        "violations": violations,
        "warnings": warnings,
        "selected_family": packet.get("selected_patch_family", {}).get("id"),
        "selected_symbol": surface.get("symbol"),
        "failure_signature": "; ".join(v["reason"] for v in violations[:3]) if violations else None,
    }


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--orfs-root", type=Path)
    parser.add_argument("--packet-json", required=True, type=Path)
    parser.add_argument("--out", type=Path)
    args = parser.parse_args()

    if args.orfs_root is None:
        runtime = resolve_runtime_paths(
            anchor_file=__file__,
            agent_root_levels_up=1,
            script_name="preflight.py",
        )
        orfs_root = runtime.orfs_root
    else:
        orfs_root = args.orfs_root.resolve()
    packet = json.loads(args.packet_json.read_text(encoding="utf-8"))
    result = run_preflight(orfs_root=orfs_root, packet=packet)
    text = json.dumps(result, indent=2, sort_keys=True) + "\n"
    if args.out:
        args.out.parent.mkdir(parents=True, exist_ok=True)
        args.out.write_text(text, encoding="utf-8")
    else:
        print(text, end="")
    raise SystemExit(0 if result["ok"] else 2)


if __name__ == "__main__":
    main()
