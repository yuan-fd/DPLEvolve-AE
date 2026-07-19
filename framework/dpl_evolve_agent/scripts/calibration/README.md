# Calibration Scripts

This directory contains calibration-adjacent script implementations.

Current scripts:

- `calibrate_start_seeds.sh`: target-local start-kind probe for prepared
  source basins before a Teacher/Student round.
- `summarize_start_seed_calibration.py`: summarizer for that target-local
  probe.

Mechanism calibration uses the normal Teacher/Student entrypoint with the
calibration Teacher prompt:

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

`--calibration-mode` is a single-iteration mechanism sweep.  Scale mechanism
count with `--children`, not with `--iterations`; Teacher assigns one distinct
mechanism per child and reviews results as calibration evidence.

Raw mechanism-sweep artifacts are round artifacts:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
```

Call these scripts through their canonical namespace:

```text
$DPL_EVOLVE_AGENT_ROOT/scripts/calibration/calibrate_start_seeds.sh
$DPL_EVOLVE_AGENT_ROOT/scripts/calibration/summarize_start_seed_calibration.py
```

Paper-level ReviewDSE Level 1 calibration contracts live under:

```text
$DPL_EVOLVE_AGENT_ROOT/calibration/
```

Generated paper-level diagnostic archives that are not one target round should
use:

```text
$DPL_EVOLVE_STATE_ROOT/calibrations/<calibration_id>/
```

Human-reviewed Level 1 summaries and source-start provenance stay tracked under
`$DPL_EVOLVE_AGENT_ROOT/calibration/`.
