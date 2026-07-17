# Claims to Artifacts Mapping

This file maps every claim in the paper to the specific experiment
configuration, execution command, expected output file, and the
generated table or figure. This is the central traceability document.

---

## Paper Claims

### Claim 1: HPWL Average Improvement (Table 4)

| Field | Value |
|---|---|
| **Paper claim** | ReviewDSE achieves 1.78% average HPWL reduction (ReviewDSE-HPWL) and 1.68% (ReviewDSE-GHR) across 9 benchmarks |
| **Paper location** | Table 4, Section 4.2 |
| **Experiment config** | `configs/paper/evolve_search.yaml` |
| **Execution command** | `make reproduce-main` |
| **Raw result file** | `results/reproduced/main/evolve_9case/suite_results.json` |
| **Reference evidence** | `Agenticflow/local_backups/dpl_evolve_state_experiments_20260525_143505/analysis_exports/20260524_selected_experiment_tables/9case_rerun1_best.tsv` |
| **Generated table** | `results/tables/table_2_main_hpwl.csv` |
| **Make target** | `make table-2` |
| **API required** | Yes (GPT-5.5 Teacher + GPT-5.4 × 4 Students) |
| **Token cost (paper)** | ~2.15B logged tokens (0.10B active) per design |
| **Estimated time** | 4+ hours per design |
| **Success criterion** | Mean HPWL improvement within ±0.5 pp of 1.78% |
| **Status** | ⬜ Reference data available; config ready; requires API key |

### Claim 2: Black-Box BO Baseline (Table 4)

| Field | Value |
|---|---|
| **Paper claim** | Black-box BO achieves 0.38% mean HPWL improvement |
| **Paper location** | Table 4, Section 4.2 |
| **Experiment config** | `configs/paper/bo_search.yaml` |
| **Execution command** | `make reproduce-bo` |
| **Reference evidence** | `Agenticflow/local_backup/bo_runs_removed_fixed_seed_20260514_141547/` (9 case × 400 trials each) |
| **Generated table** | `results/tables/table_2_main_hpwl.csv` |
| **Make target** | `make table-2` |
| **API required** | No (BO-DSE uses Optuna TPE, no LLM) |
| **Estimated time** | ~30 min/case × 9 = ~4.5 hours |
| **Success criterion** | Mean HPWL improvement within ±0.3 pp of 0.38% |
| **Status** | ⬜ Reference data available; local BO-DSE verification needed |

### Claim 3: Constraint Repair — Hard Cut-Row (Table 6)

| Field | Value |
|---|---|
| **Paper claim** | ReviewDSE recovers strict legality on all 9 hard cut-row patterns |
| **Paper location** | Table 6, Section 4.4 |
| **Experiment config** | `configs/paper/evolve_search.yaml` (cut-row cases) |
| **Execution command** | Specialized cut-row test (not in standard reproduce flow) |
| **Raw result file** | Cut-row repair evidence (pattern-level replay) |
| **API required** | Yes (LLM) |
| **Estimated time** | Hours per pattern |
| **Success criterion** | 9/9 cut-row patterns pass OpenROAD and check_placement |
| **Status** | ⬜ Reference evidence not found in current backup; configs available |

### Claim 4: Baseline Comparison (Table 4 "Default" column)

| Field | Value |
|---|---|
| **Paper claim** | Native OpenROAD Diamond detailed-placement flow HPWL values |
| **Paper location** | Table 4 "Default" column, Section 4.1 |
| **Experiment config** | `configs/paper/baseline_9case.yaml` |
| **Execution command** | `make reproduce-baseline` |
| **Raw result file** | `flow/reports/*/dpl_evolve_baseline/*/suite_runs.tsv` |
| **Verified value** | AES N45 = 176,845.1 µm (exact match with paper's 176.8k) |
| **Generated table** | `results/tables/table_1_baseline_comparison.csv` |
| **Make target** | `make table-1` |
| **API required** | No |
| **Estimated time** | ~5 min/case |
| **Success criterion** | AES openroad_dpl_flow HPWL = 176,845.1 ± 0.05 µm |
| **Status** | ✅ Verified — precise reproduction on AES N45; other 8 cases pending synthesis |

---

### Claim 5: Input Reproducibility

| Field | Value |
|---|---|
| **Paper claim** | Synthesis and global placement produce deterministic inputs |
| **Paper location** | Section on Experimental Setup |
| **Experiment config** | `configs/smoke/aes_nangate45.yaml` |
| **Execution command** | `make smoke` |
| **Raw result file** | `results/reproduced/smoke/metrics.json` |
| **Generated table** | N/A (validation only) |
| **Make target** | `make smoke` |
| **API required** | No |
| **Estimated time** | ~5 minutes |
| **Success criterion** | Input ODB SHA-256 matches reference, 14,676 instances |
| **Status** | ✅ Verified (Phase 2 audit) |

---

### Claim 6: Level 1 Calibration Warm-Up

| Field | Value |
|---|---|
| **Paper claim** | Level 1 warm-up database reduces Level 2 cold-start overhead |
| **Paper location** | Section on Two-Level Architecture |
| **Experiment config** | `configs/ablation/level1_warmup.yaml` (post-deadline) |
| **Execution command** | `make reproduce-ablation` |
| **Raw result file** | `results/reproduced/ablation/level1/` |
| **Generated table** | `results/tables/ablation_level1.csv` |
| **Make target** | TBD |
| **API required** | Yes |
| **Estimated time** | Hours |
| **Success criterion** | Quantified speedup from Level 1 warm-up |
| **Status** | ⬜ Post-deadline |

---

### Claim 7: Teacher Feedback Effectiveness

| Field | Value |
|---|---|
| **Paper claim** | Teacher agent feedback improves search efficiency |
| **Paper location** | Section on Teacher-Student Architecture |
| **Experiment config** | `configs/ablation/teacher_ablation.yaml` (post-deadline) |
| **Execution command** | `make reproduce-ablation` |
| **Raw result file** | `results/reproduced/ablation/teacher/` |
| **Generated table** | `results/tables/ablation_teacher.csv` |
| **Make target** | TBD |
| **API required** | Yes |
| **Estimated time** | Hours |
| **Success criterion** | Quantified benefit of Teacher filtering |
| **Status** | ⬜ Post-deadline (reviewer request) |

---

## Summary Matrix

| # | Claim | Paper Location | Evidence in Backup? | API | Status |
|---|---|---|---|---|---|
| 1 | HPWL 1.78% | Table 4 | ✅ Multiple experiment runs | Yes | Config ready |
| 2 | BO 0.38% | Table 4 | ✅ 9×400 trials | No | Config ready |
| 3 | 9-case cut-row repair | Table 6 | ❌ Not in current backup | Yes | Config ready |
| 4 | Default baseline values | Table 4 | ✅ 9/9 cases match | No | ✅ AES verified |
| 5 | Input reproducibility | Section 4.1 | ✅ Phase 2 audit | No | ✅ Verified |
| 6 | Stage-local counterexamples | Table 5 | ❌ Not in current backup | N/A | Reference only |
| 7 | Level 1 warm-up | Not quantified in paper | ❌ Not in backup | Yes | Post-deadline |

---

## How To Add A New Claim

1. Add a row to the appropriate section above
2. Create a config file in `configs/paper/` or `configs/ablation/`
3. Add a make target if needed
4. Document expected results in `expected-results.md`
5. Link from `README.md` if it's a primary claim
