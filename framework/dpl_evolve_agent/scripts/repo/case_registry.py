#!/usr/bin/env python3
from __future__ import annotations

import argparse
import json
import sys
from dataclasses import dataclass
from pathlib import Path

AGENT_ROOT = Path(__file__).resolve().parents[2]
PROBLEMS_ROOT = AGENT_ROOT / "problems"
CASE_SETS = PROBLEMS_ROOT / "case_sets.json"


@dataclass(frozen=True)
class CaseInfo:
    case_id: str
    problem_path: Path
    design: str
    platform: str
    design_config: str


def _read_case_sets() -> dict[str, list[str]]:
    data = json.loads(CASE_SETS.read_text(encoding="utf-8"))
    return {key: list(value) for key, value in data.items()}


def _read_problem_header(problem_path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for raw in problem_path.read_text(encoding="utf-8").splitlines():
        if not raw or raw.startswith("#") or raw.startswith(" ") or raw.startswith("-"):
            continue
        if ":" not in raw:
            continue
        key, value = raw.split(":", 1)
        key = key.strip()
        value = value.strip().strip("'\"")
        if key in {"design", "platform", "design_config", "name"}:
            values[key] = value
    return values


def list_case_ids() -> list[str]:
    return sorted(
        path.name
        for path in PROBLEMS_ROOT.iterdir()
        if path.is_dir() and (path / "problem.yaml").is_file()
    )


def case_set_names() -> list[str]:
    return sorted(_read_case_sets())


def resolve_cases(case_ids: list[str] | None = None, *, case_set: str = "default") -> list[str]:
    if case_ids:
        selected = list(case_ids)
    else:
        case_sets = _read_case_sets()
        if case_set not in case_sets:
            raise SystemExit(f"Unknown case set: {case_set}. Available: {', '.join(sorted(case_sets))}")
        selected = list(case_sets[case_set])

    known = set(list_case_ids())
    missing = [case_id for case_id in selected if case_id not in known]
    if missing:
        raise SystemExit(f"Unknown case id(s): {', '.join(missing)}")
    return selected


def get_case(case_id: str) -> CaseInfo:
    problem_path = PROBLEMS_ROOT / case_id / "problem.yaml"
    if not problem_path.is_file():
        raise SystemExit(f"Missing problem.yaml for case: {case_id}")
    header = _read_problem_header(problem_path)
    try:
        design = header["design"]
        platform = header["platform"]
    except KeyError as exc:
        raise SystemExit(f"{problem_path} is missing required key: {exc.args[0]}") from exc
    try:
        design_config = header["design_config"]
    except KeyError as exc:
        raise SystemExit(f"{problem_path} is missing required key: design_config") from exc
    return CaseInfo(
        case_id=case_id,
        problem_path=problem_path,
        design=design,
        platform=platform,
        design_config=design_config,
    )


def parse_args(argv: list[str] | None = None) -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Resolve dpl_evolve cases from the single problem registry."
    )
    parser.add_argument("--case-set", default="default")
    parser.add_argument("--case")
    parser.add_argument(
        "--field",
        choices=["case", "problem", "design_config", "design", "platform"],
        default="case",
    )
    parser.add_argument("--list-case-sets", action="store_true")
    parser.add_argument("--list-cases", action="store_true")
    return parser.parse_args(argv)


def main(argv: list[str] | None = None) -> int:
    args = parse_args(argv)
    if args.list_case_sets:
        print("\n".join(case_set_names()))
        return 0
    if args.list_cases:
        print("\n".join(list_case_ids()))
        return 0

    requested_cases = [args.case] if args.case else None
    for case_id in resolve_cases(requested_cases, case_set=args.case_set):
        info = get_case(case_id)
        if args.field == "case":
            print(info.case_id)
        elif args.field == "problem":
            print(info.problem_path)
        elif args.field == "design_config":
            print(info.design_config)
        elif args.field == "design":
            print(info.design)
        elif args.field == "platform":
            print(info.platform)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
