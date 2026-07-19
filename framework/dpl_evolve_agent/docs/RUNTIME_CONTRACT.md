# Runtime Contract

This repo treats runtime outputs as local state and tracked files as the
portable control plane.

## Roots

- `DPL_EVOLVE_AGENT_ROOT`: this repo.
- `ORFS_ROOT`: external OpenROAD-flow-scripts workspace.
- `DPL_EVOLVE_STATE_ROOT`: ignored runtime state root.

The fallback state root is a sibling directory:

```text
$DPL_EVOLVE_AGENT_ROOT/../dpl_evolve_state
```

You may set an external ignored state root when multiple repos share one ORFS
workspace:

```text
/abs/path/to/dpl_evolve_state
```

Tracked scripts, prompt templates, knowledge cards, and generated packets
should refer to directory prefixes through these variables.  Hardcoded
machine-local absolute paths are not part of the portable contract.

## Protected Evaluator

The protected evaluator owns:

- input placement snapshots;
- canonical command sequence;
- baseline wrappers;
- legality and validity checks;
- metric parsing;
- runtime gates;
- source/binary/metric consistency;
- comparison anchors.

Teacher and Student agents must not edit evaluator scripts, ORFS flow scripts,
metric parsers, or baseline wrappers during a source-evolution round.

## Generated State

Target-round generated state belongs under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/
```

Target-round artifacts use:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
$DPL_EVOLVE_STATE_ROOT/<round_id>/checkpoints/operations/
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrices/
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrix_batches/
$DPL_EVOLVE_STATE_ROOT/<round_id>/start_seed_calibration/
```

`start_seed_calibration/` is the legacy runtime directory for the target
start-kind probe. It is target-local initialization evidence, not the global
Level 1 calibration store.

Mechanism calibration launched with `--calibration-mode` is also a target-round
Teacher/Student run and writes raw artifacts under
`$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/`.

Paper-level generated diagnostic archives that are not tied to one target round
may use:

```text
$DPL_EVOLVE_STATE_ROOT/calibrations/<calibration_id>/
```

## Script Layout

Top-level `scripts/` is intentionally small: `runtime_env.sh` plus the stable
`optimize_case_with_codex.py` and `run_codex_exec.py` shims. Other
implementation scripts live under role-specific subdirectories.
