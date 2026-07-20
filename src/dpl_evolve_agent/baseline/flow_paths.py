#!/usr/bin/env python3
from __future__ import annotations

import os
from pathlib import Path


def get_flow_home() -> Path:
    flow_home = os.environ.get("FLOW_HOME")
    if flow_home:
        return Path(flow_home).expanduser().resolve()
    return Path.cwd().resolve()


def resolve_flow_path(pathlike: str | Path) -> Path:
    path = Path(pathlike)
    if path.is_absolute():
        return path
    return (get_flow_home() / path).resolve()


def normalize_flow_relative(pathlike: str | Path) -> str:
    path = Path(pathlike)
    if path.is_absolute():
        try:
            return path.resolve().relative_to(get_flow_home()).as_posix()
        except ValueError:
            return str(path)
    normalized = path.as_posix()
    if normalized.startswith("./"):
        normalized = normalized[2:]
    return normalized
