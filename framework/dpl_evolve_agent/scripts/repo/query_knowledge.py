#!/usr/bin/env python3
"""Query the repo-local LEGO-lite skill index.

The helper is intentionally deterministic and dependency-free.  It gives
Teacher/Student agents a fast first-stage lookup over concise knowledge records;
the returned full-card paths can then be opened only when relevant.
"""
from __future__ import annotations

import argparse
import json
import re
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[2]
DEFAULT_SKILL_INDEX = AGENT_ROOT / "knowledge" / "index" / "skill_cards.jsonl"
DEFAULT_STACK_INDEX = AGENT_ROOT / "knowledge" / "index" / "mechanism_stack_cards.jsonl"
DEFAULT_INDEXES = (DEFAULT_SKILL_INDEX, DEFAULT_STACK_INDEX)

TOKEN_RE = re.compile(r"[A-Za-z0-9_+%.-]+")
BLUEPRINT_RE = re.compile(r"\bblueprint\s+([A-Za-z](?:\+[A-Za-z])?)\b", re.IGNORECASE)

CANONICAL_STAGES = {
    "case_diagnosis",
    "start_selection",
    "legalization",
    "improve_placement",
    "handoff",
    "build_eval_log_diagnosis",
    "teacher_review",
}

REQUIRED_FIELDS = (
    "skill_id",
    "stage",
    "group",
    "summary",
    "use_when",
    "avoid_when",
    "source_handles",
    "metrics_to_check",
    "log_signals",
    "done_criteria",
    "full_card",
    "keywords",
)

MECHANISM_STACK_FIELDS = (
    "blueprints",
    "mechanism_roles",
    "compatible_with",
    "failure_buckets",
    "handoff_payloads",
    "stacking_notes",
    "first_patch_handles",
)

ROLE_PREFIXES = (
    "start_basin:",
    "producer:",
    "handoff:",
    "consumer:",
    "post_consumer:",
)

STAGE_ALIASES = {
    "detailed_placement": {
        "teacher_review",
        "legalization",
        "improve_placement",
        "handoff",
    },
    "detailed-placement": {
        "teacher_review",
        "legalization",
        "improve_placement",
        "handoff",
    },
    "blueprint": {"teacher_review"},
    "routing": {"teacher_review"},
    "mechanism_stack": {"teacher_review"},
    "mechanism-stack": {"teacher_review"},
    "dpo": {"improve_placement"},
    "improve": {"improve_placement"},
    "legalizer": {"legalization"},
    "diagnosis": {"case_diagnosis", "build_eval_log_diagnosis"},
    "review": {"teacher_review"},
}


def tokenize(text: str) -> list[str]:
    return [tok.lower() for tok in TOKEN_RE.findall(text)]


def load_cards(path: Path) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    if not path.exists():
        raise SystemExit(f"[ERROR] missing knowledge index: {path}")
    for lineno, raw in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        raw = raw.strip()
        if not raw:
            continue
        try:
            card = json.loads(raw)
        except json.JSONDecodeError as exc:
            raise SystemExit(f"[ERROR] invalid JSON at {path}:{lineno}: {exc}") from exc
        if not isinstance(card, dict) or not card.get("skill_id"):
            raise SystemExit(f"[ERROR] invalid knowledge card at {path}:{lineno}")
        cards.append(card)
    return cards


def load_all_cards(paths: list[Path]) -> list[dict[str, Any]]:
    cards: list[dict[str, Any]] = []
    for path in paths:
        if path.exists():
            cards.extend(load_cards(path))
    if not cards:
        joined = ", ".join(str(path) for path in paths)
        raise SystemExit(f"[ERROR] missing knowledge indexes: {joined}")
    return cards


def flatten_value(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        flattened: list[str] = []
        for item in value:
            flattened.extend(flatten_value(item))
        return flattened
    if isinstance(value, dict):
        flattened = []
        for key, item in value.items():
            flattened.append(str(key))
            flattened.extend(flatten_value(item))
        return flattened
    return [str(value)]


def text_blob(card: dict[str, Any]) -> str:
    parts: list[str] = []
    for key in (
        "skill_id",
        "stage",
        "group",
        "summary",
        "use_when",
        "avoid_when",
        "full_card",
    ):
        parts.append(str(card.get(key, "")))
    for key in (
        "source_handles",
        "patches",
        "metrics_to_check",
        "log_signals",
        "done_criteria",
        "keywords",
        "blueprints",
        "mechanism_roles",
        "compatible_with",
        "failure_buckets",
        "handoff_payloads",
        "stacking_notes",
        "prerequisites",
        "first_patch_handles",
    ):
        parts.extend(flatten_value(card.get(key, [])))
    return "\n".join(parts)


def expand_stages(stage_args: list[str]) -> set[str]:
    stages: set[str] = set()
    for raw_stage in stage_args:
        stage = raw_stage.strip().lower()
        if not stage:
            continue
        stages.add(stage)
        stages.update(STAGE_ALIASES.get(stage, set()))
    return stages


def normalize_blueprint(value: str) -> str:
    return value.strip().upper().replace(" ", "")


def requested_blueprints(query: str) -> set[str]:
    return {normalize_blueprint(match.group(1)) for match in BLUEPRINT_RE.finditer(query)}


def score_card(
    card: dict[str, Any],
    query_tokens: list[str],
    stages: set[str],
    blueprints: set[str],
) -> float:
    blob = text_blob(card).lower()
    blob_tokens = set(tokenize(blob))
    score = 0.0
    if stages:
        if str(card.get("stage", "")).lower() in stages:
            score += 8.0
        else:
            score -= 2.0
    for tok in query_tokens:
        if tok in blob_tokens:
            score += 2.0
        elif tok and tok in blob:
            score += 0.75
    # Keep exact skill/stage keyword hits stable and easy to understand.
    query = " ".join(query_tokens)
    skill_id = str(card.get("skill_id", "")).lower()
    if query and query in blob:
        score += 3.0
    if any(tok in skill_id for tok in query_tokens):
        score += 1.5
    if blueprints:
        card_blueprints = {
            normalize_blueprint(value) for value in as_list(card.get("blueprints"))
        }
        if card_blueprints:
            if card_blueprints & blueprints:
                score += 10.0
            else:
                score -= 2.0
    return score


def as_list(value: Any) -> list[str]:
    if value is None:
        return []
    if isinstance(value, list):
        return [str(item) for item in value]
    return [str(value)]


def optional_markdown_list(card: dict[str, Any], key: str, label: str | None = None) -> str | None:
    if key not in card:
        return None
    values = as_list(card.get(key))
    if not values:
        return None
    prefix = label or key
    return f"- {prefix}: " + "; ".join(f"`{x}`" for x in values)


def format_markdown(rows: list[tuple[float, dict[str, Any]]], *, show_full: bool) -> str:
    lines = ["# Knowledge Query", ""]
    if not rows:
        lines.append("No matching knowledge records.")
        return "\n".join(lines) + "\n"
    for score, card in rows:
        full_card = AGENT_ROOT / str(card.get("full_card", ""))
        lines.extend(
            [
                f"## {card['skill_id']} (score={score:.2f})",
                f"- stage: `{card.get('stage', '')}`",
                f"- group: `{card.get('group', '')}`",
                f"- summary: {card.get('summary', '')}",
                f"- use_when: {card.get('use_when', '')}",
                f"- avoid_when: {card.get('avoid_when', '')}",
                f"- skill_note: `{full_card}`",
            ]
        )
        for key in (
            "blueprints",
            "mechanism_roles",
            "compatible_with",
            "failure_buckets",
            "handoff_payloads",
            "stacking_notes",
            "prerequisites",
            "first_patch_handles",
        ):
            line = optional_markdown_list(card, key)
            if line:
                lines.append(line)
        lines.extend(
            [
                "- source_handles: " + ", ".join(f"`{x}`" for x in as_list(card.get("source_handles"))) if card.get("source_handles") else "- source_handles: none",
                "- patches: " + ", ".join(f"`{x}`" for x in as_list(card.get("patches"))) if card.get("patches") else "- patches: none",
                "- metrics_to_check: " + ", ".join(f"`{x}`" for x in as_list(card.get("metrics_to_check"))),
                "- log_signals: " + ", ".join(f"`{x}`" for x in as_list(card.get("log_signals"))),
                "- done_criteria: " + "; ".join(as_list(card.get("done_criteria"))),
                f"- rg: `rg -n \"{card['skill_id']}|{'|'.join(as_list(card.get('keywords'))[:3])}\" knowledge/index knowledge/skills knowledge/algorithms knowledge/routing knowledge/support`",
                "",
            ]
        )
        if show_full and full_card.exists():
            lines.extend(["```markdown", full_card.read_text(encoding="utf-8").strip(), "```", ""])
    return "\n".join(lines) + "\n"


def validate_cards(cards: list[dict[str, Any]]) -> int:
    """Validate local index references without requiring external tools."""
    errors: list[str] = []
    seen: set[str] = set()
    for card in cards:
        skill_id = str(card.get("skill_id", ""))
        for field in REQUIRED_FIELDS:
            value = card.get(field)
            if value in (None, "", []):
                errors.append(f"{skill_id or '<missing skill_id>'}: missing required field {field}")
        if skill_id in seen:
            errors.append(f"duplicate skill_id: {skill_id}")
        seen.add(skill_id)
        stage = str(card.get("stage", ""))
        if stage and stage not in CANONICAL_STAGES:
            errors.append(f"{skill_id}: non-canonical stage {stage}")
        if str(card.get("group", "")) == "mechanism_stack":
            for field in MECHANISM_STACK_FIELDS:
                value = card.get(field)
                if value in (None, "", []):
                    errors.append(f"{skill_id}: missing mechanism-stack field {field}")
            roles = as_list(card.get("mechanism_roles"))
            if not any(role.startswith("producer:") for role in roles):
                errors.append(f"{skill_id}: mechanism_roles missing producer role")
            if not any(role.startswith("handoff:") for role in roles):
                errors.append(f"{skill_id}: mechanism_roles missing handoff role")
            if not any(role.startswith("consumer:") for role in roles):
                errors.append(f"{skill_id}: mechanism_roles missing consumer role")
            for role in roles:
                if ":" in role and not role.startswith(ROLE_PREFIXES):
                    errors.append(f"{skill_id}: unknown mechanism role prefix in {role!r}")
        full_card = AGENT_ROOT / str(card.get("full_card", ""))
        if not full_card.exists():
            errors.append(f"{skill_id}: missing skill note {full_card}")
        for handle in as_list(card.get("source_handles")):
            if not handle.startswith(("knowledge/", "scripts/", "baseline/", "configs/")):
                continue
            handle_path = AGENT_ROOT / handle.rstrip("/")
            if not handle_path.exists():
                errors.append(f"{skill_id}: missing source handle {handle_path}")
        for patch in as_list(card.get("patches")):
            patch_path = AGENT_ROOT / patch
            if not patch_path.exists():
                errors.append(f"{skill_id}: missing patch {patch_path}")
    if errors:
        for error in errors:
            print(f"[ERROR] {error}")
        return 1
    print(f"[OK] validated {len(cards)} knowledge records")
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description="Query DPL-Evolve LEGO-lite knowledge records.")
    parser.add_argument(
        "--index",
        type=Path,
        action="append",
        default=None,
        help="Knowledge index to query. Repeatable; defaults to skill and mechanism-stack indexes.",
    )
    parser.add_argument("--stage", action="append", default=[], help="Filter/boost one workflow stage. Repeatable.")
    parser.add_argument("--q", default="", help="Lexical query over summaries, handles, metrics, and keywords.")
    parser.add_argument("--limit", type=int, default=5)
    parser.add_argument("--show-full", action="store_true", help="Append matched markdown skill notes.")
    parser.add_argument("--json", action="store_true", help="Emit scored cards as JSON.")
    parser.add_argument("--validate", action="store_true", help="Validate index full-card and patch references.")
    args = parser.parse_args()

    index_paths = args.index if args.index else list(DEFAULT_INDEXES)
    cards = load_all_cards(index_paths)
    if args.validate:
        return validate_cards(cards)

    query_tokens = tokenize(args.q)
    stages = expand_stages(args.stage)
    blueprints = requested_blueprints(args.q)
    rows = [(score_card(card, query_tokens, stages, blueprints), card) for card in cards]
    if stages:
        # Still allow out-of-stage hits when query strongly matches, but hide weak ones.
        rows = [row for row in rows if row[0] > 0]
    elif query_tokens:
        rows = [row for row in rows if row[0] > 0]
    rows.sort(key=lambda row: (row[0], str(row[1].get("skill_id", ""))), reverse=True)
    rows = rows[: max(1, args.limit)]

    if args.json:
        payload = [dict(card, score=round(score, 3)) for score, card in rows]
        print(json.dumps(payload, indent=2, sort_keys=True))
    else:
        print(format_markdown(rows, show_full=args.show_full), end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
