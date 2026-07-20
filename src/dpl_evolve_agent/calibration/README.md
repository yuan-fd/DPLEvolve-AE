# Calibration

This directory is the tracked contract for ReviewDSE Level 1 calibration.
It is distinct from the target-local start-kind probe that writes
`$DPL_EVOLVE_STATE_ROOT/<round_id>/start_seed_calibration/`.

Use `$DPL_EVOLVE_AGENT_ROOT`, `$ORFS_ROOT`, and `$DPL_EVOLVE_STATE_ROOT` for
all directory prefixes in calibration plans, reports, scripts, and generated
packets.

Level 1 calibration builds frozen method evidence and source-start provenance
from diagnostic calibration designs before target-case Teacher/Student search.
Target search may consume this frozen evidence as prior guidance, but it must
not update global Level 1 records.

## Directory Roles

- `case_sets/`: calibration-only case-set definitions and target-disjointness
  notes.
- `plans/`: reusable calibration run plans.
- `evidence/`: reviewed mechanism evidence records and compact summaries.
- `source_starts/`: source-start branch specifications and provenance.
- `schemas/`: JSON schemas for calibration manifests, evidence records, and
  source-start specs.
- `reports/`: frozen human-readable summaries for Teacher initialization.

Generated mechanism sweeps launched with `--calibration-mode` are normal
Teacher/Student rounds. Their raw prompts, Student workspaces, reviews, and
candidate artifacts stay under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
```

After human review, compact frozen Level 1 summaries belong in this tracked
`calibration/` tree. If a paper-level diagnostic archive needs a generated
state copy that is not tied to one target round, use:

```text
$DPL_EVOLVE_STATE_ROOT/calibrations/<calibration_id>/
```

## Run Mechanism Calibration

For day-to-day use, run mechanism calibration through the normal
Teacher/Student launcher with `--calibration-mode`.  Prepare ORFS, build the
shared core, and create the target placement snapshot first as described in the
top-level `README.md`.

Small prompt-only check:

```bash
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id calib_dryrun_aes_$(date +%Y%m%d_%H%M%S) \
  --iterations 1 \
  --children 5 \
  --max-parallel 2 \
  --calibration-mode \
  --skip-baseline-preflight \
  --skip-core-build \
  --dry-run \
  --audit-prompts
```

Real breadth calibration:

```bash
export ROUND_ID="calib_aes_50x10_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id "$ROUND_ID" \
  --start-kind framework \
  --iterations 1 \
  --children 50 \
  --max-parallel 10 \
  --threads 8 \
  --calibration-mode \
  --calibrate-start-seeds \
  --skip-core-build \
  --audit-prompts \
  --launch
```

Calibration breadth is controlled by `--children`; keep `--iterations 1`.
Launched calibration should keep real baselines enabled unless you are doing a
prompt-only dry run.

## Evidence Rules

- Calibration designs are diagnostic probes, not target designs.
- Evidence records describe mechanisms, not whole-patch winners only.
- Source-start specs describe how a source basin was constructed; generated
  source trees stay under the state root.
- Level 2 target search can accumulate target-local evidence, but it does not
  rewrite Level 1 evidence.
