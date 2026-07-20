#!/usr/bin/env python3
"""CLI shim for the Teacher/Student DPL-Evolve optimization loop.

The implementation lives under `scripts.teacher_loop` so this executable
entrypoint stays small and stable for humans, docs, and existing automation.
"""
from __future__ import annotations

import sys
from pathlib import Path


AGENT_ROOT = Path(__file__).resolve().parents[1]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from runtime_paths import ensure_python_module, load_local_env_sh

load_local_env_sh(AGENT_ROOT)
ensure_python_module("yaml", __file__, sys.argv)

from scripts.teacher_loop.orchestrator import main


if __name__ == "__main__":
    raise SystemExit(main())
