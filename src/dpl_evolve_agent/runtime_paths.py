from __future__ import annotations

import os
import shutil
import subprocess
import sys
import shlex
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable


def _is_orfs_root(path: Path) -> bool:
    return (path / "flow").is_dir() and (path / "tools" / "OpenROAD").is_dir()


def _default_orfs_root(agent_root: Path) -> Path:
    return (agent_root.parent / "OpenROAD-flow-scripts").resolve()


def resolve_agent_root(anchor_file: str | Path, *, levels_up: int) -> Path:
    env_root = os.environ.get("DPL_EVOLVE_AGENT_ROOT")
    if env_root:
        candidate = Path(env_root).resolve()
        if candidate.is_dir():
            return candidate
        raise SystemExit(
            f"DPL_EVOLVE_AGENT_ROOT does not exist or is not a directory: {candidate}"
        )
    return Path(anchor_file).resolve().parents[levels_up]


def load_local_env_sh(agent_root: Path) -> None:
    """Import repo-local env.sh for Python entrypoints.

    Shell wrappers already source env.sh through scripts/runtime_env.sh.  Python
    entrypoints also need the same local ORFS/state settings so agents do not
    accidentally depend on a mounted ORFS symlink or a globally exported env.
    Values explicitly provided by the caller take precedence over env.sh.
    """

    if os.environ.get("DPL_EVOLVE_SKIP_ENV_SH") == "1":
        return
    env_file = agent_root / "env.sh"
    if not env_file.is_file():
        return
    caller_overrides = {
        key: os.environ[key]
        for key in (
            "DPL_EVOLVE_AGENT_ROOT",
            "ORFS_ROOT",
            "ORFS_BUILD_ROOT",
            "DPL_EVOLVE_STATE_ROOT",
            "DPL_EVOLVE_ORFS_ALIAS_ROOT",
            "DPL_EVOLVE_PYTHON",
            "DPL_EVOLVE_PYTHON_CANDIDATES",
        )
        if key in os.environ
    }
    cmd = (
        "set -a; "
        f"source {shlex.quote(str(env_file))}; "
        f"{shlex.quote(sys.executable)} - <<'PY'\n"
        "import os\n"
        "for key, value in os.environ.items():\n"
        "    print(f'{key}={value}')\n"
        "PY"
    )
    proc = subprocess.run(
        ["bash", "-lc", cmd],
        capture_output=True,
        text=True,
        check=False,
    )
    if proc.returncode != 0:
        raise SystemExit(f"Failed to source local env.sh: {env_file}\n{proc.stderr or proc.stdout}")
    for line in proc.stdout.splitlines():
        if "=" not in line:
            continue
        key, value = line.split("=", 1)
        if key and key not in caller_overrides:
            os.environ[key] = value
    os.environ.update(caller_overrides)


def resolve_orfs_root(
    *,
    agent_root: Path,
    script_name: str,
) -> Path:
    env_root = os.environ.get("ORFS_ROOT")
    if env_root:
        candidate = Path(env_root).resolve()
        if _is_orfs_root(candidate):
            return candidate
        raise SystemExit(
            f"{script_name} expected ORFS_ROOT to point at an ORFS workspace, got: {candidate}"
        )

    sibling = _default_orfs_root(agent_root)
    if _is_orfs_root(sibling):
        return sibling

    raise SystemExit(
        f"{script_name} could not locate an ORFS workspace.\n"
        "Set ORFS_ROOT to a workspace containing flow/ and tools/OpenROAD/.\n"
        f"Default sibling lookup also failed: {sibling}"
    )


def resolve_state_root(*, orfs_root: Path, agent_root: Path) -> Path:
    env_root = os.environ.get("DPL_EVOLVE_STATE_ROOT")
    if env_root:
        return Path(env_root).resolve()
    return (agent_root.parent / "dpl_evolve_state").resolve()


def candidate_python_interpreters(
    *,
    extra_env_var: str = "DPL_EVOLVE_PYTHON_CANDIDATES",
) -> Iterable[str]:
    """Return user-configured Python candidates followed by portable defaults."""

    seen: set[str] = set()
    explicit = os.environ.get("DPL_EVOLVE_PYTHON", "").strip()
    if explicit:
        seen.add(explicit)
        yield explicit
    env_value = os.environ.get(extra_env_var, "")
    for raw in env_value.split(os.pathsep):
        candidate = raw.strip()
        if candidate and candidate not in seen:
            seen.add(candidate)
            yield candidate
    for candidate in ("python3", "python"):
        if candidate not in seen:
            seen.add(candidate)
            yield candidate


def reexec_with_module(module_name: str, script_file: str | Path, argv: list[str]) -> None:
    """Re-exec with a configured Python that can import module_name, if any."""

    current_probe = subprocess.run(
        [sys.executable, "-c", f"import {module_name}"],
        capture_output=True,
        text=True,
    )
    if current_probe.returncode == 0:
        os.environ.setdefault("DPL_EVOLVE_PYTHON", sys.executable)
        return

    for candidate in candidate_python_interpreters():
        if os.path.sep in candidate:
            candidate_path = Path(candidate).expanduser()
            if not candidate_path.exists():
                continue
            resolved = str(candidate_path)
        else:
            resolved = shutil.which(candidate)
        if not resolved or resolved == sys.executable:
            continue
        probe = subprocess.run(
            [resolved, "-c", f"import {module_name}"],
            capture_output=True,
            text=True,
        )
        if probe.returncode == 0:
            os.environ["DPL_EVOLVE_PYTHON"] = resolved
            os.execv(resolved, [resolved, str(script_file), *argv[1:]])


def ensure_python_module(module_name: str, script_file: str | Path, argv: list[str]) -> None:
    """Require the current or configured Python to import module_name."""

    reexec_with_module(module_name, script_file, argv)
    probe = subprocess.run(
        [sys.executable, "-c", f"import {module_name}"],
        capture_output=True,
        text=True,
    )
    if probe.returncode != 0:
        searched = ", ".join(candidate_python_interpreters())
        raise SystemExit(
            f"Could not find a Python interpreter that can import {module_name}. "
            "Set DPL_EVOLVE_PYTHON or DPL_EVOLVE_PYTHON_CANDIDATES in env.sh. "
            f"Searched: {searched}"
        )


def resolve_input_path(
    value: str | Path,
    *,
    cwd: Path | None,
    agent_root: Path,
    orfs_root: Path,
) -> Path:
    path = Path(os.path.expandvars(os.path.expanduser(str(value))))
    if path.is_absolute():
        return path.resolve()

    if cwd is not None:
        cwd_candidate = (cwd / path).resolve()
        if cwd_candidate.exists():
            return cwd_candidate

    if path.parts and path.parts[0] in {"evolve_agent", "dpl_evolve_agent"}:
        return (agent_root / Path(*path.parts[1:])).resolve()

    if path.parts and path.parts[0] in {"flow", "tools"}:
        return (orfs_root / path).resolve()

    agent_candidate = (agent_root / path).resolve()
    if agent_candidate.exists():
        return agent_candidate

    return (cwd / path).resolve() if cwd is not None else path.resolve()


def clean_subprocess_env(env: dict[str, str] | None = None) -> dict[str, str]:
    """Return an environment safe to pass to shell subprocesses.

    Interactive shells on shared EDA machines often export Bash functions such
    as module helpers or repo-local Python aliases as BASH_FUNC_* entries.  A
    non-interactive /bin/bash can print noisy "error importing function"
    diagnostics when those definitions are not portable.  They are not needed
    by the dpl_evolve workflow, so strip them from captured build/eval logs.

    Nested Codex workers should also not inherit the parent Codex thread.  The
    worker wrapper records each child operation from its own JSON event stream;
    inheriting CODEX_THREAD_ID can confuse Codex's local session recorder.
    """

    cleaned = dict(os.environ if env is None else env)
    for key in list(cleaned):
        if key.startswith("BASH_FUNC_"):
            del cleaned[key]
    for key in (
        "CODEX_CI",
        "CODEX_THREAD_ID",
        "CODEX_INTERNAL_ORIGINATOR_OVERRIDE",
        "BASH_ENV",
        "ENV",
    ):
        cleaned.pop(key, None)
    return cleaned


@dataclass(frozen=True)
class RuntimePaths:
    orfs_root: Path
    agent_root: Path
    state_root: Path

    @property
    def packet_dir(self) -> Path:
        return self.state_root / "packets"

    @property
    def checkpoints_dir(self) -> Path:
        return self.state_root / "checkpoints"

    @property
    def operations_dir(self) -> Path:
        return self.checkpoints_dir / "operations"


def resolve_runtime_paths(
    *,
    anchor_file: str | Path,
    agent_root_levels_up: int,
    script_name: str,
    cwd: Path | None = None,
) -> RuntimePaths:
    agent_root = resolve_agent_root(anchor_file, levels_up=agent_root_levels_up)
    load_local_env_sh(agent_root)
    orfs_root = resolve_orfs_root(
        agent_root=agent_root,
        script_name=script_name,
    )
    state_root = resolve_state_root(orfs_root=orfs_root, agent_root=agent_root)
    return RuntimePaths(orfs_root=orfs_root, agent_root=agent_root, state_root=state_root)
