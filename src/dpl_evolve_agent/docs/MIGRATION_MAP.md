# Migration Map

This file records the release-readiness namespace migration so reviewers can
distinguish intentional moves from accidental deletions.

## Script Namespaces

The old top-level `scripts/<name>` paths below were removed during the
namespace cleanup. Canonical implementations live under role namespaces.

| Removed Old Path | Canonical Implementation | Owner |
| --- | --- | --- |
| `scripts/prepare_workspace.sh` | `scripts/workspace/prepare_workspace.sh` | Workspace bootstrap |
| `scripts/create_variant_start.sh` | `scripts/workspace/create_variant_start.sh` | Variant source bootstrap |
| `scripts/configure_openroad_core.sh` | `scripts/workspace/configure_openroad_core.sh` | Common-core configure |
| `scripts/build_openroad_core.sh` | `scripts/workspace/build_openroad_core.sh` | Common-core build |
| `scripts/build_openroad_only.sh` | `scripts/workspace/build_openroad_only.sh` | Explicit OpenROAD build |
| `scripts/configure_openroad_variant_relink.sh` | `scripts/workspace/configure_openroad_variant_relink.sh` | Variant configure |
| `scripts/build_openroad_variant_relink.sh` | `scripts/workspace/build_openroad_variant_relink.py` | Variant relink |
| `scripts/build_openroad_variant_relink.py` | `scripts/workspace/build_openroad_variant_relink.py` | Variant relink implementation |
| `scripts/run_canonical_line.sh` | `scripts/evaluator/run_canonical_line.sh` | Canonical line dispatch |
| `scripts/run_place_batch.sh` | `scripts/evaluator/run_place_batch.sh` | Place snapshot generation |
| `scripts/report_stage_metrics.py` | `scripts/evaluator/report_stage_metrics.py` | Stage metrics |
| `scripts/report_candidate_metrics.py` | `scripts/evaluator/report_candidate_metrics.py` | Candidate metrics |
| `scripts/normalize_candidate_matrix_results.py` | `scripts/evaluator/normalize_candidate_matrix_results.py` | Matrix result normalization |
| `scripts/run_candidate_matrix.sh` | `scripts/matrix/run_candidate_matrix.sh` | Fixed-source matrix replay |
| `scripts/run_round_candidate_matrices.sh` | `scripts/matrix/run_round_candidate_matrices.sh` | Round source matrix replay |
| `scripts/compare_runs.py` | `scripts/analysis/compare_runs.py` | Human comparison |
| `scripts/compact_round_peer_prompts.py` | `scripts/analysis/compact_round_peer_prompts.py` | Prompt compaction |
| `scripts/report_experiment_status.py` | `scripts/analysis/report_experiment_status.py` | Launch status |
| `scripts/report_experiment_quick_status.py` | `scripts/analysis/report_experiment_quick_status.py` | Fast batch status |
| `scripts/export_round_candidate_sources.py` | `scripts/analysis/export_round_candidate_sources.py` | Source export |
| `scripts/fetch_reference_papers.sh` | `scripts/analysis/fetch_reference_papers.sh` | Local reference fetch |
| `scripts/bo_tune_case.py` | `scripts/bo/bo_tune_case.py` | Black-box optimization |
| `scripts/setup_raytune_venv.sh` | `scripts/bo/setup_raytune_venv.sh` | BO environment |
| `scripts/calibrate_start_seeds.sh` | `scripts/calibration/calibrate_start_seeds.sh` | Target start-kind probe |
| `scripts/summarize_start_seed_calibration.py` | `scripts/calibration/summarize_start_seed_calibration.py` | Probe summarizer |
| `scripts/calibrate_start_patches.sh` | removed; use `scripts/calibration/calibrate_start_seeds.sh` | Legacy probe alias |
| `scripts/summarize_start_patch_calibration.py` | removed; use `scripts/calibration/summarize_start_seed_calibration.py` | Legacy summarizer alias |
| `scripts/check_release_readiness.sh` | `scripts/repo/check_release_readiness.sh` | Repo release gate |
| `scripts/audit_repo_hygiene.py` | `scripts/repo/audit_repo_hygiene.py` | Repo hygiene audit |
| `scripts/case_registry.py` | `scripts/repo/case_registry.py` | Case registry resolver |
| `scripts/checkpoint.py` | `scripts/repo/checkpoint.py` | Local checkpoint manager |
| `scripts/query_knowledge.py` | `scripts/repo/query_knowledge.py` | Skill-card query and validation |
| `scripts/check_evolve_now.py` | `scripts/orchestration/check_evolve_now.py` | Live experiment status |
| `scripts/monitor_evolve_heartbeat.sh` | `scripts/orchestration/monitor_evolve_heartbeat.sh` | Repeated experiment heartbeat |
| `scripts/watch_teacher_round.py` | `scripts/orchestration/watch_teacher_round.py` | Round dashboard |

## Knowledge Taxonomy

| Old Path | New Path | Role |
| --- | --- | --- |
| `knowledge/EVIDENCE_POLICY.md` | `knowledge/policies/evidence_policy.md` | Evidence classification policy |
| `knowledge/CONTEXT_AUDIT.md` | removed | Historical audit superseded by active contracts and directory READMEs |
| `knowledge/reference/commands.txt` | removed | Historical command snippets removed from active knowledge |
| `knowledge/algorithm_cards/` | `knowledge/algorithms/` | Paper-derived algorithms and pseudocode |
| `knowledge/insights/workflow/` | `knowledge/contracts/` | Metric and workflow contracts |
| `knowledge/insights/case_evolution/case_type_quality_mechanism_priority.md` | `knowledge/routing/case_feature_to_mechanism_route_map.md` | Default case-to-route map |
| `knowledge/insights/case_evolution/mechanism_reconstruction_roadmap.md` | `knowledge/routing/mechanism_reconstruction_roadmap.md` | Selected-route roadmap |
| `knowledge/insights/case_evolution/evidence_weighted_mechanism_priority.md` | `knowledge/routing/evidence_weighted_mechanism_priority.md` | Review-time route priority |
| `knowledge/insights/case_evolution/runtime_light_final_hpwl_mechanism_priority.md` | `knowledge/routing/runtime_light_final_hpwl_mechanism_priority.md` | Runtime-aware route priority |
| `knowledge/insights/case_evolution/*` | `knowledge/support/case_evolution/*` | Support evidence and route background |
| `knowledge/insights/dpo/*` | `knowledge/support/dpo/*` | DPO and handoff support |
| `knowledge/insights/legalization/*` | `knowledge/support/legalization/*` | Legalization support |
| `knowledge/insights/legalm/*` | `knowledge/support/legalm/*` | LEGALM support |
| `knowledge/insights/README.md` | removed | Historical taxonomy note superseded by `knowledge/README.md` and directory READMEs |

## Compatibility Rules

- `scripts/optimize_case_with_codex.py` and `scripts/run_codex_exec.py` are
  intentionally stable top-level shims because their implementations already
  live under `scripts/teacher_loop/` and `scripts/codex_exec/`.
- `scripts/runtime_env.sh` remains top-level because callers source it for
  shell environment mutation; it is not an exec-style wrapper candidate.
- Other top-level script wrappers were removed. New references must use
  canonical role-namespace paths.
- Do not add new runtime references to `knowledge/insights/` or
  `knowledge/algorithm_cards/`.
- Default Teacher routing may read `knowledge/routing/` and contracts; support,
  algorithm, and reference material must be on-demand.
- Machine-local absolute paths belong in `env.sh` or ignored state, not tracked
  files.
