# Scripts

This directory is the active tool layer for `dpl_evolve_agent`.

The default human flow is the top-level `README.md`.  This file is only an
inventory: it explains where scripts belong and which entrypoints are canonical.
Tracked scripts and generated packets must use environment prefixes instead of
machine-local absolute paths:

```text
DPL_EVOLVE_AGENT_ROOT
ORFS_ROOT
DPL_EVOLVE_STATE_ROOT
DPL_EVOLVE_PYTHON
```

Top-level `scripts/` stays intentionally small:

- `runtime_env.sh`: shared shell runtime resolver.
- `optimize_case_with_codex.py`: stable Teacher/Student optimization CLI shim.
- `run_codex_exec.py`: stable Codex worker-recorder CLI shim.

## Canonical Namespaces

| Namespace | Role | Default Use |
| --- | --- | --- |
| `ae/` | Check/setup the pinned AE environment and validate the AES smoke result. | Reviewer and Agent reproduction gate. |
| `workspace/` | Prepare ORFS/OpenROAD, configure/build the common core, create/relink private variants. | Bootstrap and Student build helpers. |
| `evaluator/` | Produce placement snapshots, run canonical lines, summarize metrics. | Baselines, smoke checks, candidate validation. |
| `matrix/` | Replay fixed candidate sources across TSV DSE plans. | Post-round DSE validation. |
| `calibration/` | Target-local start-seed probes and calibration summarizers. | Optional calibration support. |
| `orchestration/` | Watch or summarize long-running Teacher/Student rounds. | Monitoring only. |
| `analysis/` | Export sources and summarize completed or in-flight results. | Human review and reporting. |
| `repo/` | Case registry, knowledge query, hygiene/readiness checks, checkpoints. | Control-plane utilities. |
| `teacher_loop/` | Python implementation modules behind `optimize_case_with_codex.py`. | Internal orchestration code. |
| `codex_exec/` | Python implementation modules behind `run_codex_exec.py`. | Internal Codex execution recorder. |
| `bo/` | Ray Tune black-box parameter tuning. | Advanced comparison path only. |

## Main Entrypoints

Use these for normal work:

- `scripts/ae/check_environment.sh`
- `scripts/ae/setup_user_environment.sh`
- `scripts/ae/run_aes_smoke.sh`
- `scripts/repo/check_release_readiness.sh`
- `scripts/workspace/prepare_workspace.sh`
- `scripts/workspace/configure_openroad_core.sh`
- `scripts/workspace/build_openroad_core.sh`
- `scripts/evaluator/run_place_batch.sh`
- `baseline/run_baseline_suite.sh`
- `scripts/optimize_case_with_codex.py`
- `scripts/matrix/run_candidate_matrix.sh`
- `scripts/matrix/run_round_candidate_matrices.sh`
- `scripts/analysis/report_experiment_status.py`

Student agents should use the generated helper scripts inside their workspace
instead of reconstructing build/evaluation/git commands from memory.

## Calibration

`scripts/calibration/calibrate_start_seeds.sh` is a target-local start-kind
probe used by `--calibrate-start-seeds`.

Mechanism calibration uses the normal Teacher/Student entrypoint:

```text
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case <case_id> \
  --flow-variant <flow_variant> \
  --round-id <calibration_round_id> \
  --iterations 1 \
  --children 50 \
  --max-parallel 10 \
  --calibration-mode \
  --calibrate-start-seeds \
  --launch
```

Raw mechanism-sweep artifacts are normal round artifacts under
`$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/`.

## Advanced Paths

`scripts/bo/` is for black-box OpenROAD native-parameter tuning. It requires a
separate Ray Tune environment from `scripts/bo/setup_raytune_venv.sh` and is
not part of the default evolve flow.

Experiment-specific or historical launchers belong outside `scripts/`:

```text
experiments/launchers/
experiments/workbenches/
experiments/analysis/
```

Treat those launchers as human-only campaign recipes.  Teacher/Student agents
should prefer the top-level README flow, generated workspace helpers, and the
canonical `scripts/matrix/` replay tools.
