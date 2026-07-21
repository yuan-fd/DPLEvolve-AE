"""
Archive helper skeleton for dpl_evolve runs.

Stores:
- packet
- patch note
- evaluator JSON
- metrics.json path
- optional diff

NOTE: This module is intentionally a stub in the AE artifact release.
The full archive implementation (which snapshots evolved OpenDP source
trees, evaluator outputs, and per-run diffs) is coupled to the live
experimental infrastructure (ORFS workspace, build artifacts, and LLM
API sessions) which are not distributed with the AE package.

For artifact evaluation purposes, the pre-computed program archives
are provided under artifacts/01-table4-qor/selected-programs/ and
verified via SHA-256 integrity manifests.
"""
from __future__ import annotations
