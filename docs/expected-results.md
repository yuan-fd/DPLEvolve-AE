# Expected Results & Tolerances

This document records the expected values and acceptable tolerances for
each paper claim verified by this artifact. The JSON files under
`artifacts/*/expected/` are the authoritative reference; this document
provides context and interpretation guidance.

## Table 4: QoR Comparison

**Reference:** `artifacts/01-table4-qor/expected/table4.json`
**Paper claims:** `artifacts/01-table4-qor/expected/paper_claims.json`

| Metric | BO-DSE (Baseline) | ReviewDSE-HPWL | ReviewDSE-GHR |
|--------|-------------------|----------------|---------------|
| Mean HPWL reduction | ~0.38% | ~1.78% | N/A |
| Global route overflow reduction | N/A | N/A | ~1.68% |
| Runtime ratio vs baseline | 1.0× | ~1.34× | ~1.11× |

Tolerance: ±0.05 percentage points for HPWL/GHR metrics.
Smaller deviations are expected across different hardware due to
floating-point variations in OpenROAD's internal calculations.

## Table 5: Composability

**Reference:** `artifacts/02-table5-composability/expected/`

Three counterexamples are verified. Each produces a pass/fail verdict
against the expected composability property. All three must pass.

## Table 6: Cut-Row Repair Patterns

**Reference:** `artifacts/03-table6-cutrow/expected/`

Nine pattern verification results. Each is a boolean pass/fail check.
All nine must pass.

## AES Smoke Flow

**Reference:** `artifacts/04-aes-smoke/expected/ae_reproduction_lock.json`

The lock file contains exact expected values for:

- Final WNS (worst negative slack)
- Final TNS (total negative slack)
- Die area
- Design area
- Total wirelength

Tolerances are specified per-metric in the lock file. Values are
generally expected to match within 1% on identical tool commits.
Larger deviations indicate either a different tool version or a
build configuration mismatch.

## Interpreting Deviations

Small deviations are normal and expected because:

1. **Compiler differences** between machines can produce slightly
   different binaries from the same source.
2. **Floating-point non-determinism** in EDA tools.
3. **System library version differences** (glibc, etc.).

If a deviation exceeds the stated tolerance:

1. Verify the tool commits match `provenance/source-commits.json`.
2. Run `make check` to confirm your environment.
3. Check `docs/troubleshooting.md` for known issues.
