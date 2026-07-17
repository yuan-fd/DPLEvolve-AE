# Expected Results

This document defines the expected numerical results for each experiment and
how to judge whether a reproduction is successful.

---

## AES Smoke Test (make smoke)

### Hard Gates (must match exactly or within tolerance)

| Metric | Expected Value | Tolerance | Type |
|---|---|---|---|
| Input ODB SHA-256 | `d8e58c24...` | Exact | checksum |
| Instance count | 14,676 | Exact | integer |
| Instance area | 18,648.20 µm² | ±0.05 | float |
| Global HPWL | 188,569.2 µm | ±0.05 | float |
| Final HPWL | 176,845.1 µm | ±0.05 | float |
| Placement violations | (none) | Exact | string |
| Metric errors | 0 | Exact | integer |
| Legalize exit status | 0 | Exact | integer |

### Soft Indicators (informational, not pass/fail)

| Metric | Typical Range | Notes |
|---|---|---|
| Runtime | 10–20 seconds | Machine-dependent |
| OpenROAD warnings | 0–5 | ORD-0012 is benign |

### Pass/Fail Logic

```
Pass = ALL hard gates satisfied
     AND input ODB checksum matches
     AND final HPWL within tolerance

Conditional pass = ALL gates satisfied
     BUT input checksum differs (different compiler/linker producing same design)
     → Requires manual review of instance count and area

Fail = ANY hard gate violated
     OR metrics.json missing
     OR legalize exit status != 0
```

---

## Baseline Reproduction (make reproduce-baseline)

### AES Nangate45 (reference values)

| Baseline Line | Final HPWL (µm) | Notes |
|---|---|---|
| openroad_dpl_flow | 176,845.1 | Exact match expected for pinned versions |
| openroad_dpl_negotiation | ~177,589.1 | Current-build value (old: 176,083.1) |
| evolve_default | ~176,886.8 | Current-build value (old: 178,566.1) |

**Important**: The negotiation and evolve_default lines depend on DPLEvolve
overlay behavior which has evolved since the paper submission. Exact match
with the old reference values is not expected. The current-build values above
are the correct reference for the current code state.

### Other Cases

For cases other than AES, the expected values are recorded in the reference
results directory (`results/reference/`). These are verified by the baseline
suite runner.

---

## Main HPWL Results (make reproduce-main)

### Paper Claims to Verify

| Claim | Expected | Tolerance | Notes |
|---|---|---|---|
| ReviewDSE avg HPWL improvement | 1.78% | ±0.5 pp | Across 9 cases |
| Black-box BO avg HPWL improvement | 0.38% | ±0.3 pp | Across 9 cases |
| Constraint repair success | 9/9 cases | — | Binary per-case |

### How HPWL Improvement is Calculated

```
improvement = (baseline_hpwl - optimized_hpwl) / baseline_hpwl × 100%
```

Where `baseline_hpwl` is the `openroad_dpl_flow` final HPWL for each case.

### Statistical Validity

The paper reports mean values across cases. For reproduction:
- Single-run results may differ from paper means
- Multi-seed runs (3–5 seeds) are recommended for statistical comparison
- Report both mean ± std and per-case values

---

## Understanding Deviations

### Acceptable Deviations

1. **Runtime differences** (±50%): Machine-dependent, not a pass/fail criterion
2. **Binary hash differences**: Different compiler/linker produce different
   binaries; expected
3. **Small floating-point differences** (< 0.05 µm): Rounding behavior across
   machines
4. **DSE result variation**: LLM-driven search is inherently non-deterministic

### Unacceptable Deviations (Indicate Problems)

1. **Input ODB checksum mismatch with different instance count**: Wrong Yosys
   version
2. **Large HPWL difference** (> 1%): Different OpenROAD version, different
   input, or build issue
3. **Placement violations in baseline**: Broken environment or wrong input
4. **Missing metrics.json**: Script failure or disk issue

---

## Reference vs Reproduced

| Directory | Contents | Modified By |
|---|---|---|
| `results/reference/` | Paper's published results | Never (read-only) |
| `results/reproduced/` | Your reproduced results | make reproduce-* |
| `results/tables/` | Generated tables and CSVs | make table-* |

Always compare `reproduced/` against `reference/`, not against the paper
directly.
