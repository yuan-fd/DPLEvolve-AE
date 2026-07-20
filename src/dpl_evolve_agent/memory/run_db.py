"""JSONL-backed run evidence helpers.

The first version is deliberately small: it gives the control plane a stable
place to read/write structured run facts without introducing a database service.
"""
from __future__ import annotations

import json
from pathlib import Path
from typing import Any, Iterable


def memory_root(state_root: Path) -> Path:
    return state_root / "memory"


def run_db_path(state_root: Path) -> Path:
    return memory_root(state_root) / "run_db.jsonl"


def load_jsonl(path: Path) -> list[dict[str, Any]]:
    if not path.exists():
        return []
    records: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        try:
            item = json.loads(line)
        except json.JSONDecodeError:
            continue
        if isinstance(item, dict):
            records.append(item)
    return records


def append_jsonl(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def recent_runs(
    state_root: Path,
    *,
    problem: str | None = None,
    family: str | None = None,
    limit: int = 20,
) -> list[dict[str, Any]]:
    records = load_jsonl(run_db_path(state_root))
    if problem is not None:
        records = [item for item in records if item.get("problem") == problem]
    if family is not None:
        records = [
            item
            for item in records
            if item.get("patch_family") == family or item.get("family") == family
        ]
    return records[-limit:]


def summarize_recent_failures(records: Iterable[dict[str, Any]], *, limit: int = 5) -> list[str]:
    failures: list[str] = []
    for item in records:
        status = str(item.get("status", "")).lower()
        improved = item.get("improved")
        if status in {"ok", "accepted"} and improved is not False:
            continue
        signature = item.get("failure_signature") or item.get("reject_reason")
        if not signature:
            signature = item.get("summary") or item.get("result")
        if signature:
            failures.append(str(signature))
    return failures[-limit:]

