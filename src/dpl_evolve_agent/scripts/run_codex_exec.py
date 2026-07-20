#!/usr/bin/env python3
"""Stable CLI shim for recording one non-interactive Codex operation."""
from __future__ import annotations

import sys
from pathlib import Path

AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from scripts.codex_exec.runner import main


if __name__ == "__main__":
    raise SystemExit(main())
