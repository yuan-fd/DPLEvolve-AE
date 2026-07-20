#!/usr/bin/env python3
from __future__ import annotations

import argparse
import hashlib
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Any


def run(*args: str, cwd: Path | None = None) -> subprocess.CompletedProcess[str]:
    return subprocess.run(args, cwd=cwd, capture_output=True, text=True, check=False)


class Checks:
    def __init__(self) -> None:
        self.failures = 0
        self.warnings = 0

    def ok(self, message: str) -> None:
        print(f"[OK] {message}")

    def fail(self, message: str) -> None:
        self.failures += 1
        print(f"[FAIL] {message}")

    def warn(self, message: str) -> None:
        self.warnings += 1
        print(f"[WARN] {message}")


def git_head(path: Path) -> str | None:
    proc = run("git", "rev-parse", "HEAD", cwd=path)
    return proc.stdout.strip() if proc.returncode == 0 else None


def check_git_revision(
    checks: Checks,
    label: str,
    path: Path,
    expected: str,
    *,
    allow_descendant: bool = False,
) -> None:
    head = git_head(path)
    if head is None:
        checks.fail(f"{label} is not a readable git worktree: {path}")
        return
    if head == expected:
        checks.ok(f"{label} revision {expected[:12]}")
        return
    if allow_descendant:
        proc = run("git", "merge-base", "--is-ancestor", expected, "HEAD", cwd=path)
        if proc.returncode == 0:
            checks.ok(f"{label} descends from audited base {expected[:12]} (HEAD {head[:12]})")
            return
    checks.fail(f"{label} revision is {head}, expected {expected}")


def check_ancestor(checks: Checks, label: str, path: Path, ancestor: str) -> None:
    proc = run("git", "merge-base", "--is-ancestor", ancestor, "HEAD", cwd=path)
    if proc.returncode == 0:
        checks.ok(f"{label} contains base anchor {ancestor[:12]}")
    else:
        checks.fail(f"{label} does not contain base anchor {ancestor}")


def sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for chunk in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def check_binary(
    checks: Checks,
    label: str,
    path: Path,
    expected_hash: str,
    *,
    strict_hashes: bool,
) -> None:
    if not path.is_file() or not os.access(path, os.X_OK):
        checks.fail(f"{label} binary is missing or not executable: {path}")
        return
    actual_hash = sha256(path)
    if actual_hash == expected_hash:
        checks.ok(f"{label} binary hash {actual_hash[:12]}")
    elif strict_hashes:
        checks.fail(f"{label} binary hash is {actual_hash}, expected {expected_hash}")
    else:
        checks.warn(
            f"{label} binary hash differs from the audited machine build; "
            f"source revisions remain authoritative ({actual_hash})"
        )
    version_args = ("-V",) if label == "Yosys" else ("-version",)
    proc = run(str(path), *version_args)
    version = (proc.stdout or proc.stderr).strip().splitlines()
    if proc.returncode == 0 and version:
        checks.ok(f"{label} reports: {version[0]}")
    else:
        checks.fail(f"{label} version command failed")
    ldd = run("ldd", str(path))
    if ldd.returncode != 0:
        checks.fail(f"ldd failed for {label}: {(ldd.stderr or ldd.stdout).strip()}")
    elif "not found" in ldd.stdout:
        missing = [line.strip() for line in ldd.stdout.splitlines() if "not found" in line]
        checks.fail(f"{label} has unresolved libraries: {', '.join(missing)}")
    else:
        checks.ok(f"{label} shared libraries resolve")


def main() -> int:
    parser = argparse.ArgumentParser(description="Validate the pinned AE environment.")
    parser.add_argument("--agent-root", required=True, type=Path)
    parser.add_argument("--orfs-root", required=True, type=Path)
    parser.add_argument("--state-root", required=True, type=Path)
    parser.add_argument("--python", required=True, type=Path)
    parser.add_argument("--openroad-binary", type=Path)
    parser.add_argument("--yosys-binary", type=Path)
    parser.add_argument("--strict-hashes", action="store_true")
    args = parser.parse_args()

    agent_root = args.agent_root.resolve()
    orfs_root = args.orfs_root.resolve()
    state_root = args.state_root.resolve()
    lock: dict[str, Any] = json.loads(
        (agent_root / "metadata" / "ae_reproduction_lock.json").read_text(encoding="utf-8")
    )
    checks = Checks()

    for command in ("git", "make", "gcc", "g++", "cmake", "bison", "flex", "swig", "ldd"):
        path = shutil.which(command)
        if path:
            checks.ok(f"command {command}: {path}")
        else:
            checks.fail(f"required command is unavailable: {command}")

    repos = lock["repositories"]
    check_git_revision(
        checks,
        "dpl_evolve_agent",
        agent_root,
        repos["dpl_evolve_agent"]["base_commit"],
        allow_descendant=True,
    )
    check_git_revision(checks, "ORFS", orfs_root, repos["orfs"]["prepared_commit"])
    check_ancestor(checks, "ORFS", orfs_root, repos["orfs"]["base_commit"])
    openroad_root = orfs_root / "tools" / "OpenROAD"
    check_git_revision(
        checks, "OpenROAD", openroad_root, repos["openroad"]["prepared_commit"]
    )
    check_ancestor(checks, "OpenROAD", openroad_root, repos["openroad"]["base_commit"])

    submodule_paths = {
        "OpenSTA": openroad_root / "src" / "sta",
        "OpenROAD ABC": openroad_root / "third-party" / "abc",
        "Yosys": orfs_root / "tools" / "yosys",
        "Yosys ABC": orfs_root / "tools" / "yosys" / "abc",
        "Yosys cxxopts": orfs_root / "tools" / "yosys" / "libs" / "cxxopts",
    }
    submodule_keys = {
        "OpenSTA": "opensta",
        "OpenROAD ABC": "openroad_abc",
        "Yosys": "yosys",
        "Yosys ABC": "yosys_abc",
        "Yosys cxxopts": "yosys_cxxopts",
    }
    for label, path in submodule_paths.items():
        check_git_revision(checks, label, path, lock["submodules"][submodule_keys[label]])

    python_proc = run(str(args.python), "--version")
    expected_python = lock["python"]["major_minor"]
    reported_python = (python_proc.stdout or python_proc.stderr).strip()
    if python_proc.returncode == 0 and reported_python.startswith(f"Python {expected_python}."):
        checks.ok(reported_python)
    else:
        checks.fail(f"Python must be {expected_python}.x, got {reported_python or 'unavailable'}")
    yaml_proc = run(
        str(args.python),
        "-c",
        "import importlib.metadata; print(importlib.metadata.version('PyYAML'))",
    )
    expected_yaml = lock["python"]["packages"]["PyYAML"]
    if yaml_proc.returncode == 0 and yaml_proc.stdout.strip() == expected_yaml:
        checks.ok(f"PyYAML {expected_yaml}")
    else:
        checks.fail(f"PyYAML must be {expected_yaml}")

    openroad_binary = args.openroad_binary or (
        state_root / lock["reference_binaries"]["openroad"]["relative_state_path"]
    )
    yosys_binary = args.yosys_binary or (
        state_root / lock["reference_binaries"]["yosys"]["relative_state_path"]
    )
    check_binary(
        checks,
        "OpenROAD",
        openroad_binary.resolve(),
        lock["reference_binaries"]["openroad"]["sha256"],
        strict_hashes=args.strict_hashes,
    )
    check_binary(
        checks,
        "Yosys",
        yosys_binary.resolve(),
        lock["reference_binaries"]["yosys"]["sha256"],
        strict_hashes=args.strict_hashes,
    )

    print(
        f"\nEnvironment check complete: {checks.failures} failure(s), "
        f"{checks.warnings} warning(s)."
    )
    return 1 if checks.failures else 0


if __name__ == "__main__":
    raise SystemExit(main())
