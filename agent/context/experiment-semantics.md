# Experiment Semantics

This file defines what each experiment means — what constitutes success,
what constitutes failure, and what the boundary conditions are.

---

## Experiment: AES Smoke Test

### Purpose
Validate the complete synthesis-to-placement pipeline using pinned tools.
Does NOT test DSE or invoke LLMs.

### Input
- RTL: `$ORFS_ROOT/flow/designs/src/aes/aes.v`
- Platform: Nangate45
- Config: `designs/nangate45/aes/config.mk`

### Pipeline Stages
1. Yosys synthesis (pinned commit)
2. Floorplan initialization
3. Global placement (RePlAce)
4. Detailed placement (OpenROAD `detailed_placement`)
5. Metric extraction (HPWL, instance count, area, violations)

### Success Criteria (Hard Gates)
- `status == "ok"`
- `legalize_exit_status == 0`
- `instance_count == 14676`
- `instance_area == 18648.2 +/- 0.05`
- `global_hpwl_micron == 188569.2 +/- 0.05`
- `final_hpwl_micron == 176845.1 +/- 0.05`
- `placement_violations == ""`
- `metric_error_count == 0`
- Input ODB SHA-256 matches reference

### Success Semantics
- **PASS**: All hard gates satisfied → Environment is validated
- **FAIL**: Any hard gate violated → Environment is broken, stop and diagnose

---

## Experiment: Baseline Reproduction

### Purpose
Reproduce the three canonical baseline lines across all paper cases.
Provides the reference point against which DSE improvements are measured.

### Baseline Lines
| ID | Description | Dependencies |
|---|---|---|
| `openroad_dpl_flow` | Native OpenROAD detailed placement | OpenROAD only |
| `openroad_dpl_negotiation` | OpenROAD with `-use_negotiation` | OpenROAD only |
| `evolve_default` | DPLEvolve framework default | DPLEvolve overlay |

### Cases
Configured in `configs/paper/baseline_9case.yaml`:
- Nangate45: aes, ibex, jpeg, ariane133, bp_quad
- ASAP7: aes, ibex, jpeg, swerv_wrapper

### Success Criteria
- All three lines complete with exit code 0 for each case
- No fatal errors in any OpenROAD log
- AES openroad_dpl_flow HPWL: 176845.1 ± 0.05 (hard gate)
- Other cases: verified against reference values

### Success Semantics
- **PASS**: AES hard gate met + all runs exit 0 → Baselines verified
- **CONDITIONAL_PASS**: AES hard gate met, some non-AES cases differ → Investigate
- **FAIL**: AES hard gate violated → Environment issue

---

## Experiment: Main DSE Results

### Purpose
Reproduce the paper's core claim — ReviewDSE Level 2 achieves superior HPWL
improvement over black-box BO.

### Workflow
1. **Level 1** (optional, if warm-up DB available): Load pre-built mechanism DB
2. **Level 2**: Multi-agent Teacher-Student loop
   - Students propose source-code changes
   - Compile candidate OpenROAD binaries
   - Evaluate on benchmark designs
   - Teacher screens results
   - Iterate until convergence or budget exhaustion

### Success Criteria
- All 9 cases evaluated
- ReviewDSE mean HPWL improvement within ±0.5 pp of 1.78%
- BO mean HPWL improvement within ±0.3 pp of 0.38%
- All 9 constraint scenarios repaired

### Non-Determinism
LLM-based search is inherently non-deterministic. Exact numerical reproduction
is NOT expected. Assess whether:
1. The workflow completes without fatal errors
2. The relative ordering (ReviewDSE > BO > Baseline) is preserved
3. The magnitude of improvement is in the same ballpark as the paper

### Success Semantics
- **PASS**: Workflow completes; ReviewDSE > BO; improvements within tolerance
- **CONDITIONAL_PASS**: Workflow completes; ordering preserved but magnitudes differ
- **FAIL**: Workflow crashes or ordering violated

---

## Experiment Boundaries

### What an experiment does NOT validate
- The quality or correctness of the LLM's source-code modifications
- The optimality of the discovered configurations
- The generality of the approach beyond the tested cases
- The statistical significance of improvement (single runs only)

### Budget Constraints
- **NEVER** increase the case list beyond what's in the config
- **NEVER** increase the number of DSE rounds beyond the config
- **NEVER** auto-extend timeouts
- Each experiment has a fixed budget; if it fails within budget, report the
  failure — do not silently expand the budget
