from __future__ import annotations

import datetime as dt
import json
import os
import shutil
import subprocess
import sys
import time
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import clean_subprocess_env, resolve_runtime_paths
from scripts.codex_exec.artifacts import (
    maybe_cost_estimate,
    summarize_events,
    summarize_stderr,
    write_readme,
)
from scripts.codex_exec.cli import parse_args


def runtime_paths() -> Any:
    return resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="run_codex_exec.py",
    )


def default_exec_root(runtime: Any) -> Path:
    return runtime.orfs_root


def load_prompt(args: Any) -> str:
    if args.prompt_file is not None:
        return args.prompt_file.read_text(encoding="utf-8")
    if args.prompt is not None:
        return args.prompt
    if not sys.stdin.isatty():
        return sys.stdin.read()
    raise SystemExit("Provide --prompt, --prompt-file, or pipe prompt text on stdin.")


def operation_can_be_retried(op_dir: Path) -> bool:
    """Return true for pre-agent startup failures that are safe to archive.

    Once Codex has produced a thread id or agent message, reusing the same
    operation id would mix real experiment evidence.  Startup failures before a
    thread exists are different: they are usually environment/session-store
    problems and should not make the whole round unrestartable.
    """
    summary = load_json_file(op_dir / "codex_usage_summary.json")
    if not summary:
        return False
    if int(summary.get("returncode", 0) or 0) == 0:
        return False
    if summary.get("thread_id"):
        return False
    if int(summary.get("agent_message_count", 0) or 0) > 0:
        return False
    return True


def archive_retryable_operation_dir(op_dir: Path) -> Path:
    stamp = dt.datetime.now().strftime("%Y%m%d_%H%M%S")
    archive = op_dir.with_name(f"{op_dir.name}.failed_{stamp}")
    suffix = 1
    while archive.exists():
        suffix += 1
        archive = op_dir.with_name(f"{op_dir.name}.failed_{stamp}_{suffix}")
    shutil.move(str(op_dir), str(archive))
    return archive


def ensure_operation_dir(operations_dir: Path, operation_id: str) -> Path:
    op_dir = operations_dir / operation_id
    if op_dir.exists():
        if operation_can_be_retried(op_dir):
            archive_retryable_operation_dir(op_dir)
            op_dir.mkdir(parents=True, exist_ok=False)
            return op_dir
        raise SystemExit(
            f"Operation directory already exists for '{operation_id}': {op_dir}\n"
            "Use a fresh operation id to avoid mixing old and new Codex artifacts."
        )
    op_dir.mkdir(parents=True, exist_ok=False)
    return op_dir


def load_json_file(path: Path | None) -> dict[str, Any]:
    if path is None or not path.is_file():
        return {}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except (json.JSONDecodeError, OSError):
        return {}
    return data if isinstance(data, dict) else {}


def write_json_file(path: Path, data: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n", encoding="utf-8")


def path_is_writable_dir(path: Path) -> bool:
    try:
        path.mkdir(parents=True, exist_ok=True)
        probe = path / ".dpl_evolve_write_probe"
        probe.write_text("ok\n", encoding="utf-8")
        probe.unlink(missing_ok=True)
        return True
    except OSError:
        return False


def default_codex_home(runtime: Any, session_state: Path | None) -> tuple[Path, str]:
    state = load_json_file(session_state)
    recorded_home = str(state.get("session_store_home") or "").strip()
    explicit = os.environ.get("CODEX_HOME")
    home = Path(explicit).expanduser().resolve() if explicit else (Path.home() / ".codex").resolve()

    if recorded_home:
        recorded_path = Path(recorded_home).expanduser().resolve()
        if recorded_path == home and path_is_writable_dir(recorded_path):
            return recorded_path, "session_state"

    if path_is_writable_dir(home):
        return home, "env" if explicit else "default_home"

    raise SystemExit(
        "Codex session store is not writable. Fix CODEX_HOME or ~/.codex before "
        f"launching agents: {home}"
    )


def session_exists(codex_home: Path | None, thread_id: str | None) -> bool:
    if not codex_home or not thread_id:
        return False
    index = codex_home / "session_index.jsonl"
    if index.is_file() and thread_id in index.read_text(encoding="utf-8", errors="ignore"):
        return True
    for root_name in ("sessions", "archived_sessions"):
        root = codex_home / root_name
        if not root.exists():
            continue
        for path in root.rglob("*"):
            if thread_id in path.name:
                return True
    return False


def state_resume_thread_id(args: Any, codex_home: Path | None) -> tuple[str | None, str]:
    if args.no_session_auto_resume:
        return None, "disabled"
    if args.resume_session_id or args.resume_last:
        return None, "explicit resume option"
    state = load_json_file(args.session_state)
    thread_id = str(state.get("last_thread_id") or "").strip()
    if not thread_id:
        return None, "missing last_thread_id"
    if codex_home and not session_exists(codex_home, thread_id):
        return None, f"thread not found in session store: {thread_id}"
    return thread_id, "session_state"


def quote_env_value(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def write_session_env_file(
    *,
    path: Path | None,
    identity: str | None,
    session_store_home: Path | None,
    session_state: Path | None,
    last_thread_id: str | None,
) -> None:
    if path is None:
        return
    path.parent.mkdir(parents=True, exist_ok=True)
    lines = [
        "# Generated by scripts/run_codex_exec.py. Source this for manual resume/debug.",
    ]
    if identity:
        lines.append(f"export CODEX_SESSION_IDENTITY={quote_env_value(identity)}")
    if session_store_home:
        lines.append(
            f"export CODEX_SESSION_STORE_HOME={quote_env_value(str(session_store_home))}"
        )
    if session_state:
        lines.append(f"export CODEX_SESSION_STATE={quote_env_value(str(session_state))}")
    if last_thread_id:
        lines.append(f"export CODEX_LAST_THREAD_ID={quote_env_value(last_thread_id)}")
    path.write_text("\n".join(lines) + "\n", encoding="utf-8")


def build_command(
    args: Any,
    root: Path,
    last_message_path: Path,
    effective_resume_session_id: str | None,
) -> list[str]:
    # Teacher/Student prompts are generated as self-contained packets. Disable
    # Codex memory injection so child workers use curated repo-local knowledge,
    # not unrelated global rollout memories.
    memory_disable_args = ["--disable", "memories"]
    if effective_resume_session_id or args.resume_session_id or args.resume_last:
        cmd = [
            "codex",
            "exec",
            "resume",
            "--json",
            "-o",
            str(last_message_path),
            *memory_disable_args,
        ]
        # `codex exec resume` does not expose `--sandbox`; without this, a
        # resumed worker falls back to the user config and can hit the local
        # bubblewrap `--perms` incompatibility even when the first turn used
        # `--sandbox danger-full-access`.
        if args.sandbox == "danger-full-access":
            cmd.append("--dangerously-bypass-approvals-and-sandbox")
        if args.model:
            cmd.extend(["--model", args.model])
        if args.skip_git_repo_check:
            cmd.append("--skip-git-repo-check")
        for cfg in args.config:
            cmd.extend(["-c", cfg])
        if args.resume_last:
            cmd.append("--last")
        else:
            cmd.append(effective_resume_session_id or args.resume_session_id)
        cmd.append("-")
        return cmd

    cmd = [
        "codex",
        "exec",
        "--json",
        "-C",
        str(root),
        "-o",
        str(last_message_path),
        *memory_disable_args,
    ]
    if args.model:
        cmd.extend(["--model", args.model])
    if args.profile:
        cmd.extend(["--profile", args.profile])
    if args.sandbox:
        cmd.extend(["--sandbox", args.sandbox])
    if args.skip_git_repo_check:
        cmd.append("--skip-git-repo-check")
    if args.color:
        cmd.extend(["--color", args.color])
    for cfg in args.config:
        cmd.extend(["-c", cfg])
    for directory in args.add_dir:
        cmd.extend(["--add-dir", directory])
    cmd.append("-")
    return cmd


def should_auto_resume(summary: dict[str, Any], returncode: int) -> tuple[bool, str]:
    if returncode == 0:
        return False, "clean_exit"
    thread_id = str(summary.get("thread_id") or "").strip()
    if not thread_id:
        return False, "missing_thread_id"
    last_error = str(summary.get("last_error_message") or "").lower()
    last_event = str(summary.get("last_event_type") or "")
    capacity_needles = (
        "selected model is at capacity",
        "model is at capacity",
        "rate limit",
        "rate_limit",
        "too many requests",
        "429",
        "currently experiencing high demand",
        "temporary errors",
        "402 payment required",
        "unexpected status 402",
        "internal server error",
        "502 bad gateway",
        "bad gateway",
        "503 service unavailable",
        "504 gateway timeout",
    )
    if any(needle in last_error for needle in capacity_needles):
        return True, "transient_capacity_or_rate_limit"
    if int(summary.get("agent_message_count", 0) or 0) <= 0:
        return False, "missing_agent_messages"
    transient_needles = (
        "stream disconnected before completion",
        "stream closed before response.completed",
        "stream closed before completion",
        "reconnecting",
    )
    if any(needle in last_error for needle in transient_needles):
        return True, "transient_stream_disconnect"
    if last_event == "turn.failed" and "stream" in last_error:
        return True, "turn_failed_stream_disconnect"
    return False, "non_transient_failure"


def run_codex_process(
    *,
    command: list[str],
    root: Path,
    env: dict[str, str],
    prompt_text: str,
    events_path: Path,
    stderr_path: Path,
) -> int:
    with events_path.open("a", encoding="utf-8") as events_fh, stderr_path.open(
        "a", encoding="utf-8"
    ) as stderr_fh:
        proc = subprocess.Popen(
            command,
            cwd=str(root),
            env=env,
            stdin=subprocess.PIPE,
            stdout=subprocess.PIPE,
            stderr=stderr_fh,
            text=True,
            encoding="utf-8",
        )
        assert proc.stdin is not None
        assert proc.stdout is not None
        proc.stdin.write(prompt_text)
        proc.stdin.close()

        for line in proc.stdout:
            events_fh.write(line)
            events_fh.flush()

        return proc.wait()


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    runtime = runtime_paths()
    root = args.cwd.resolve() if args.cwd else default_exec_root(runtime)
    operations_dir = (
        args.operations_dir.resolve()
        if args.operations_dir is not None
        else runtime.operations_dir
    )
    op_dir = ensure_operation_dir(operations_dir, args.operation_id)
    prompt_text = load_prompt(args)
    session_store_home, session_store_reason = default_codex_home(
        runtime, args.session_state
    )
    auto_resume_session_id, auto_resume_reason = state_resume_thread_id(
        args, session_store_home
    )

    prompt_path = op_dir / "codex_prompt.md"
    events_path = op_dir / "codex_events.jsonl"
    stderr_path = op_dir / "codex_stderr.log"
    last_message_path = op_dir / "codex_last_message.txt"
    summary_path = op_dir / "codex_usage_summary.json"
    invocation_path = op_dir / "codex_invocation.json"
    readme_path = op_dir / "README.md"

    prompt_path.write_text(prompt_text, encoding="utf-8")
    command = build_command(args, root, last_message_path, auto_resume_session_id)

    started_at = dt.datetime.now().isoformat()
    start_time = time.perf_counter()
    invocation = {
        "operation_id": args.operation_id,
        "repo_root": str(root),
        "codex_cwd": str(root),
        "agent_root": str(runtime.agent_root),
        "state_root": str(runtime.state_root),
        "operations_dir": str(operations_dir),
        "command": command,
        "model": args.model,
        "profile": args.profile,
        "sandbox": args.sandbox,
        "resume_session_id": args.resume_session_id,
        "auto_resume_session_id": auto_resume_session_id,
        "auto_resume_reason": auto_resume_reason,
        "resume_last": args.resume_last,
        "session_identity": args.session_identity,
        "session_state": None if args.session_state is None else str(args.session_state),
        "session_env_file": None
        if args.session_env_file is None
        else str(args.session_env_file),
        "session_store_home": str(session_store_home),
        "session_store_reason": session_store_reason,
        "skip_git_repo_check": args.skip_git_repo_check,
        "config": args.config,
        "add_dir": args.add_dir,
        "started_at": started_at,
    }
    invocation_path.write_text(json.dumps(invocation, indent=2) + "\n", encoding="utf-8")
    write_session_env_file(
        path=args.session_env_file,
        identity=args.session_identity,
        session_store_home=session_store_home,
        session_state=args.session_state,
        last_thread_id=auto_resume_session_id,
    )

    events_path.write_text("", encoding="utf-8")
    stderr_path.write_text("", encoding="utf-8")
    env = clean_subprocess_env()
    env["CODEX_HOME"] = str(session_store_home)
    returncode = run_codex_process(
        command=command,
        root=root,
        env=env,
        prompt_text=prompt_text,
        events_path=events_path,
        stderr_path=stderr_path,
    )

    finished_at = dt.datetime.now().isoformat()
    elapsed_seconds = time.perf_counter() - start_time
    summary = summarize_events(events_path)
    auto_resumed = False
    auto_resume_attempts = 0
    auto_resume_reasons: list[str] = []
    max_attempts = max(0, int(args.auto_resume_max_attempts or 0))
    for _ in range(max_attempts):
        resume_ok, resume_reason = should_auto_resume(summary, returncode)
        if not resume_ok:
            auto_resume_reasons.append(resume_reason)
            break
        auto_resumed = True
        auto_resume_attempts += 1
        auto_resume_reasons.append(resume_reason)
        resume_thread_id = str(summary.get("thread_id") or "").strip()
        if not resume_thread_id:
            break
        delay = max(0.0, float(args.auto_resume_backoff_seconds or 0.0))
        if delay:
            # Spread repeated proxy-capacity retries instead of immediately
            # re-entering the same failing backend window.
            time.sleep(delay * auto_resume_attempts)
        command = build_command(args, root, last_message_path, resume_thread_id)
        returncode = run_codex_process(
            command=command,
            root=root,
            env=env,
            prompt_text=prompt_text,
            events_path=events_path,
            stderr_path=stderr_path,
        )
        finished_at = dt.datetime.now().isoformat()
        elapsed_seconds = time.perf_counter() - start_time
        summary = summarize_events(events_path)
    summary["stderr_summary"] = summarize_stderr(stderr_path)
    usage = summary["usage"]
    summary["operation_id"] = args.operation_id
    summary["returncode"] = returncode
    summary["auto_resumed"] = auto_resumed
    summary["auto_resume_attempts"] = auto_resume_attempts
    summary["auto_resume_reasons"] = auto_resume_reasons
    summary["session_identity"] = args.session_identity
    summary["session_state"] = None if args.session_state is None else str(args.session_state)
    summary["session_env_file"] = None
    if args.session_env_file is not None:
        summary["session_env_file"] = str(args.session_env_file)
    summary["session_store_home"] = str(session_store_home)
    summary["session_store_reason"] = session_store_reason
    summary["auto_resume_session_id"] = auto_resume_session_id
    summary["auto_resume_reason"] = auto_resume_reason
    summary["local_session_recorded"] = session_exists(
        session_store_home, summary.get("thread_id")
    )
    summary["started_at"] = started_at
    summary["finished_at"] = finished_at
    summary["elapsed_seconds"] = round(elapsed_seconds, 3)
    summary["paths"] = {
        "prompt": str(prompt_path),
        "events": str(events_path),
        "stderr": str(stderr_path),
        "last_message": str(last_message_path),
        "invocation": str(invocation_path),
    }
    summary["cost_estimate"] = maybe_cost_estimate(
        input_tokens=usage["input_tokens"],
        cached_input_tokens=usage["cached_input_tokens"],
        output_tokens=usage["output_tokens"],
        input_price_per_1m=args.input_price_per_1m,
        cached_input_price_per_1m=args.cached_input_price_per_1m,
        output_price_per_1m=args.output_price_per_1m,
    )
    if (
        (not last_message_path.exists() or last_message_path.stat().st_size == 0)
        and str(summary.get("last_agent_message") or "").strip()
    ):
        # `codex exec -o` only writes a final answer on a clean turn completion.
        # Preserve the last streamed agent message so Teacher review still has
        # useful context after transport failures late in a long student turn.
        last_message_path.write_text(
            str(summary["last_agent_message"]).rstrip() + "\n",
            encoding="utf-8",
        )
    summary_path.write_text(json.dumps(summary, indent=2) + "\n", encoding="utf-8")
    if args.session_state is not None:
        previous_state = load_json_file(args.session_state)
        history = list(previous_state.get("history") or [])
        if len(history) > 50:
            history = history[-50:]
        history.append(
            {
                "operation_id": args.operation_id,
                "thread_id": summary.get("thread_id"),
                "returncode": returncode,
                "started_at": started_at,
                "finished_at": finished_at,
                "auto_resume_session_id": auto_resume_session_id,
                "auto_resume_reason": auto_resume_reason,
                "local_session_recorded": summary["local_session_recorded"],
            }
        )
        write_json_file(
            args.session_state,
            {
                "identity": args.session_identity,
                "session_store_home": str(session_store_home),
                "session_store_reason": session_store_reason,
                "session_env_file": None
                if args.session_env_file is None
                else str(args.session_env_file),
                "last_operation_id": args.operation_id,
                "last_thread_id": summary.get("thread_id"),
                "last_returncode": returncode,
                "last_updated_at": finished_at,
                "last_local_session_recorded": summary["local_session_recorded"],
                "history": history,
            },
        )
    write_session_env_file(
        path=args.session_env_file,
        identity=args.session_identity,
        session_store_home=session_store_home,
        session_state=args.session_state,
        last_thread_id=summary.get("thread_id"),
    )

    write_readme(
        readme_path,
        operation_id=args.operation_id,
        command=command,
        prompt_path=prompt_path,
        events_path=events_path,
        stderr_path=stderr_path,
        last_message_path=last_message_path,
        summary=summary,
    )
    print(json.dumps(summary, indent=2))
    return returncode
