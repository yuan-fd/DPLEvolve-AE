"""Shared Teacher/Student loop constants.

Keep these values separate from dataclasses and path helpers so CLI parsing,
evidence collection, prompt assembly, and reporting can share one stable
contract.
"""
from __future__ import annotations

START_KINDS = (
    "framework",
    "diamond",
    "source_topk_diamond",
    "default_negotiation",
)

PREPARED_START_KIND_PATTERN = (
    r"(?:framework|diamond|source_topk_diamond|default_negotiation)"
)

CANONICAL_LINES = (
    "openroad_dpl_flow",
    "openroad_dpl_negotiation",
    "evolve_default",
)

__all__ = [
    "CANONICAL_LINES",
    "PREPARED_START_KIND_PATTERN",
    "START_KINDS",
]
