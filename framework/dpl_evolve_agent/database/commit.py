#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from database.schema import compact_dict
from database.store import TABLES, append_record
from runtime_paths import resolve_runtime_paths


def parse_record(*, record_json: Path | None, record_inline: str | None) -> dict:
    if record_json is None and record_inline is None:
        raise SystemExit("Provide --record-json or --record.")
    if record_json is not None and record_inline is not None:
        raise SystemExit("Use only one of --record-json or --record.")
    if record_json is not None:
        return json.loads(record_json.read_text(encoding="utf-8"))
    assert record_inline is not None
    return json.loads(record_inline)


def main() -> None:
    parser = argparse.ArgumentParser(description="Append one framework database record.")
    parser.add_argument("--state-root", type=Path)
    parser.add_argument("--table", required=True, choices=sorted(TABLES - {"database_commits"}))
    parser.add_argument("--record-json", type=Path)
    parser.add_argument("--record")
    parser.add_argument("--source", default="manual")
    args = parser.parse_args()

    if args.state_root is None:
        runtime = resolve_runtime_paths(
            anchor_file=__file__,
            agent_root_levels_up=1,
            script_name="database.commit",
        )
        state_root = runtime.state_root
    else:
        state_root = args.state_root.resolve()

    record = parse_record(record_json=args.record_json, record_inline=args.record)
    commit = append_record(state_root, args.table, record, source=args.source)
    print(json.dumps(compact_dict(commit), indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
