# Experiment Plans

This directory contains TSV plans for cross-case/core-utilization DSE replay.
A plan says which placement snapshots and baseline rows must exist before a
fixed Student source is judged transferable.

Use these plans after a Teacher/Student discovery round.  The generic matrix
scripts are the recommended path:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_candidate_matrix.sh"
"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh"
```

Historical campaign launchers under `experiments/launchers/` may still encode
paper-specific defaults, but new reproducible runs should start from the matrix
scripts above.

## Plans

- `smoke_dse.tsv`: one AES row for validating the replay flow quickly.
- `flow_knowledge_core_util.tsv`: five-row transfer plan for faster
  mechanism-transfer checks.
- `full_cross_case_core_util.tsv`: nine-row full DSE matrix across Nangate45,
  ASAP7, JPEG utilization sweeps, and larger transfer probes.

## TSV Format

Rows are tab separated:

```text
enabled case core_utilization flow_variant round_id start_kind notes
```

- `enabled`: `1` to run, `0`/`false`/`skip` to ignore.
- `case`: problem id under `problems/`.
- `core_utilization`: integer value, `default`, or `-`.
- `flow_variant`: ORFS placement snapshot name.
- `round_id`: human-stable experiment label for launchers and reports.
- `start_kind`: discovery source-start hint, usually `framework`.
- `notes`: short human intent for the row.

The fixed-candidate matrix uses `case`, `core_utilization`, and `flow_variant`
for execution.  The remaining columns keep the plan readable and compatible
with historical campaign launchers.

## Baseline Matrix

Build or reuse snapshots and run the three canonical baselines for every
enabled row:

```bash
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/full_cross_case_core_util.tsv"
export MATRIX_ID="full_baselines_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_candidate_matrix.sh" \
  --matrix-id "$MATRIX_ID" \
  --plan "$PLAN" \
  --threads 10 \
  --max-parallel 6 \
  --baseline-only
```

Use `--force-place` only when you intentionally want to regenerate placement
snapshots.  Use `--skip-place` only when all plan snapshots already exist.

## One Fixed Candidate

Test one exported or manually prepared `dpl_evolve` source across the plan:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_candidate_matrix.sh" \
  --matrix-id candidate_probe_$(date +%Y%m%d_%H%M%S) \
  --candidate-src "$DPL_EVOLVE_STATE_ROOT/some_exported_candidate/dpl_evolve" \
  --candidate-label candidate_probe \
  --plan "$PLAN" \
  --threads 10 \
  --max-parallel 6
```

This builds one private OpenROAD binary and reuses it for every enabled plan
row.  That is the key matrix rule: code is fixed while design/utilization
changes.

## All Candidates From A Round

After a discovery round, export each committed Student source revision and run
one fixed-source matrix per candidate:

```bash
export ROUND_ID="<completed-round-id>"
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/full_cross_case_core_util.tsv"
export MATRIX_PREFIX="${ROUND_ID}_full_matrix_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh" \
  --round-id "$ROUND_ID" \
  --plan "$PLAN" \
  --matrix-prefix "$MATRIX_PREFIX" \
  --threads 10 \
  --matrix-row-parallel 6
```

The wrapper runs a baseline matrix first unless `--skip-baseline-matrix` is
given.  For a first real smoke, keep baselines enabled and add:

```bash
--student student_01 --iteration iter_01 --limit 1
```

Rerun the same command with the same `--matrix-prefix` to resume.  Complete
candidate matrices are skipped automatically.

Outputs are written under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrix_batches/<matrix_prefix>/candidates.tsv
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrices/<matrix_id>/results.tsv
```
