#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
from pathlib import Path
from typing import Any

from flow_paths import resolve_flow_path


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description=(
            "Normalize placement-only report_metrics JSON into a stable metadata.json "
            "for dpl_evolve baselines."
        )
    )
    parser.add_argument("--orfs-metrics", required=True)
    parser.add_argument("--output", required=True)
    return parser.parse_args()


def normalize_key(key: str) -> str:
    return key.replace(":", "__").replace("/", "__")


def main() -> None:
    args = parse_args()

    metrics_path = resolve_flow_path(args.orfs_metrics)
    output = resolve_flow_path(args.output)

    with metrics_path.open("r", encoding="utf-8") as fh:
        stage_metrics: dict[str, Any] = json.load(fh)

    normalized = {
        normalize_key(key): value for key, value in sorted(stage_metrics.items())
    }

    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(
        json.dumps(normalized, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )


if __name__ == "__main__":
    main()
