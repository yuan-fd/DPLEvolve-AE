#!/usr/bin/env python3
"""Watch a Teacher/Student optimization round.

This is a lightweight observability dashboard for the case-optimization loop.
It intentionally shows observable state only: prompts, operation status,
visible final/progress messages, diffs, metrics paths, usage, and process state.
It does not expose hidden model reasoning.
"""
from __future__ import annotations

import argparse
import html
import json
import os
import re
import subprocess
import sys
import time
from dataclasses import dataclass
from http.server import BaseHTTPRequestHandler, ThreadingHTTPServer
from pathlib import Path
from typing import Any
from urllib.parse import parse_qs, quote, unquote, urlparse

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import resolve_runtime_paths
from scripts.teacher_loop.common import round_id_from_dir


@dataclass(frozen=True)
class OperationView:
    iteration: str
    kind: str
    label: str
    operation_id: str
    prompt_path: Path
    operation_dir: Path
    status: str
    returncode: int | None
    elapsed_seconds: float | None
    thread_id: str | None
    input_tokens: int
    cached_input_tokens: int
    output_tokens: int
    last_message: str
    last_event: str
    stderr_tail: str
    diff_path: Path | None
    metrics_paths: list[Path]


def runtime_paths():
    return resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="watch_teacher_round.py",
    )


def tail_text(path: Path, max_chars: int = 4000) -> str:
    if not path.exists():
        return ""
    try:
        text = path.read_text(encoding="utf-8", errors="replace")
    except Exception:
        return ""
    if len(text) <= max_chars:
        return text
    return text[-max_chars:]


def load_json(path: Path) -> dict[str, Any] | None:
    try:
        return json.loads(path.read_text(encoding="utf-8"))
    except Exception:
        return None


def process_text() -> str:
    try:
        return subprocess.check_output(
            ["pgrep", "-af", "optimize_case_with_codex|run_codex_exec|codex exec|openroad|build_openroad_variant_relink"],
            text=True,
            stderr=subprocess.DEVNULL,
        )
    except subprocess.CalledProcessError:
        return ""


def extract_metrics_paths(text: str) -> list[Path]:
    paths: list[Path] = []
    for match in re.finditer(r"(/[^`\\s]+/metrics\\.json)", text):
        path = Path(match.group(1))
        if path not in paths:
            paths.append(path)
    return paths


def summarize_events(events_path: Path) -> str:
    if not events_path.exists():
        return ""
    lines = tail_text(events_path, max_chars=20000).splitlines()[-80:]
    summaries: list[str] = []
    for raw in lines:
        try:
            event = json.loads(raw)
        except Exception:
            continue
        typ = event.get("type", "")
        if typ == "turn.completed":
            usage = event.get("usage", {})
            summaries.append(
                "turn.completed "
                f"in={usage.get('input_tokens', 0)} "
                f"cached={usage.get('cached_input_tokens', 0)} "
                f"out={usage.get('output_tokens', 0)}"
            )
        elif typ == "item.started":
            item = event.get("item", {})
            summaries.append(f"started {item.get('type', 'item')}")
        elif typ == "item.completed":
            item = event.get("item", {})
            item_type = item.get("type", "item")
            if item_type == "command_execution":
                command = str(item.get("command", ""))[:160]
                summaries.append(f"command completed: {command}")
            elif item_type == "agent_message":
                text = str(item.get("text", "")).strip().replace("\n", " ")
                summaries.append(f"message: {text[:180]}")
            else:
                summaries.append(f"completed {item_type}")
        elif typ:
            summaries.append(typ)
    return "\n".join(summaries[-8:])


def infer_operations(round_dir: Path, operations_dir: Path) -> list[tuple[str, str, str, Path]]:
    round_id = round_id_from_dir(round_dir)
    inferred: list[tuple[str, str, str, Path]] = []
    for iter_dir in sorted(round_dir.glob("iter_*")):
        if not iter_dir.is_dir():
            continue
        iteration = iter_dir.name
        prompt_dir = iter_dir / "prompts"
        teacher_plan = prompt_dir / "teacher_plan.md"
        if teacher_plan.exists():
            inferred.append((iteration, "teacher", f"{round_id}_{iteration}_teacher_plan", teacher_plan))
        for prompt in sorted(prompt_dir.glob("student_*.md")):
            inferred.append((iteration, "student", f"{round_id}_{iteration}_{prompt.stem}", prompt))
        teacher_review = prompt_dir / "teacher_review.md"
        if teacher_review.exists():
            inferred.append((iteration, "teacher", f"{round_id}_{iteration}_teacher_review", teacher_review))
    return inferred


def diff_path_for(round_dir: Path, iteration: str, operation_id: str) -> Path | None:
    match = re.search(r"_(student_\\d+)_", operation_id)
    if not match:
        return None
    student_id = match.group(1)
    path = round_dir / "students" / student_id / iteration / "artifacts" / "implementation.diff"
    return path


def operation_status(op_id: str, op_dir: Path, proc_text: str) -> tuple[str, int | None]:
    summary = load_json(op_dir / "codex_usage_summary.json")
    if summary is not None:
        rc = summary.get("returncode")
        try:
            rc_int = int(rc)
        except Exception:
            rc_int = None
        if rc_int == 0:
            return "done", rc_int
        return "failed", rc_int
    if op_id in proc_text:
        return "running", None
    if op_dir.exists():
        return "started", None
    return "pending", None


def collect(round_dir: Path) -> list[OperationView]:
    rt = runtime_paths()
    operations_dir = round_dir.parent / "checkpoints" / "operations"
    proc_text = process_text()
    views: list[OperationView] = []
    for iteration, kind, op_id, prompt_path in infer_operations(round_dir, operations_dir):
        op_dir = operations_dir / op_id
        summary = load_json(op_dir / "codex_usage_summary.json") or {}
        status, returncode = operation_status(op_id, op_dir, proc_text)
        usage = summary.get("usage", {}) if isinstance(summary.get("usage"), dict) else {}
        last_message_path = op_dir / "codex_last_message.txt"
        last_message = tail_text(last_message_path, max_chars=5000).strip()
        stderr_tail = tail_text(op_dir / "codex_stderr.log", max_chars=2400).strip()
        last_event = summarize_events(op_dir / "codex_events.jsonl")
        diff_path = diff_path_for(round_dir, iteration, op_id)
        metrics_paths = extract_metrics_paths(last_message)
        views.append(
            OperationView(
                iteration=iteration,
                kind=kind,
                label=prompt_path.stem,
                operation_id=op_id,
                prompt_path=prompt_path,
                operation_dir=op_dir,
                status=status,
                returncode=returncode,
                elapsed_seconds=summary.get("elapsed_seconds"),
                thread_id=summary.get("thread_id"),
                input_tokens=int(usage.get("input_tokens", 0) or 0),
                cached_input_tokens=int(usage.get("cached_input_tokens", 0) or 0),
                output_tokens=int(usage.get("output_tokens", 0) or 0),
                last_message=last_message,
                last_event=last_event,
                stderr_tail=stderr_tail,
                diff_path=diff_path if diff_path and diff_path.exists() else diff_path,
                metrics_paths=metrics_paths,
            )
        )
    return views


def status_icon(status: str) -> str:
    return {
        "done": "OK",
        "failed": "FAIL",
        "running": "RUN",
        "started": "START",
        "pending": "WAIT",
    }.get(status, status.upper())


def print_table(round_dir: Path) -> None:
    views = collect(round_dir)
    print(f"round: {round_dir}")
    print("Note: dashboard shows observable messages/artifacts, not hidden model reasoning.")
    header = f"{'iter':<8} {'status':<7} {'label':<38} {'elapsed':>9} {'out_tok':>8}  operation"
    print(header)
    print("-" * len(header))
    for view in views:
        elapsed = "" if view.elapsed_seconds is None else f"{view.elapsed_seconds:.1f}s"
        print(
            f"{view.iteration:<8} {status_icon(view.status):<7} "
            f"{view.label[:38]:<38} {elapsed:>9} {view.output_tokens:>8}  "
            f"{view.operation_id}"
        )
        if view.status in {"running", "failed", "done"}:
            snippet = (view.last_message or view.last_event or view.stderr_tail).strip()
            if snippet:
                print(" " * 10 + snippet.replace("\n", " ")[:220])


def href_for(path: Path) -> str:
    return f"/file?path={quote(str(path))}"


def render_html(round_dir: Path, refresh: int) -> str:
    views = collect(round_dir)
    cards = []
    for view in views:
        status_class = html.escape(view.status)
        paths = [
            f'<a href="{href_for(view.prompt_path)}">prompt</a>',
            f'<a href="{href_for(view.operation_dir)}">operation_dir</a>',
        ]
        if view.diff_path:
            paths.append(f'<a href="{href_for(view.diff_path)}">implementation.diff</a>')
        for metrics in view.metrics_paths[:3]:
            paths.append(f'<a href="{href_for(metrics)}">metrics.json</a>')
        message = html.escape(view.last_message or view.last_event or view.stderr_tail or "")
        cards.append(
            f"""
            <section class="card {status_class}">
              <h2>{html.escape(view.iteration)} / {html.escape(view.label)}
                <span>{html.escape(status_icon(view.status))}</span></h2>
              <p><b>operation:</b> {html.escape(view.operation_id)}</p>
              <p><b>elapsed:</b> {html.escape(str(view.elapsed_seconds or ""))}
                 <b>returncode:</b> {html.escape(str(view.returncode))}
                 <b>thread:</b> {html.escape(str(view.thread_id or ""))}</p>
              <p><b>tokens:</b> in {view.input_tokens}, cached {view.cached_input_tokens}, out {view.output_tokens}</p>
              <p>{' | '.join(paths)}</p>
              <details open><summary>visible latest output</summary><pre>{message}</pre></details>
            </section>
            """
        )
    return f"""<!doctype html>
<html>
<head>
  <meta charset="utf-8">
  <meta http-equiv="refresh" content="{refresh}">
  <title>DPL Evolve Round Watch</title>
  <style>
    body {{ font-family: ui-sans-serif, system-ui, sans-serif; margin: 24px; background: #f6f3ec; color: #1d2528; }}
    header {{ margin-bottom: 18px; }}
    .grid {{ display: grid; grid-template-columns: repeat(auto-fit, minmax(460px, 1fr)); gap: 14px; }}
    .card {{ border: 1px solid #d4c8b7; border-left-width: 8px; border-radius: 12px; padding: 14px; background: #fffdf8; box-shadow: 0 1px 5px rgba(0,0,0,.05); }}
    .running {{ border-left-color: #d8891f; }}
    .done {{ border-left-color: #227c55; }}
    .failed {{ border-left-color: #b3261e; }}
    .pending, .started {{ border-left-color: #6f7782; }}
    h1 {{ margin: 0 0 6px; }}
    h2 {{ display: flex; justify-content: space-between; gap: 12px; margin: 0 0 8px; font-size: 18px; }}
    pre {{ white-space: pre-wrap; max-height: 360px; overflow: auto; background: #172026; color: #f3f4e8; padding: 10px; border-radius: 8px; }}
    a {{ color: #155a8a; }}
    .note {{ color: #58636b; }}
  </style>
</head>
<body>
  <header>
    <h1>DPL Evolve Round Watch</h1>
    <div><b>round:</b> {html.escape(str(round_dir))}</div>
    <div class="note">Shows observable status, artifacts, logs, and visible messages. Hidden model reasoning is not available.</div>
    <div class="note">Auto-refresh: {refresh}s. Generated at {html.escape(time.strftime('%Y-%m-%d %H:%M:%S'))}.</div>
  </header>
  <main class="grid">
    {''.join(cards)}
  </main>
</body>
</html>"""


class DashboardHandler(BaseHTTPRequestHandler):
    round_dir: Path
    refresh: int

    def do_GET(self) -> None:  # noqa: N802
        parsed = urlparse(self.path)
        if parsed.path == "/file":
            qs = parse_qs(parsed.query)
            raw_path = qs.get("path", [""])[0]
            path = Path(unquote(raw_path))
            if path.is_dir():
                body = "\n".join(sorted(item.name for item in path.iterdir()))
            else:
                body = tail_text(path, max_chars=120000)
            payload = f"<pre>{html.escape(body)}</pre>".encode("utf-8")
            self.send_response(200)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(payload)))
            self.end_headers()
            self.wfile.write(payload)
            return
        payload = render_html(self.round_dir, self.refresh).encode("utf-8")
        self.send_response(200)
        self.send_header("Content-Type", "text/html; charset=utf-8")
        self.send_header("Content-Length", str(len(payload)))
        self.end_headers()
        self.wfile.write(payload)

    def log_message(self, fmt: str, *args: Any) -> None:
        return


def resolve_round_dir(value: str) -> Path:
    path = Path(value)
    if path.exists():
        return path.resolve()
    rt = runtime_paths()
    return (rt.state_root / value / "teacher_rounds").resolve()


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description="Watch a DPL Teacher/Student optimization round.")
    parser.add_argument("round", help="Round id or round directory.")
    parser.add_argument("--serve", action="store_true", help="Serve a local HTML dashboard.")
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8765)
    parser.add_argument("--refresh", type=int, default=5)
    parser.add_argument("--watch", action="store_true", help="Continuously print the terminal table.")
    args = parser.parse_args(argv)

    round_dir = resolve_round_dir(args.round)
    if not round_dir.exists():
        raise SystemExit(f"Round directory does not exist: {round_dir}")

    if args.serve:
        DashboardHandler.round_dir = round_dir
        DashboardHandler.refresh = max(1, args.refresh)
        server = ThreadingHTTPServer((args.host, args.port), DashboardHandler)
        print(f"dashboard: http://{args.host}:{args.port}")
        print(f"round_dir: {round_dir}")
        server.serve_forever()

    if args.watch:
        while True:
            os.system("clear")
            print_table(round_dir)
            time.sleep(max(1, args.refresh))

    print_table(round_dir)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
