# Validate AES Smoke

## Objective

Establish that the pinned AES/Nangate45 input and native OpenROAD detailed
placement baseline remain reproducible without invoking an LLM.

## Procedure

1. Run `scripts/ae/check_environment.sh`.
2. If the reference artifacts exist, run
   `scripts/ae/run_aes_smoke.sh --check-only`.
3. Run a fresh test only when explicitly requested, using
   `scripts/ae/run_aes_smoke.sh --run --threads <N>`.
4. Preserve the generated timestamped flow variant.
5. Report environment warnings separately from validation failures.

## Required Report Fields

```text
mode
flow_variant
run_tag
ORFS revision
OpenROAD revision
Yosys revision
input ODB SHA-256
instance count and area
global and final HPWL
placement violations
metric error count
exit status
metrics.json path
```

Do not claim reproduction of the paper's aggregate result from this smoke test.
