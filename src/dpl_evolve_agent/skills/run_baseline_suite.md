# Skill: run_baseline_suite

## Purpose
Run the full baseline matrix and one evolve line to compare the candidate legalizer against fixed references.

## Expected local entrypoint
`$DPL_EVOLVE_AGENT_ROOT/baseline/run_baseline_suite.sh`

If the run is agent-managed rather than manual, prefer calling it through:

- `$DPL_EVOLVE_AGENT_ROOT/adapters/science_codeevolve/evaluator.py --mode suite`

## What this should cover
- OpenROAD DPL flow: `detailed_placement`, `improve_placement`, `optimize_mirroring`
- current evolve line

DREAMPlace/Abacus remains a donor/reference mechanism, not part of the default
baseline suite.

## Procedure
1. Move to ORFS repo root.
2. Confirm the baseline runner exists.
3. Read the script header / usage block if arguments are uncertain.
4. Launch a suite run with a unique run tag or reuse an existing `FLOW_VARIANT`
   when you only need to reuse upstream ORFS snapshots such as
   `3_4_place_resized.odb`.
5. Wait for completion.
6. Locate artifact outputs under:
   - `flow/results/.../dpl_evolve_baseline/<run_tag>/`
   - `flow/reports/.../dpl_evolve_baseline/<run_tag>/`
   - `flow/logs/.../dpl_evolve_baseline/<run_tag>/`
7. Read consolidated `metrics.json`.

## Required outputs
- run tag
- artifact root
- metrics file path
- per-baseline summary table
- failure line if any sub-baseline fails

## Example

```bash
"$DPL_EVOLVE_AGENT_ROOT/baseline/run_baseline_suite.sh" \
  --case <case_id> \
  --flow-variant <FLOW_VARIANT_WITH_3_4_PLACE_RESIZED_ODB> \
  --tag-prefix agent_suite \
  --threads 10
```

## Notes for Codex
Use suite runs sparingly during patch debugging.
For iterative patching, prefer `run_single_baseline.md`.
The suite summary for this control plane is:

- `flow/reports/<platform>/<design>/<flow_variant>/dpl_evolve_baseline/suite_runs.tsv`
