# Task: Reproduce Baseline Results

## Goal
Run the three canonical baseline lines on all paper cases and validate results.

## API Required
No

## Estimated Time
~30 minutes

---

## Pre-Flight

1. Run `make check` — confirm all critical checks pass
2. Verify Yosys: `$YOSYS_EXE -V | grep 8449dd470`
3. Verify OpenROAD: `$OPENROAD_EXE -version`
4. Record provenance: `make provenance`
5. Confirm AES input ODB checksum matches reference

---

## Execution Steps

### Step 1: Run the smoke test first
```bash
bash scripts/human/smoke_test.sh --run --threads 8
```
This validates the minimal pipeline on AES. If it fails, stop and diagnose.
Do not proceed to the full baseline suite.

### Step 2: Run the full baseline suite
```bash
bash scripts/human/reproduce_baseline.sh
```
This runs all 9 cases × 3 lines = 27 runs.

### Step 3: Collect and validate results
```bash
bash scripts/agent/validate_run.sh --experiment baseline
```

### Step 4: Generate Table 1
```bash
make table-1
```

---

## Validation Checklist

- [ ] AES openroad_dpl_flow HPWL: 176845.1 ± 0.05
- [ ] All 27 runs exit code 0
- [ ] Zero placement violations in all runs
- [ ] metadata.json present for every run
- [ ] suite_runs.tsv generated with all 3 lines
- [ ] No hidden ERROR/FATAL in any log

---

## Expected Output Files

```
flow/reports/nangate45/aes/<variant>/dpl_evolve_baseline/<tag>/metrics.json
flow/reports/nangate45/aes/<variant>/dpl_evolve_baseline/<tag>/suite_runs.tsv
flow/reports/nangate45/aes/<variant>/dpl_evolve_baseline/<tag>/legalize_summary.json
```

## Failure Handling

If any case fails:
1. Check that the input ODB exists and has correct checksum
2. Check the OpenROAD log for the failing case
3. Check disk space
4. Do NOT skip the failed case — all cases must pass or the task is incomplete
