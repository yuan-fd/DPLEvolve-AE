# Experiments Guide

This document describes each experiment, its purpose, command, expected
runtime, and how to interpret the output.

---

## Experiment Hierarchy

```
make smoke                    (1 case, 1 line, 5 min)
  └─ make reproduce-baseline  (5-9 cases, 3 lines, 30 min)
       └─ make reproduce-main (9 cases, full DSE, hours–days)
            ├─ make table-1
            ├─ make table-2
            └─ make table-3
```

---

## Experiment 1: AES Smoke Test

**Purpose**: Validate the complete pipeline without LLM API calls.

**Command**: `make smoke`

**What it runs**:
1. Synthesis: RTL → gate-level netlist (pinned Yosys 0.64)
2. Floorplan + Global Placement (ORFS)
3. Detailed Placement: OpenROAD `detailed_placement` (no DSE)
4. Metric extraction: HPWL, instance count, area, violations

**Cases**: 1 (aes_nangate45)
**Baseline lines**: 1 (openroad_dpl_flow)
**API calls**: 0
**Expected time**: ~5 minutes
**Expected output**:
```
[OK] AES smoke test PASSED
```

**Success criteria** (hard gates):
- Input ODB SHA-256 matches reference
- 14,676 instances
- 18,648.2 µm² instance area
- Global HPWL: 188,569.2 µm
- Final HPWL: 176,845.1 µm
- Zero placement violations

---

## Experiment 2: Baseline Reproduction

**Purpose**: Reproduce the three canonical baseline lines on all paper cases.

**Command**: `make reproduce-baseline`

**Baseline lines**:
1. `openroad_dpl_flow` — Native OpenROAD detailed placement
2. `openroad_dpl_negotiation` — OpenROAD with negotiation flag
3. `evolve_default` — DPLEvolve framework default command

**Cases**: 5–9 (configurable in `configs/paper/baseline_9case.yaml`)
**API calls**: 0
**Expected time**: ~5 min/case, ~30 min total for 5 cases
**Expected output**: `suite_runs.tsv` with HPWL for each case × line combination

**Success criteria**:
- All three lines complete with exit code 0
- OpenROAD default HPWL exactly matches reference (for aes_nangate45)
- No placement violations in any run

---

## Experiment 3: Main Paper Results (Level 2 DSE)

**Purpose**: Reproduce the paper's core claim — ReviewDSE achieves 1.78%
average HPWL improvement vs 0.38% for black-box BO.

**Command**: `make reproduce-main`

**Prerequisites**:
1. `make reproduce-baseline` must pass
2. LLM API access must be configured:
   ```bash
   export ANTHROPIC_API_KEY=...
   # or configure in dpl_evolve_agent/env.sh
   ```

**What it runs**:
1. Level 1: Calibration warm-up (pre-builds mechanism database)
2. Level 2: Teacher-Student multi-agent DSE loop
   - Multiple Student agents explore source-code branches
   - Teacher agent reviews and filters results
   - Iterative rounds until convergence or budget exhaustion

**Cases**: 9 (5 Nangate45 + 4 ASAP7)
**API calls**: Yes — thousands per case
**Expected time**: Hours to days depending on search breadth
**Token budget**: ~2.15B tokens per design (paper claim)

**Warning**: This experiment is **expensive**. The paper reports ~2.15B tokens
per design. Ensure you understand the cost before running.

---

## Experiment 4: Black-Box BO Baseline

**Purpose**: Reproduce the black-box Bayesian optimization baseline for
comparison.

**Command**: `make reproduce-bo` (or via configs)

**What it runs**:
- Traditional BO over OpenROAD detailed-placement parameters
- No source-code modification
- Same evaluation budget as ReviewDSE for fair comparison

**Expected result**: ~0.38% HPWL improvement (paper claim)

---

## Experiment Configuration

All experiments are configured via YAML files in `configs/`:

| Config | Experiment |
|---|---|
| `configs/smoke/aes_nangate45.yaml` | AES smoke test |
| `configs/paper/baseline_9case.yaml` | Baseline reproduction |
| `configs/paper/bo_search.yaml` | Black-box BO |
| `configs/paper/evolve_search.yaml` | ReviewDSE Level 2 |
| `configs/ablation/` | Ablation studies (post-deadline) |

---

## Running Custom Experiments

1. Copy an existing config from `configs/paper/`
2. Modify the cases, lines, or parameters
3. Run via the appropriate script in `scripts/human/`
4. Results land in `results/reproduced/<experiment_id>/`

---

## Experiment Dependencies

```
Yosys build ──→ Input synthesis ──→ Baseline runs ──→ DSE runs
                     │                    │                │
                     ▼                    ▼                ▼
              ODB checksum          suite_runs.tsv    evolve_results/
              Instance count        metrics.json      bo_results/
              Global HPWL           baseline tsv
```
