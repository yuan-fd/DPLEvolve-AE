#!/usr/bin/env python3
from __future__ import annotations

import argparse
import csv
import json
import math
import re
from pathlib import Path
from typing import Any


def load_json(path: Path) -> dict[str, Any]:
    if not path.is_file():
        return {}
    return json.loads(path.read_text(encoding="utf-8"))


def load_snapshot(path: Path) -> dict[str, dict[str, str]]:
    rows: dict[str, dict[str, str]] = {}
    if not path.is_file():
        return rows
    with path.open("r", encoding="utf-8") as fh:
        reader = csv.DictReader(fh, delimiter="\t")
        for row in reader:
            rows[row["inst"]] = row
    return rows


def displacement(before: dict[str, dict[str, str]], after: dict[str, dict[str, str]], dbu: float | None) -> dict[str, Any]:
    movable = 0
    moved = 0
    total = 0.0
    max_disp = 0.0
    for name, brow in before.items():
        if name not in after or brow.get("is_fixed") == "1":
            continue
        arow = after[name]
        dx = float(arow["x"]) - float(brow["x"])
        dy = float(arow["y"]) - float(brow["y"])
        disp = math.hypot(dx, dy)
        movable += 1
        total += disp
        max_disp = max(max_disp, disp)
        if dx != 0.0 or dy != 0.0:
            moved += 1
    avg = total / movable if movable else 0.0
    out: dict[str, Any] = {
        "movable_instance_count": movable,
        "moved_instance_count": moved,
        "average_displacement_dbu": avg,
        "max_displacement_dbu": max_disp,
    }
    if dbu:
        out["average_displacement_micron"] = avg / dbu
        out["max_displacement_micron"] = max_disp / dbu
    return out


def first_float(pattern: str, text: str) -> float | None:
    m = re.search(pattern, text, re.MULTILINE)
    if not m:
        return None
    return float(m.group(1))


def last_float(pattern: str, text: str) -> float | None:
    vals = re.findall(pattern, text, re.MULTILINE)
    return float(vals[-1]) if vals else None


def parse_hpwl(log_path: Path) -> dict[str, Any]:
    if not log_path.is_file():
        return {}
    text = log_path.read_text(encoding="utf-8", errors="replace")
    before = first_float(r"^original HPWL\s+([0-9.+-eE]+)\s+u", text)
    if before is None:
        before = first_float(r"^Original HPWL\s+([0-9.+-eE]+)\s+u", text)
    after = last_float(r"^\[INFO DPL-0022\]\s+HPWL after\s+([0-9.+-eE]+)\s+u", text)
    if after is None:
        after = last_float(r"^Final HPWL\s+([0-9.+-eE]+)\s+u", text)
    if after is None:
        after = last_float(r"^legalized HPWL\s+([0-9.+-eE]+)\s+u", text)
    out: dict[str, Any] = {"source": "openroad_dpl_log", "log": str(log_path)}
    if before is not None:
        out["before_micron"] = before
    if after is not None:
        out["after_micron"] = after
    if before is not None and after is not None:
        out["delta_micron"] = after - before
        if before != 0:
            out["delta_percent"] = (after - before) / before * 100.0
    return out


def status_from_check_placement(summary: dict[str, Any]) -> str:
    check_status = summary.get("check_status")
    if check_status is None or check_status == "":
        return summary.get("status", "missing")
    try:
        return "ok" if int(check_status) == 0 else "error"
    except (TypeError, ValueError):
        return summary.get("status", "missing")


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", required=True)
    parser.add_argument("--before", required=True)
    parser.add_argument("--after", required=True)
    parser.add_argument("--legalize-log", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()

    summary_path = Path(args.summary).resolve()
    summary = load_json(summary_path)
    dbu = float(summary["dbu_per_micron"]) if summary.get("dbu_per_micron") else None
    disp = displacement(load_snapshot(Path(args.before).resolve()), load_snapshot(Path(args.after).resolve()), dbu)
    hpwl = parse_hpwl(Path(args.legalize_log).resolve())
    status = status_from_check_placement(summary)
    payload = {
        "status": status,
        "case_id": summary.get("case_id"),
        "pattern_id": summary.get("pattern_id"),
        "legalizer_line": summary.get("legalizer_line"),
        "runtime_seconds": summary.get("runtime_seconds"),
        "dbu_per_micron": dbu,
        "hpwl": hpwl,
        "displacement": disp,
        "legality": {
            "check_status": summary.get("check_status"),
            "check_result": summary.get("check_result"),
            "check_report": summary.get("check_report"),
            "status_source": summary.get("status_source"),
            "raw_summary_status": summary.get("status"),
            "command_rc": summary.get("command_rc"),
            "command_error": summary.get("command_error"),
        },
        "row_stats": {
            key: summary.get(key)
            for key in (
                "before_row_count",
                "before_total_sites",
                "before_min_sites",
                "before_max_sites",
                "before_rows_le50",
                "after_row_count",
                "after_total_sites",
                "after_min_sites",
                "after_max_sites",
                "after_rows_le50",
            )
        },
        "supporting_files": {
            "legalize_summary": str(summary_path),
            "before_snapshot": str(Path(args.before).resolve()),
            "after_snapshot": str(Path(args.after).resolve()),
            "legalize_log": str(Path(args.legalize_log).resolve()),
        },
    }
    output = Path(args.output).resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
