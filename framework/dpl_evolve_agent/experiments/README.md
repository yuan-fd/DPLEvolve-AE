# Experiments

This directory contains experiment-specific launchers and workbench generators.
They are intentionally separate from `scripts/`, which is reserved for generic
framework helpers used by Teacher/Student agents and reusable validation flows.

## Launchers

`experiments/launchers/` contains fixed campaign drivers such as 8/9-case BO
sweeps, 9-case evolve sweeps, and historical knowledge-collection pipelines.
These scripts may encode a particular case set, flow variant, concurrency
policy, or paper-experiment setup.

Current launchers:

- `run_aggressive_flow_knowledge_experiments.sh`: historical flow-knowledge
  batch driver from a TSV plan.
- `run_jpeg_util60_discovery_then_matrix.sh`: JPEG util60 discovery plus
  fixed-candidate matrix pipeline.
- `run_openroad_dpl_9case_baselines.sh`: 9-case OpenROAD DPL baseline batch.
- `run_bo_8case_openroad_dpl.sh`: 8-case OpenROAD native DPL BO sweep.
- `run_bo_9case_openroad_dpl.sh`: 9-case OpenROAD native DPL BO sweep.
- `run_evolve_9case_place_batch.sh`: 9-case Teacher/Student evolve sweep.
- `run_evolve_9case_breadth16x4_place_batch.sh`: wider 9-case evolve campaign
  with breadth-16/iteration-4 settings.
- `run_best_evolved_9case_transfer_matrix.sh`: best-evolved source transfer
  matrix launcher for cross-case reuse checks.

## Analysis

`experiments/analysis/` contains campaign-specific reducers and materializers,
for example best-evolved source materialization and transfer-matrix summaries.
These scripts may assume a particular experiment artifact layout; generic
status, comparison, and export helpers belong under `scripts/analysis/`.

## Workbenches

`experiments/workbenches/` contains one-purpose setup scripts for isolated
repair or validation work. Workbench scripts should generate their own ignored
runtime directory under `DPL_EVOLVE_STATE_ROOT` and should not be treated as
general Teacher/Student helper commands.
