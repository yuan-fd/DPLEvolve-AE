from __future__ import annotations

import hashlib
import json
import os
import tempfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Any

from .schema import DatabaseCommit, compact_dict

TABLES = {
    "candidate_programs",
    "build_results",
    "eval_results",
    "teacher_reviews",
    "database_commits",
}


def database_root(state_root: Path) -> Path:
    return state_root / "database"


def table_path(state_root: Path, table: str) -> Path:
    if table not in TABLES:
        allowed = ", ".join(sorted(TABLES))
        raise ValueError(f"Unknown database table {table!r}; expected one of: {allowed}")
    return database_root(state_root) / f"{table}.jsonl"


def canonical_json(record: dict[str, Any]) -> str:
    return json.dumps(record, ensure_ascii=True, sort_keys=True, separators=(",", ":"))


def record_hash(record: dict[str, Any]) -> str:
    return hashlib.sha256(canonical_json(record).encode("utf-8")).hexdigest()


def now_utc() -> str:
    return datetime.now(timezone.utc).replace(microsecond=0).isoformat()


def infer_record_id(record: dict[str, Any]) -> str | None:
    for key in ("program_id", "build_id", "eval_id", "review_id", "commit_id", "run_id"):
        value = record.get(key)
        if value:
            return str(value)
    return None


def append_jsonl(path: Path, record: dict[str, Any]) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    line = canonical_json(record) + "\n"
    fd, tmp_name = tempfile.mkstemp(prefix=f".{path.name}.", dir=str(path.parent))
    try:
        with os.fdopen(fd, "w", encoding="utf-8") as tmp:
            if path.exists():
                tmp.write(path.read_text(encoding="utf-8"))
            tmp.write(line)
        os.replace(tmp_name, path)
    finally:
        try:
            os.unlink(tmp_name)
        except FileNotFoundError:
            pass


def load_records(state_root: Path, table: str) -> list[dict[str, Any]]:
    path = table_path(state_root, table)
    if not path.exists():
        return []
    records: list[dict[str, Any]] = []
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line:
            continue
        records.append(json.loads(line))
    return records


def append_record(
    state_root: Path,
    table: str,
    record: Any,
    *,
    source: str = "manual",
) -> DatabaseCommit:
    record_dict = compact_dict(record)
    append_jsonl(table_path(state_root, table), record_dict)
    digest = record_hash(record_dict)
    commit = DatabaseCommit(
        commit_id=f"DBC_{digest[:16]}",
        created_at=now_utc(),
        table=table,
        record_id=infer_record_id(record_dict),
        record_hash=digest,
        source=source,
    )
    if table != "database_commits":
        append_jsonl(table_path(state_root, "database_commits"), compact_dict(commit))
    return commit
