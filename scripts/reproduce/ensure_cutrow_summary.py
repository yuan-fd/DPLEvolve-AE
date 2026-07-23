#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any


def first_float(pattern: str, text: str) -> float | None:
    match = re.search(pattern, text, re.MULTILINE)
    return float(match.group(1)) if match else None


def last_float(pattern: str, text: str) -> float | None:
    values = re.findall(pattern, text, re.MULTILINE)
    return float(values[-1]) if values else None


def infer_runtime(text: str) -> float | None:
    runtime = last_float(r"^Took\s+([0-9.+-eE]+)\s+seconds: detailed_placement", text)
    if runtime is not None:
        return runtime
    return last_float(r"^Took\s+([0-9.+-eE]+)\s+seconds: detailed_placement.*$", text)


def infer_status(text: str, rc: int | None) -> tuple[str, str, str]:
    if "detailed placement checks failed during check placement" in text or "DPL-0033" in text:
        return "error", "1", "check_placement_failed"
    if "OpenROAD cut-row legalization complete:" in text and re.search(r"^\s+status:\s+ok\s*$", text, re.MULTILINE):
        return "ok", "0", "ok"
    if rc == 124:
        return "timeout", "not_run", "timeout"
    if rc and rc != 0:
        return "error", "not_run", "openroad_failed_before_check_placement"
    return "missing", "not_run", "summary_missing"


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--summary", required=True)
    parser.add_argument("--log", required=True)
    parser.add_argument("--eval-dir", required=True)
    parser.add_argument("--case-id", required=True)
    parser.add_argument("--pattern-id", required=True)
    parser.add_argument("--line", required=True)
    parser.add_argument("--rc", type=int, default=0)
    parser.add_argument("--timeout", type=float, default=0.0)
    args = parser.parse_args()

    summary_path = Path(args.summary).resolve()
    if summary_path.is_file():
        return 0

    log_path = Path(args.log).resolve()
    text = log_path.read_text(encoding="utf-8", errors="replace") if log_path.is_file() else ""
    eval_dir = Path(args.eval_dir).resolve()
    status, check_status, check_result = infer_status(text, args.rc)
    runtime = args.timeout if status == "timeout" and args.timeout else infer_runtime(text)

    payload: dict[str, Any] = {
        "status": status,
        "status_source": "check_placement" if check_status in {"0", "1"} else "wrapper_inference",
        "case_id": args.case_id,
        "pattern_id": args.pattern_id,
        "legalizer_line": args.line,
        "command": "detailed_placement -use_negotiation" if args.line == "openroad_dpl_negotiation" else args.line,
        "command_rc": args.rc,
        "command_error": "",
        "error_message": check_result,
        "check_status": check_status,
        "check_result": check_result,
        "runtime_seconds": runtime,
        "dbu_per_micron": 2000,
        "before_snapshot": str(eval_dir / "before.tsv"),
        "after_snapshot": str(eval_dir / "after.tsv"),
        "detailed_placement_report": str(eval_dir / "detailed_placement_report.json"),
        "check_report": str(eval_dir / "check_placement_report.json"),
        "output_odb": str(eval_dir / "legalized.odb"),
        "output_def": str(eval_dir / "legalized.def"),
    }
    if args.rc == 124:
        payload["timeout_seconds"] = args.timeout
        payload["reason"] = "openroad_eval_timeout"

    summary_path.parent.mkdir(parents=True, exist_ok=True)
    summary_path.write_text(json.dumps(payload, indent=2, sort_keys=True) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
