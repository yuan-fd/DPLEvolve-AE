"""Read-only family evidence helpers used by the context builder."""
from __future__ import annotations

from pathlib import Path
from typing import Any


def families_by_id(registry: dict[str, Any]) -> dict[str, dict[str, Any]]:
    return {
        str(item.get("id")): item
        for item in registry.get("families", [])
        if isinstance(item, dict) and item.get("id")
    }


def active_family_ids(registry: dict[str, Any]) -> list[str]:
    ids: list[str] = []
    for item in registry.get("families", []):
        if not isinstance(item, dict):
            continue
        status = str(item.get("status", ""))
        if status in {"active_base", "promising_stack"} and item.get("id"):
            ids.append(str(item["id"]))
    return ids


def family_evidence_cards(family: dict[str, Any], *, max_cards: int = 5) -> list[str]:
    cards: list[str] = []
    summary = str(family.get("summary", "")).strip()
    if summary:
        cards.append(summary)
    for key, prefix in (
        ("strengths", "strength"),
        ("weaknesses", "watch_out"),
        ("mechanisms", "mechanism"),
    ):
        values = family.get(key, [])
        if not isinstance(values, list):
            continue
        for value in values:
            cards.append(f"{prefix}: {value}")
            if len(cards) >= max_cards:
                return cards
    return cards[:max_cards]


def compact_family_record(family: dict[str, Any]) -> dict[str, Any]:
    return {
        "id": family.get("id"),
        "status": family.get("status"),
        "parent": family.get("parent"),
        "summary": str(family.get("summary", "")).strip(),
        "mechanisms": family.get("mechanisms", [])[:6],
        "strengths": family.get("strengths", [])[:4],
        "weaknesses": family.get("weaknesses", [])[:4],
        "representative_cases": family.get("representative_cases", [])[:3],
        "notes_file": family.get("notes_file"),
    }


def load_optional_family_stats(state_root: Path) -> dict[str, Any]:
    stats_path = state_root / "memory" / "family_stats.yaml"
    if not stats_path.exists():
        return {}
    try:
        import yaml
    except ModuleNotFoundError:
        return {}
    data = yaml.safe_load(stats_path.read_text(encoding="utf-8"))
    return data if isinstance(data, dict) else {}

