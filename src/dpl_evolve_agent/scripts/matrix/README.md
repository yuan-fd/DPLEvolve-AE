# Matrix Scripts

This directory owns fixed-source cross-case replay.

The normal DSE flow is:

1. run one Teacher/Student discovery round on a target case;
2. export committed Student source revisions from that round;
3. build each fixed source once;
4. evaluate that exact binary across every enabled row in a TSV plan.

## Scripts

- `run_candidate_matrix.sh`: build one candidate source once, then evaluate it
  across a plan.
- `run_round_candidate_matrices.sh`: export committed Student sources from a
  Teacher round, then launch one candidate matrix per source.

Use `scripts/workspace/` for build/relink actions, `scripts/evaluator/` for
flow and metrics helpers, and `scripts/analysis/` for source export/reporting.

## Baseline-Only Matrix

Use this when preparing a plan from a clean ORFS workspace:

```bash
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/full_cross_case_core_util.tsv"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_candidate_matrix.sh" \
  --matrix-id full_baselines_$(date +%Y%m%d_%H%M%S) \
  --plan "$PLAN" \
  --threads 10 \
  --max-parallel 6 \
  --baseline-only
```

For each plan row this creates or reuses the placement snapshot, then runs the
canonical baseline suite.

## One Candidate

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_candidate_matrix.sh" \
  --matrix-id candidate_probe_$(date +%Y%m%d_%H%M%S) \
  --candidate-src "$DPL_EVOLVE_STATE_ROOT/exported_candidate/dpl_evolve" \
  --candidate-label candidate_probe \
  --plan "$PLAN" \
  --threads 10 \
  --max-parallel 6
```

## All Candidates From One Round

```bash
export ROUND_ID="<completed-round-id>"
export MATRIX_PREFIX="${ROUND_ID}_full_matrix_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh" \
  --round-id "$ROUND_ID" \
  --plan "$PLAN" \
  --matrix-prefix "$MATRIX_PREFIX" \
  --threads 10 \
  --matrix-row-parallel 6
```

Do not pass `--skip-baseline-matrix` on a first real run.  Add it only when the
same plan snapshots and canonical baselines are already complete.

For a real one-candidate smoke:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh" \
  --round-id "$ROUND_ID" \
  --plan "$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/smoke_dse.tsv" \
  --matrix-prefix "${ROUND_ID}_smoke_matrix_$(date +%Y%m%d_%H%M%S)" \
  --threads 8 \
  --matrix-row-parallel 1 \
  --student student_01 \
  --iteration iter_01 \
  --limit 1
```

Rerun with the same `--matrix-prefix` to resume.  Completed candidate matrices
are detected from `results.tsv` and skipped.
