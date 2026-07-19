"""Knowledge-card and posterior-stat storage helpers."""
from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Any, Iterable

from memory.run_db import load_jsonl, memory_root


def observations_path(state_root: Path) -> Path:
    return memory_root(state_root) / "observations.jsonl"


def knowledge_cards_path(state_root: Path) -> Path:
    return memory_root(state_root) / "knowledge_cards.jsonl"


def family_region_stats_path(state_root: Path) -> Path:
    return memory_root(state_root) / "family_region_stats.json"


def stable_id(prefix: str, parts: Iterable[Any]) -> str:
    raw = "|".join(str(part) for part in parts)
    digest = hashlib.sha1(raw.encode("utf-8")).hexdigest()[:12]
    return f"{prefix}_{digest}"


def append_unique_jsonl(path: Path, record: dict[str, Any], *, key: str = "id") -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    existing = {
        item.get(key)
        for item in load_jsonl(path)
        if isinstance(item, dict) and item.get(key)
    }
    if record.get(key) in existing:
        return
    with path.open("a", encoding="utf-8") as f:
        f.write(json.dumps(record, sort_keys=True) + "\n")


def load_stats(state_root: Path) -> dict[str, Any]:
    path = family_region_stats_path(state_root)
    if not path.exists():
        return {"families": {}, "regions": {}}
    try:
        data = json.loads(path.read_text(encoding="utf-8"))
    except json.JSONDecodeError:
        return {"families": {}, "regions": {}}
    if not isinstance(data, dict):
        return {"families": {}, "regions": {}}
    data.setdefault("families", {})
    data.setdefault("regions", {})
    return data


def write_stats(state_root: Path, stats: dict[str, Any]) -> None:
    path = family_region_stats_path(state_root)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(stats, indent=2, sort_keys=True) + "\n", encoding="utf-8")


def recent_knowledge_cards(
    state_root: Path,
    *,
    problem: str | None = None,
    family: str | None = None,
    symbol: str | None = None,
    status: str | None = None,
    limit: int = 8,
) -> list[dict[str, Any]]:
    cards = load_jsonl(knowledge_cards_path(state_root))
    if problem is not None:
        cards = [item for item in cards if item.get("scope", {}).get("problem") == problem]
    if family is not None:
        cards = [item for item in cards if item.get("scope", {}).get("family") == family]
    if symbol is not None:
        cards = [item for item in cards if item.get("scope", {}).get("symbol") == symbol]
    if status is not None:
        cards = [item for item in cards if item.get("status") == status]
    cards.sort(key=lambda item: (item.get("confidence", 0.0), item.get("created_from", "")))
    return cards[-limit:]


def card_prompt_fragment(card: dict[str, Any]) -> str:
    fragment = str(card.get("prompt_fragment") or card.get("claim") or "").strip()
    confidence = card.get("confidence")
    if confidence is None:
        return fragment
    return f"{fragment} (confidence={float(confidence):.2f})"


def _rate(numerator: int, denominator: int) -> float:
    return round(numerator / denominator, 6) if denominator else 0.0


def update_family_region_stats(state_root: Path, run_record: dict[str, Any]) -> dict[str, Any]:
    stats = load_stats(state_root)
    run_id = str(run_record.get("run_id") or "")
    processed = stats.setdefault("processed_run_ids", [])
    if run_id and run_id in processed:
        return stats
    family = str(run_record.get("patch_family") or "unknown")
    symbol = str(run_record.get("touched_symbol") or "unknown")
    problem = str(run_record.get("problem") or "unknown")
    improved = bool(run_record.get("improved"))
    accepted = str(run_record.get("status")) == "accepted"
    score = run_record.get("score")
    token_usage = run_record.get("token_usage", {}) or {}
    token_cost = int(token_usage.get("input_tokens", 0) or 0) + int(
        token_usage.get("output_tokens", 0) or 0
    )

    def update_bucket(bucket: dict[str, Any]) -> None:
        bucket["attempts"] = int(bucket.get("attempts", 0)) + 1
        bucket["accepted"] = int(bucket.get("accepted", 0)) + (1 if accepted else 0)
        bucket["improved"] = int(bucket.get("improved", 0)) + (1 if improved else 0)
        bucket["rejected"] = int(bucket.get("rejected", 0)) + (0 if accepted else 1)
        bucket["token_cost_total"] = int(bucket.get("token_cost_total", 0)) + token_cost
        if score is not None:
            scores = bucket.setdefault("scores", [])
            scores.append(float(score))
            bucket["best_score"] = max(float(bucket.get("best_score", score)), float(score))
        bucket["improvement_rate"] = _rate(int(bucket["improved"]), int(bucket["attempts"]))
        bucket["accept_rate"] = _rate(int(bucket["accepted"]), int(bucket["attempts"]))
        bucket["last_problem"] = problem
        bucket["last_run_id"] = run_record.get("run_id")

    families = stats.setdefault("families", {})
    regions = stats.setdefault("regions", {})
    family_bucket = families.setdefault(family, {})
    region_bucket = regions.setdefault(symbol, {})
    update_bucket(family_bucket)
    update_bucket(region_bucket)
    family_bucket.setdefault("by_problem", {}).setdefault(problem, {"attempts": 0, "improved": 0})
    by_problem = family_bucket["by_problem"][problem]
    by_problem["attempts"] = int(by_problem.get("attempts", 0)) + 1
    by_problem["improved"] = int(by_problem.get("improved", 0)) + (1 if improved else 0)
    by_problem["improvement_rate"] = _rate(by_problem["improved"], by_problem["attempts"])
    if run_id:
        processed.append(run_id)
    write_stats(state_root, stats)
    return stats
