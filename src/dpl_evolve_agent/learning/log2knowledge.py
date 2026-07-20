#!/usr/bin/env python3
"""Convert RunDB records into observations, knowledge cards, and stats."""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from memory.knowledge_db import (
    append_unique_jsonl,
    card_prompt_fragment,
    knowledge_cards_path,
    observations_path,
    stable_id,
    update_family_region_stats,
)
from memory.run_db import load_jsonl, run_db_path
from runtime_paths import resolve_runtime_paths


def token_cost(run_record: dict[str, Any]) -> int:
    usage = run_record.get("token_usage", {}) or {}
    return int(usage.get("input_tokens", 0) or 0) + int(usage.get("output_tokens", 0) or 0)


def extract_observation(run_record: dict[str, Any]) -> dict[str, Any]:
    improved = bool(run_record.get("improved"))
    outcome = "improved" if improved else "rejected"
    family = run_record.get("patch_family") or "unknown_family"
    symbol = run_record.get("touched_symbol") or "unknown_symbol"
    problem = run_record.get("problem") or "unknown_problem"
    score = run_record.get("score")
    observation_id = stable_id(
        "O",
        [run_record.get("run_id"), family, symbol, outcome, score],
    )
    if improved:
        factual = f"{family} at {symbol} improved {problem}"
    else:
        reason = run_record.get("failure_signature") or run_record.get("next_guidance")
        factual = f"{family} at {symbol} was rejected on {problem}: {reason}"
    return {
        "id": observation_id,
        "run_id": run_record.get("run_id"),
        "problem": problem,
        "family": family,
        "symbol": symbol,
        "target_file": run_record.get("target_file"),
        "outcome": outcome,
        "status": run_record.get("status"),
        "score": score,
        "metrics": run_record.get("metrics"),
        "token_cost": token_cost(run_record),
        "factual_result": factual,
        "failure_signature": run_record.get("failure_signature"),
        "artifacts": run_record.get("artifacts", {}),
    }


def knowledge_card_from_observation(observation: dict[str, Any]) -> dict[str, Any]:
    improved = observation.get("outcome") == "improved"
    kind = "improvement_pattern" if improved else "failure_pattern"
    run_id = observation.get("run_id")
    family = observation.get("family")
    problem = observation.get("problem")
    symbol = observation.get("symbol")
    card_id = stable_id("K", [kind, run_id, family, problem, symbol])
    if improved:
        claim = f"{family} on {problem} improved the controller score at {symbol}."
        prompt = f"Consider nearby {family} edits at {symbol}; prior run {run_id} improved."
        status = "active"
        confidence = 0.55
        positive = [run_id]
        negative: list[str] = []
    else:
        reason = observation.get("failure_signature") or observation.get("factual_result")
        claim = f"{family} on {problem} did not improve at {symbol}: {reason}"
        prompt = f"Avoid repeating {family} at {symbol} without addressing: {reason}"
        status = "active"
        confidence = 0.45
        positive = []
        negative = [run_id]
    return {
        "id": card_id,
        "kind": kind,
        "claim": claim,
        "scope": {
            "problem": problem,
            "family": family,
            "symbol": symbol,
            "target_file": observation.get("target_file"),
        },
        "evidence_runs": {
            "positive": positive,
            "negative": negative,
        },
        "confidence": confidence,
        "contradiction_count": 0,
        "prompt_fragment": prompt,
        "token_cost": observation.get("token_cost", 0),
        "status": status,
        "created_from": run_id,
    }


def process_run_record(state_root: Path, run_record: dict[str, Any]) -> dict[str, Any]:
    observation = extract_observation(run_record)
    card = knowledge_card_from_observation(observation)
    append_unique_jsonl(observations_path(state_root), observation)
    append_unique_jsonl(knowledge_cards_path(state_root), card)
    stats = update_family_region_stats(state_root, run_record)
    return {
        "observation": observation,
        "knowledge_card": card,
        "stats": stats,
    }


def load_selected_run(state_root: Path, *, run_id: str | None) -> dict[str, Any]:
    records = load_jsonl(run_db_path(state_root))
    if not records:
        raise SystemExit(f"RunDB is empty: {run_db_path(state_root)}")
    if run_id is None:
        return records[-1]
    for record in reversed(records):
        if record.get("run_id") == run_id:
            return record
    raise SystemExit(f"Run id not found in RunDB: {run_id}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--state-root", type=Path)
    parser.add_argument("--run-record-json", type=Path)
    parser.add_argument("--run-id")
    parser.add_argument("--print-summary", action="store_true")
    args = parser.parse_args()

    if args.state_root is None:
        runtime = resolve_runtime_paths(
            anchor_file=__file__,
            agent_root_levels_up=1,
            script_name="log2knowledge.py",
        )
        state_root = runtime.state_root
    else:
        state_root = args.state_root.resolve()

    if args.run_record_json:
        run_record = json.loads(args.run_record_json.read_text(encoding="utf-8"))
    else:
        run_record = load_selected_run(state_root, run_id=args.run_id)
    result = process_run_record(state_root, run_record)
    if args.print_summary:
        card = result["knowledge_card"]
        print(card_prompt_fragment(card))


if __name__ == "__main__":
    main()

