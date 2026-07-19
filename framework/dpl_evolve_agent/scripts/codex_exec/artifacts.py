from __future__ import annotations

import json
import subprocess
from pathlib import Path
from typing import Any


def maybe_cost_estimate(
    *,
    input_tokens: int,
    cached_input_tokens: int,
    output_tokens: int,
    input_price_per_1m: float | None,
    cached_input_price_per_1m: float | None,
    output_price_per_1m: float | None,
) -> dict[str, Any] | None:
    if (
        input_price_per_1m is None
        or cached_input_price_per_1m is None
        or output_price_per_1m is None
    ):
        return None

    fresh_input_tokens = max(0, input_tokens - cached_input_tokens)
    estimated_cost_usd = (
        (fresh_input_tokens / 1_000_000.0) * input_price_per_1m
        + (cached_input_tokens / 1_000_000.0) * cached_input_price_per_1m
        + (output_tokens / 1_000_000.0) * output_price_per_1m
    )
    return {
        "fresh_input_tokens": fresh_input_tokens,
        "input_price_per_1m": input_price_per_1m,
        "cached_input_price_per_1m": cached_input_price_per_1m,
        "output_price_per_1m": output_price_per_1m,
        "estimated_cost_usd": estimated_cost_usd,
    }


def summarize_events(events_path: Path) -> dict[str, Any]:
    usage_totals = {
        "input_tokens": 0,
        "cached_input_tokens": 0,
        "output_tokens": 0,
    }
    thread_id = None
    turn_count = 0
    turn_failed_count = 0
    agent_message_count = 0
    last_agent_message = None
    last_error_message = None
    last_event_type = None

    with events_path.open("r", encoding="utf-8") as fh:
        for raw_line in fh:
            line = raw_line.strip()
            if not line.startswith("{"):
                continue
            try:
                event = json.loads(line)
            except json.JSONDecodeError:
                continue

            last_event_type = event.get("type", last_event_type)
            if event.get("type") == "thread.started":
                thread_id = event.get("thread_id", thread_id)

            if event.get("type") == "turn.completed":
                turn_count += 1
                usage = event.get("usage", {})
                usage_totals["input_tokens"] += int(usage.get("input_tokens", 0) or 0)
                usage_totals["cached_input_tokens"] += int(
                    usage.get("cached_input_tokens", 0) or 0
                )
                usage_totals["output_tokens"] += int(usage.get("output_tokens", 0) or 0)

            if event.get("type") == "turn.failed":
                turn_failed_count += 1
                error = event.get("error", {})
                if isinstance(error, dict):
                    last_error_message = error.get("message", last_error_message)

            if event.get("type") == "error":
                last_error_message = event.get("message", last_error_message)

            if event.get("type") == "item.completed":
                item = event.get("item", {})
                if item.get("type") == "agent_message":
                    agent_message_count += 1
                    last_agent_message = item.get("text", last_agent_message)

    return {
        "thread_id": thread_id,
        "turn_count": turn_count,
        "turn_failed_count": turn_failed_count,
        "agent_message_count": agent_message_count,
        "last_agent_message": last_agent_message,
        "last_error_message": last_error_message,
        "last_event_type": last_event_type,
        "usage": usage_totals,
    }


def summarize_stderr(stderr_path: Path) -> dict[str, Any]:
    if not stderr_path.exists():
        return {"line_count": 0, "categories": {}, "samples": []}
    text = stderr_path.read_text(encoding="utf-8", errors="replace")
    categories = {
        "analytics_noise": ["codex_analytics::client", "analytics-events", "403 Forbidden"],
        "file_watcher_noise": ["file_watcher", "failed to unwatch"],
        "rollout_recording_warning": ["failed to record rollout items"],
        "tool_router_error": [
            "codex_core::tools::router",
            "exec_command failed",
            "write_stdin failed",
        ],
        "traceback": ["Traceback"],
    }
    counts = {name: 0 for name in categories}
    samples: list[str] = []
    for line in text.splitlines():
        lowered = line.lower()
        matched = False
        for name, needles in categories.items():
            if any(needle.lower() in lowered for needle in needles):
                counts[name] += 1
                matched = True
        if matched and len(samples) < 12:
            samples.append(line[:500])
    return {
        "line_count": len(text.splitlines()),
        "categories": {name: count for name, count in counts.items() if count},
        "samples": samples,
    }


def write_readme(
    readme_path: Path,
    *,
    operation_id: str,
    command: list[str],
    prompt_path: Path,
    events_path: Path,
    stderr_path: Path,
    last_message_path: Path,
    summary: dict[str, Any],
) -> None:
    usage = summary["usage"]
    stderr_summary = summary.get("stderr_summary", {})
    lines = [
        f"# {operation_id}",
        "",
        "Codex non-interactive invocation record.",
        "",
        f"- thread_id: {summary.get('thread_id')}",
        f"- turns: {summary.get('turn_count')}",
        f"- agent_messages: {summary.get('agent_message_count')}",
        f"- input_tokens: {usage['input_tokens']}",
        f"- cached_input_tokens: {usage['cached_input_tokens']}",
        f"- output_tokens: {usage['output_tokens']}",
        f"- stderr_categories: {stderr_summary.get('categories', {})}",
        "",
        "## Files",
        f"- prompt: `{prompt_path}`",
        f"- events: `{events_path}`",
        f"- stderr: `{stderr_path}`",
        f"- last_message: `{last_message_path}`",
        "",
        "## Command",
        "```bash",
        " ".join(subprocess.list2cmdline([part]) for part in command),
        "```",
    ]
    readme_path.write_text("\n".join(lines) + "\n", encoding="utf-8")
