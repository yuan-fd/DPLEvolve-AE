#!/usr/bin/env python3
"""Build a compact packet for DPL-Evolve legalize/improve source edits.

`detailed_placement_evolve` is the stable legalize entrypoint, followed by the
evolved improve-placement step. Agents should evolve a coherent
legalize/improve mechanism in private `dpl_evolve` source while using archived
algorithms only as donors. The private source seed is selected by the
orchestrator (`framework`, `diamond`, `default_negotiation`, or `prepared`), so
this packet must
stay seed-neutral. Downstream optimize-mirroring may remain in
evaluation, but it is not part of the active patch surface.
"""
from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any

AGENT_ROOT = Path(__file__).resolve().parents[2]
if str(AGENT_ROOT) not in sys.path:
    sys.path.insert(0, str(AGENT_ROOT))

from memory.knowledge_db import card_prompt_fragment, recent_knowledge_cards
from memory.run_db import recent_runs, summarize_recent_failures
from runtime_paths import resolve_runtime_paths

try:
    import yaml  # type: ignore[import-not-found]
except ModuleNotFoundError:  # Keep packet generation usable in minimal envs.
    yaml = None


LEGALIZE_IMPROVE_SURFACE = {
    "file": "tools/OpenROAD/src/dpl_evolve/src/StudentAlgorithm.cpp",
    "header_files": [
        "adapters/science_codeevolve/patch_surface.yaml",
        "tools/OpenROAD/src/dpl_evolve/include/dpl_evolve/Opendp.h",
        "tools/OpenROAD/src/dpl_evolve/src/EvolveLegalizer.h",
        "tools/OpenROAD/src/dpl_evolve/src/EvolveContext.h",
        "tools/OpenROAD/src/dpl_evolve/src/EvolveTelemetry.h",
        "tools/OpenROAD/src/dpl_evolve/src/LegalmCommon.h",
        "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_random.h",
        "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_manager.h",
        "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_hpwl.h",
    ],
    "symbol": "dpl_evolve legalize/improve flow",
    "allowed_patch_modes": [
        "stage_ordering",
        "stage_guarding",
        "cross_stage_cooptimization",
        "shared_objective_handoff",
        "legalize_improve_coordination",
        "relaxed_guidance_stage",
        "assignment_donor_stage",
        "full_legalization_stage",
        "detailed_improvement_flow",
        "objective_redesign",
        "bounded_candidate_generation",
        "bounded_polish_stage",
        "high_performance_data_structure",
        "telemetry_only",
    ],
    "risk": "high",
}


REFERENCE_FILES = [
    "knowledge/policies/evidence_policy.md",
    "knowledge/contracts/metric_contract.md",
    "knowledge/support/case_evolution/strategy_inspiration_by_design_feature.md",
    "knowledge/support/case_evolution/dense_legalizer_routing.md",
    "knowledge/support/dpo/source_level_mechanisms.md",
    "knowledge/support/dpo/openroad_native_handoff.md",
    "knowledge/index/skill_cards.jsonl",
    "knowledge/skills/README.md",
    "scripts/repo/query_knowledge.py",
    "family_variants/REFERENCE_INDEX.yaml",
    "family_variants/README.md",
    "family_variants/openroad_diamond/README.md",
    "family_variants/openroad_negotiation_nblg/README.md",
    "family_variants/legalm_guidance/README.md",
    "family_variants/dreamplace_abacus/README.md",
    "knowledge/reference/papers/paper_sources.yaml",
]


def abs_orfs(rel: str, runtime: Any) -> str:
    return str(runtime.orfs_root / rel)


def abs_agent(rel: str) -> str:
    return str(AGENT_ROOT / rel)


def parse_scalar(value: str) -> Any:
    value = value.strip()
    if value in {"true", "True"}:
        return True
    if value in {"false", "False"}:
        return False
    if value in {"null", "None", "~"}:
        return None
    if (value.startswith('"') and value.endswith('"')) or (
        value.startswith("'") and value.endswith("'")
    ):
        return value[1:-1]
    return value


def load_simple_yaml(path: Path) -> dict[str, Any]:
    """Tiny YAML subset reader for repo-local problem/index files.

    It supports top-level mappings, one-level nested mappings, and block lists.
    This is not a general YAML parser; it only keeps lightweight packet
    generation working when PyYAML is unavailable.
    """

    root: dict[str, Any] = {}
    stack: list[tuple[int, Any]] = [(-1, root)]
    last_key_for_indent: dict[int, str] = {}

    for raw_line in path.read_text(encoding="utf-8").splitlines():
        if not raw_line.strip() or raw_line.lstrip().startswith("#"):
            continue
        indent = len(raw_line) - len(raw_line.lstrip(" "))
        line = raw_line.strip()

        while stack and indent <= stack[-1][0]:
            stack.pop()
        container = stack[-1][1]

        if line.startswith("- "):
            value = parse_scalar(line[2:])
            if not isinstance(container, list):
                parent = stack[-2][1] if len(stack) >= 2 else root
                key = last_key_for_indent.get(stack[-1][0])
                if isinstance(parent, dict) and key:
                    parent[key] = []
                    container = parent[key]
                    stack[-1] = (stack[-1][0], container)
            if isinstance(container, list):
                container.append(value)
            continue

        if ":" not in line or not isinstance(container, dict):
            continue
        key, raw_value = line.split(":", 1)
        key = key.strip()
        raw_value = raw_value.strip()
        if raw_value:
            container[key] = parse_scalar(raw_value)
        else:
            container[key] = {}
            stack.append((indent, container[key]))
            last_key_for_indent[indent] = key
    return root


def load_yaml(path: Path) -> Any:
    if yaml is not None:
        data = yaml.safe_load(path.read_text(encoding="utf-8"))
        return {} if data is None else data
    return load_simple_yaml(path)


def maybe_load_yaml(path: Path) -> Any:
    if not path.exists():
        return {}
    return load_yaml(path)


def compact_problem(problem: dict[str, Any]) -> dict[str, Any]:
    objective = problem.get("objective") or {}
    return {
        "name": problem.get("name"),
        "design": problem.get("design"),
        "platform": problem.get("platform"),
        "task_type": problem.get("task_type"),
        "input_stage": problem.get("input_stage"),
        "evaluation_track": problem.get("evaluation_track", "strict_legalizer"),
        "objective": {
            "primary": objective.get("primary", "legalization_hpwl"),
            "secondary": objective.get("secondary", []),
            "guardrails": objective.get("guardrails", ["legality", "runtime"]),
        },
    }


def packet_markdown(packet: dict[str, Any]) -> str:
    surface = packet["selected_patch_surface"]
    primary_handles = packet.get("primary_handles", [])
    secondary_handles = packet.get("secondary_handles", [])
    reference_shelf = packet.get("reference_shelf", packet["reference_files"])
    registry_objective = packet["problem"]["objective"].get("primary", "")
    lines = [
        "# Current Run Packet",
        "",
        f"- problem: {packet['problem']['name']}",
        f"- design/platform: {packet['problem']['design']} / {packet['problem']['platform']}",
        f"- track: {packet['problem']['evaluation_track']}",
        "- primary objective: final canonical HPWL after the complete detailed-placement flow",
        f"- registry objective field: {registry_objective} (legacy label; do not optimize legalization HPWL alone)",
        f"- selected surface: {surface['symbol']} in {surface['file']}",
        "- seed policy: use the Student workspace packet for the actual "
        "`start_kind` and parent source; this packet is seed-neutral",
        "",
        "## Direction",
        "- Keep `detailed_placement_evolve` as the primary evolved placement command.",
        "- Patch private `dpl_evolve` source across legalization/detailed placement, improve placement, and their handoff.",
        "- Use reference algorithms as donors, then implement a clean, high-performance mechanism in `dpl_evolve`.",
        "- Treat the stage flow as a scaffold; implementation details must be justified by canonical evaluator evidence.",
        "- Treat design-feature strategy notes as mechanism inspiration, not route selectors or case-name rules.",
        "- Prepared start parents are `framework`, `diamond`, and `default_negotiation`; choose among them from case features and stage evidence.",
        "- Use DPO source-level mechanism cards for improve-placement work: bounded source-edge scoring, transaction rollback, bounded staged descent, scoped LSMC, and handoff consumers. Implement in current source; do not rely on code transplant.",
        "- Implement handoff with OpenROAD-native in-process state: compact ids/vectors and `odb`/DetailedMgr mappings, not log/file/string payloads.",
        "- Use `knowledge/index/skill_cards.jsonl` and `scripts/repo/query_knowledge.py` as the first-stage LEGO-lite skill lookup before opening one matched skill note.",
        "- Do not treat the packet references below as a reading checklist. Route first from stage evidence and open at most one matched skill/card when the route needs it.",
        "",
        "## Routing Surface",
        f"- file: {surface['file']}",
        f"- symbol: {surface['symbol']}",
        "- primary handles:",
        *[f"- {item}" for item in primary_handles],
        "- secondary handles:",
        *[f"- {item}" for item in secondary_handles],
        f"- risk: {surface['risk']}",
        "- allowed patch modes:",
        *[f"- {item}" for item in surface["allowed_patch_modes"]],
        "",
        "## Query Entry Points",
        *[f"- {item}" for item in reference_shelf],
        "",
        "## Evidence Cards",
        *[f"- {item}" for item in packet["evidence_cards"]],
        "",
        "## Last Failures To Avoid",
        *[f"- {item}" for item in packet["last_failures_to_avoid"]],
    ]
    return "\n".join(lines) + "\n"


def build_packet(problem_dir: Path, out_dir: Path) -> dict[str, Any]:
    runtime = resolve_runtime_paths(
        anchor_file=__file__,
        agent_root_levels_up=2,
        script_name="kb_query.py",
    )
    problem_path = problem_dir / "problem.yaml"
    problem = compact_problem(load_yaml(problem_path))
    problem_name = str(problem.get("name") or problem_dir.name)
    cards = [
        card_prompt_fragment(card)
        for card in recent_knowledge_cards(
            runtime.state_root,
            problem=problem_name,
            status="active",
            limit=6,
        )
    ]
    cards = [card for card in cards if card]
    recent = recent_runs(runtime.state_root, problem=problem_name, limit=12)
    failures = summarize_recent_failures(recent, limit=6)

    reference_index = maybe_load_yaml(AGENT_ROOT / "family_variants" / "REFERENCE_INDEX.yaml")
    packet = {
        "schema_version": 2,
        "packet_type": "dpl_evolve_legalize_improve_patch",
        "problem": problem,
        "reference_index": reference_index,
        "selected_patch_surface": LEGALIZE_IMPROVE_SURFACE,
        "primary_handles": [
            abs_orfs("tools/OpenROAD/src/dpl_evolve/src/StudentAlgorithm.cpp", runtime),
            abs_orfs("tools/OpenROAD/src/dpl_evolve/src/Optdp.cpp", runtime),
            abs_orfs("tools/OpenROAD/src/dpl_evolve/src/LegalmGuidance.cpp", runtime),
        ],
        "secondary_handles": [
            abs_orfs(
                "tools/OpenROAD/src/dpl_evolve/src/LegalmFullLegalization.cpp", runtime
            ),
            abs_orfs("tools/OpenROAD/src/dpl_evolve/include/dpl_evolve/Opendp.h", runtime),
        ],
        "reference_shelf": [
            abs_agent("knowledge/index/skill_cards.jsonl"),
            abs_agent("scripts/repo/query_knowledge.py"),
            abs_agent("knowledge/contracts/metric_contract.md"),
            abs_agent("knowledge/support/dpo/openroad_native_handoff.md"),
        ],
        "reference_files": REFERENCE_FILES,
        "evidence_cards": cards,
        "last_failures_to_avoid": failures,
        "read_order": [
            "adapters/science_codeevolve/current_run_packet.md",
            "skills/patch_rules.md",
            str(problem_path.relative_to(AGENT_ROOT)),
            "adapters/science_codeevolve/patch_surface.yaml",
            "knowledge/policies/evidence_policy.md",
            "knowledge/contracts/metric_contract.md",
            "knowledge/support/case_evolution/strategy_inspiration_by_design_feature.md",
            "knowledge/support/case_evolution/dense_legalizer_routing.md",
            "knowledge/support/dpo/source_level_mechanisms.md",
            "knowledge/support/dpo/openroad_native_handoff.md",
            "knowledge/index/skill_cards.jsonl",
            "knowledge/skills/README.md",
            "scripts/repo/query_knowledge.py",
            LEGALIZE_IMPROVE_SURFACE["file"],
            "tools/OpenROAD/src/dpl_evolve/src/Optdp.cpp",
            "tools/OpenROAD/src/dpl_evolve/src/LegalmGuidance.cpp",
            "tools/OpenROAD/src/dpl_evolve/src/LegalmFullLegalization.cpp",
            "tools/OpenROAD/src/dpl_evolve/src/LegalmRowAssignment.cpp",
            "tools/OpenROAD/src/dpl_evolve/src/optimization/detailed_manager.cxx",
            "tools/OpenROAD/src/dpl_evolve/src/objective/detailed_hpwl.cxx",
            "family_variants/REFERENCE_INDEX.yaml",
        ],
        "allowed_actions": [
            "Edit only `tools/OpenROAD/src/dpl_evolve` in a prepared private variant source.",
            "Implement coherent flow changes across legalization/detailed placement, improve placement, and their shared handoff.",
            "Use reference algorithms to implement bounded legalize and improve mechanisms; avoid one-stage-only attempts.",
            "Use prepared start-kind seed branches when assigned; start-point switches are branch-only inside the private source tree.",
            "Implement selected bounded DPO mechanisms directly in the private `dpl_evolve` source; do not rely on code transplant.",
            "Use LEGO-lite skill records to select stage mechanisms and done criteria; do not bulk-load unrelated cards.",
            "Add or refine `EvolveTelemetry` counters that explain stage behavior.",
        ],
        "forbidden_actions": [
            "Do not edit classic `tools/OpenROAD/src/dpl` as part of an evolve attempt.",
            "Do not edit evaluator, scoring, baseline Tcl, or benchmark selection.",
            "Do not introduce fallback-to-default as a success path.",
            "Do not optimize only for one benchmark name; feature-diverse full-flow cases are promotion gates.",
        ],
        "hard_gates": [
            "Private OpenROAD binary builds successfully through lightweight relink.",
            "`detailed_placement_evolve` remains the primary evolved placement command.",
            "Strict evaluator reports legal placement.",
            "Use `metrics.json:hpwl` or `hpwl_openroad_log` as canonical HPWL; `hpwl_proxy` is debug-only.",
            "Telemetry identifies which stages ran and whether repair/polish was needed.",
            "Runtime must remain acceptable for the case, with controlled complexity and clear HPWL or algorithmic evidence when it is slower than baseline.",
        ],
        "required_final_note": [
            "Touched files and stage owner.",
            "Mechanism implemented and reference mechanism used.",
            "Selected LEGO-lite stage skills and whether their done criteria were met.",
            "Legalization/detailed-placement change, improve-placement change, and any shared handoff used between them.",
            "Build command and private binary path.",
            "Canonical result paths for every evaluated case.",
            "Telemetry snippet, expected complexity, and remaining failure mode.",
        ],
    }

    out_dir.mkdir(parents=True, exist_ok=True)
    (out_dir / "current_run_packet.json").write_text(
        json.dumps(packet, indent=2, sort_keys=True) + "\n",
        encoding="utf-8",
    )
    (out_dir / "current_run_packet.md").write_text(
        packet_markdown(packet),
        encoding="utf-8",
    )
    return packet


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--problem_dir", type=Path, required=True)
    parser.add_argument("--out_dir", type=Path, required=True)
    args = parser.parse_args()
    packet = build_packet(args.problem_dir.resolve(), args.out_dir.resolve())
    print(json.dumps({"packet": str(args.out_dir / "current_run_packet.md"), "type": packet["packet_type"]}))


if __name__ == "__main__":
    main()
