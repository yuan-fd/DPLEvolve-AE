"""Bridge module that re-exports key constants for the evaluator.

The canonical definitions live in scripts/teacher_loop/evidence.py.
This module exists so that adapter code (e.g. adapters/science_codeevolve/evaluator.py)
can import with a short, stable import path that does not depend on the internal
teacher-loop module tree.

Generated for AE reproducibility — the constants here must stay in sync with
scripts/teacher_loop/evidence.py.
"""
from __future__ import annotations

from scripts.teacher_loop.evidence import HPWL_RUNTIME_GAIN_FORMULA, VALUE_REFERENCE_LINE

__all__ = ["HPWL_RUNTIME_GAIN_FORMULA", "VALUE_REFERENCE_LINE"]
