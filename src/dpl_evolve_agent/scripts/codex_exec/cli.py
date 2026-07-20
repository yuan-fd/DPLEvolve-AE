from __future__ import annotations

import argparse
import datetime as dt
from pathlib import Path


def default_operation_id() -> str:
    return f"codex_exec_{dt.datetime.now().strftime('%Y%m%d_%H%M%S')}"


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Run one non-interactive Codex operation and record artifacts."
    )
    parser.add_argument("--operation-id", default=default_operation_id())
    parser.add_argument(
        "--operations-dir",
        type=Path,
        help=(
            "Directory that stores this operation's recorded artifacts. "
            "Defaults to the runtime checkpoints/operations directory."
        ),
    )
    parser.add_argument("--prompt")
    parser.add_argument("--prompt-file", type=Path)
    parser.add_argument("--model")
    parser.add_argument("--profile")
    parser.add_argument(
        "--sandbox",
        default="danger-full-access",
        help=(
            "Sandbox mode passed to codex exec. Use danger-full-access by "
            "default for this local experiment harness; workspace-write uses "
            "bubblewrap and can fail before shell startup on older bwrap builds."
        ),
    )
    parser.add_argument("--skip-git-repo-check", action="store_true")
    parser.add_argument("--color", default="never")
    parser.add_argument(
        "--cwd",
        type=Path,
        help=(
            "Working directory passed to `codex exec -C` and to the wrapper "
            "process. Defaults to the ORFS root."
        ),
    )
    parser.add_argument("--config", action="append", default=[])
    parser.add_argument("--add-dir", action="append", default=[])
    parser.add_argument(
        "--resume-session-id",
        help="Resume an existing Codex exec session/thread instead of starting new.",
    )
    parser.add_argument(
        "--resume-last",
        action="store_true",
        help="Resume the most recent Codex exec session. Mostly for manual debugging.",
    )
    parser.add_argument(
        "--session-identity",
        help="Stable human-readable identity for this Teacher/Student worker.",
    )
    parser.add_argument(
        "--session-state",
        type=Path,
        help=(
            "JSON state file storing the worker identity and last thread id. "
            "If present, a later run auto-resumes the recorded thread."
        ),
    )
    parser.add_argument(
        "--session-env-file",
        type=Path,
        help="Shell env file recording the identity and last thread id for this workspace.",
    )
    parser.add_argument(
        "--no-session-auto-resume",
        action="store_true",
        help="Do not auto-resume from --session-state even if it has a thread id.",
    )
    parser.add_argument(
        "--auto-resume-max-attempts",
        type=int,
        default=8,
        help="Maximum wrapper-level retries for transient Codex/API failures.",
    )
    parser.add_argument(
        "--auto-resume-backoff-seconds",
        type=float,
        default=45.0,
        help="Base delay before retrying a transient Codex/API failure.",
    )
    parser.add_argument("--input-price-per-1m", type=float)
    parser.add_argument("--cached-input-price-per-1m", type=float)
    parser.add_argument("--output-price-per-1m", type=float)
    return parser.parse_args(argv)
