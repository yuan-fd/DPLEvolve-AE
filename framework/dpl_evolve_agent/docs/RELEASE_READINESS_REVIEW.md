# Release Readiness Review

This is the working review/action/build-test checklist for making the repo
usable by another operator without private context.

## Current Runtime Status

- Stable entrypoints remain at `scripts/optimize_case_with_codex.py` and
  `scripts/run_codex_exec.py`.
- Runtime roots are environment-driven: `DPL_EVOLVE_AGENT_ROOT`, `ORFS_ROOT`,
  and `DPL_EVOLVE_STATE_ROOT`.
- Teacher loop implementation is split by role:
  - `scripts/teacher_loop/orchestrator.py`: sequencing only.
  - `scripts/teacher_loop/context_packets.py`: generated context and route
    insight packets.
  - `scripts/teacher_loop/packet_builders.py`: generated packet/README files.
  - `scripts/teacher_loop/prompt_rendering.py`: prompt text, prompt audit, and
    Teacher/Student prompt constructors.
  - `scripts/teacher_loop/workspace_scripts.py`: start-kind/workspace policy.
- Runtime split status: Phase 2 is complete. Legacy
  `scripts/teacher_loop/prompts.py` is now only a compatibility import module.
- Script namespace status: Phase 3 is complete for workspace, evaluator,
  matrix, analysis, calibration, BO, repo-contract Python helpers, and
  orchestration observability helpers. Old top-level wrappers were removed.
  `optimize_case_with_codex.py` and `run_codex_exec.py` remain stable top-level
  shims. `runtime_env.sh` intentionally remains top-level because shell callers
  source it.
- Knowledge taxonomy status: Phase 4 is complete for policy, routing,
  contract, algorithm, support, and reference directories.

## Script Namespace Review

Implementation paths should follow this ownership map:

| Target Namespace | Files To Move Or Wrap | Reason |
| --- | --- | --- |
| `scripts/orchestration/` | `monitor_evolve_heartbeat.sh`, `watch_teacher_round.py`, `check_evolve_now.py` | Human/runtime observability and status tools. |
| `scripts/workspace/` | `prepare_workspace.sh`, `create_variant_start.sh`, `configure_openroad_core.sh`, `build_openroad_core.sh`, `configure_openroad_variant_relink.sh`, `build_openroad_variant_relink.py`, `build_openroad_only.sh` | Workspace/bootstrap/build ownership. |
| `scripts/evaluator/` | `run_canonical_line.sh`, `run_place_batch.sh`, `report_stage_metrics.py`, `report_candidate_metrics.py`, `normalize_candidate_matrix_results.py` | Flow execution and metrics normalization. |
| `scripts/analysis/` | `compare_runs.py`, `compact_round_peer_prompts.py`, `report_experiment_status.py`, `report_experiment_quick_status.py`, `export_round_candidate_sources.py`, `fetch_reference_papers.sh` | Human-facing reports, compaction, exports, and reference material fetches. |
| `scripts/matrix/` | `run_candidate_matrix.sh`, `run_round_candidate_matrices.sh` | Cross-case candidate validation and matrix execution. |
| `scripts/bo/` | `bo_tune_case.py`, `setup_raytune_venv.sh` | Black-box tuning and BO environment setup. |
| `scripts/calibration/` | `calibrate_start_seeds.sh`, `summarize_start_seed_calibration.py` | Target-local probes and future paper-level calibration helpers. |
| `scripts/repo/` | `audit_repo_hygiene.py`, `checkpoint.py`, `case_registry.py`, `query_knowledge.py`, `check_release_readiness.sh` | Repo contracts, release gates, case metadata, and knowledge query support. |

## Naming Rules

- Top-level `scripts/` is restricted to `README.md`, `runtime_env.sh`,
  `optimize_case_with_codex.py`, and `run_codex_exec.py`.
- Implementation files should live under a role namespace.
- Use verb_object naming for runnable scripts, for example
  `run_candidate_matrix.sh`, `report_candidate_metrics.py`, or
  `configure_openroad_variant_relink.sh`.
- Use noun-role names for Python implementation modules, for example
  `context_packets.py`, `packet_builders.py`, and `prompt_rendering.py`.
- Avoid overloaded terms:
  - "calibration" is paper-level Level 1 calibration under `calibration/`.
  - target-local start-kind checking is a "probe".
  - generated Student shell files are "workspace helper scripts".

## Build And Test Gate

Before calling a refactor release-ready, run:

```bash
scripts/repo/check_release_readiness.sh
```

## Remaining Release Blockers

- Phase 3 namespace migration is complete for `scripts/workspace/`,
  `scripts/evaluator/`, `scripts/matrix/`, `scripts/analysis/`,
  `scripts/calibration/`, `scripts/bo/`, and repo-contract Python helpers
  under `scripts/repo/`, plus orchestration observability helpers under
  `scripts/orchestration/`. The two active launcher/recorder shims remain
  top-level stable entrypoints.
- Internal launchers, skills, generated helpers, and user-facing docs should
  use canonical role-namespace implementation paths.
- Phase 4 taxonomy is complete: default route knowledge lives under
  `knowledge/routing/`, policy/contracts under `knowledge/policies/` and
  `knowledge/contracts/`, on-demand support under `knowledge/support/`,
  and paper-derived cards under `knowledge/algorithms/`.
- Run one non-dry-run smoke case only after the local ORFS/OpenROAD workspace
  is confirmed available and baselines are prepared.
