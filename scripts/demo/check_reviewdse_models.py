#!/usr/bin/env python3
"""Probe the exact ReviewDSE models before starting an expensive round."""
from __future__ import annotations

import argparse
import json
import shutil
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Any


def public_error(value: Any) -> str:
    """Return a compact public error from a Codex JSON event value."""
    if isinstance(value, dict):
        for key in ("detail", "message", "error"):
            if key in value:
                return public_error(value[key])
        return ""
    text = str(value or "").strip()
    for _ in range(2):
        if not (text.startswith("{") and text.endswith("}")):
            break
        try:
            decoded = json.loads(text)
        except json.JSONDecodeError:
            break
        nested = public_error(decoded)
        if not nested or nested == text:
            break
        text = nested
    return " ".join(text.split())[:1000]


def event_error(stdout: str, stderr: str) -> str:
    for raw in stdout.splitlines():
        try:
            event = json.loads(raw)
        except json.JSONDecodeError:
            continue
        if not isinstance(event, dict):
            continue
        if event.get("type") == "error":
            message = public_error(event.get("message"))
            if message:
                return message
        if event.get("type") == "turn.failed":
            message = public_error(event.get("error"))
            if message:
                return message
    return public_error(stderr) or "Codex model probe exited without a public error message"


def probe(*, role: str, model: str, effort: str, cwd: Path) -> tuple[bool, str]:
    codex = shutil.which("codex")
    if codex is None:
        return False, "codex executable was not found in PATH"
    with tempfile.TemporaryDirectory(prefix="dplevolve-model-probe-") as directory:
        last_message = Path(directory) / "last_message.txt"
        command = [
            codex,
            "exec",
            "--json",
            "-C",
            str(cwd),
            "-o",
            str(last_message),
            "--disable",
            "memories",
            "--model",
            model,
            "--sandbox",
            "read-only",
            "--skip-git-repo-check",
            "--color",
            "never",
            "-c",
            f'model_reasoning_effort="{effort}"',
            "-",
        ]
        prompt = (
            f"This is a model-availability probe for the ReviewDSE {role}. "
            "Do not use tools. Reply with exactly MODEL_READY."
        )
        try:
            result = subprocess.run(
                command,
                cwd=cwd,
                input=prompt,
                text=True,
                stdout=subprocess.PIPE,
                stderr=subprocess.PIPE,
                check=False,
                timeout=180,
            )
        except subprocess.TimeoutExpired:
            return False, "model probe timed out after 180 seconds"
        if result.returncode != 0:
            return False, event_error(result.stdout, result.stderr)
        try:
            answer = last_message.read_text(encoding="utf-8").strip()
        except OSError:
            answer = ""
        if answer != "MODEL_READY":
            return False, f"unexpected model-probe response: {answer or '(empty)'}"
        return True, "MODEL_READY"


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cwd", type=Path, required=True)
    parser.add_argument("--student-model", required=True)
    parser.add_argument("--student-effort", required=True)
    parser.add_argument("--teacher-model", required=True)
    parser.add_argument("--teacher-effort", required=True)
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    checks = (
        ("Student", args.student_model, args.student_effort),
        ("Teacher", args.teacher_model, args.teacher_effort),
    )
    print("ReviewDSE model availability preflight (small live requests)")
    for role, model, effort in checks:
        print(f"  checking {role:<7} {model} / {effort} ...", flush=True)
        ok, detail = probe(
            role=role.lower(), model=model, effort=effort, cwd=args.cwd.resolve()
        )
        if not ok:
            print(f"[FAIL] {role} model is unavailable: {detail}", file=sys.stderr)
            print(
                "[STOP] No ReviewDSE round was started; remaining probes were skipped.",
                file=sys.stderr,
            )
            return 2
        print(f"[OK]   {role} {model} / {effort}")
    print("[OK] Requested Teacher and Student models are available.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
