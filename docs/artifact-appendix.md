# Artifact Appendix

This is the formal Artifact Appendix for the MLCAD 2026 paper:

> **From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA**

Paper ID: 150

---

## A.1 Abstract

This artifact provides the scripts, configurations, and reference data to
reproduce the key experimental results in the paper. It includes:

1. A fully automated, version-pinned build system for Yosys and a custom
   OpenROAD core with DPLEvolve source overlays.
2. Baseline evaluation scripts for the three canonical detailed-placement
   lines (native OpenROAD, negotiation mode, DPLEvolve default).
3. Configuration files and launcher scripts for the Level 2 Teacher-Student
   multi-agent DSE pipeline.
4. Reference results from the paper's experimental runs.
5. Validation scripts with checksum-gated input verification and
   tolerance-based output comparison.

The artifact is designed so that the minimal validation path (`make setup &&
make smoke`) runs in under 40 minutes without any LLM API calls, while the
full DSE reproduction requires Claude API access.

---

## A.2 Artifact Check-List (Meta-Information)

- **Algorithm**: LLM-agent-driven white-box design space exploration
- **Program**: Bash, Python, C++ (OpenROAD), Tcl
- **Compilation**: GCC ≥ 9, CMake ≥ 3.20
- **Binary**: Custom OpenROAD binary with DPLEvolve overlay
- **Data set**: Open-source benchmark designs (ORFS default set)
- **Run-time environment**: Linux x86-64, Environment Modules recommended
- **Hardware**: 4+ CPU cores, 16+ GB RAM, 10+ GB disk
- **Publicly available?**: Yes (GitHub + Zenodo)
- **Code license**: BSD 3-Clause
- **Workflow framework**: ORFS (OpenROAD-flow-scripts)

---

## A.3 Description

### A.3.1 How to Access

The artifact is available at:
- **GitHub**: https://github.com/CODA-Team/DPLEvolve
- **Zenodo**: [DOI to be assigned upon publication]

### A.3.2 Hardware Dependencies

Standard x86-64 Linux server. No GPU, no specialized hardware. Tested on
RHEL 8 with Environment Modules.

### A.3.3 Software Dependencies

All build dependencies are handled by `make setup`:
- GCC ≥ 9, CMake ≥ 3.20, Bison ≥ 3.6, Flex ≥ 2.6
- Python 3.11 with PyYAML 6.0.3
- Yosys 0.64 (exact commit pinned)
- Custom OpenROAD (exact commit pinned)

### A.3.4 Data Sets

Open-source benchmark designs distributed with OpenROAD-flow-scripts:
- Nangate45: aes, ibex, jpeg, ariane133, bp_quad
- ASAP7: aes, ibex, jpeg, swerv_wrapper

---

## A.4 Installation

```bash
git clone <this-repo-url> DPLEvolve-AE
cd DPLEvolve-AE
make check     # Verify prerequisites
make setup     # Build all dependencies (~30 min)
```

The setup is fully automated and idempotent. All artifacts are installed
under user-writable project directories; no root access is needed.

---

## A.5 Experiment Workflow

### Minimal Validation (No API, ~40 min total)

```bash
make setup     # Build Yosys + OpenROAD (~30 min)
make smoke     # AES smoke test (~5 min)
make check     # Environment validation (~1 min)
```

### Baseline Reproduction (No API, ~30 min)

```bash
make reproduce-baseline   # All 9 cases, 3 baseline lines
make table-1              # Generate Table 1
```

### Full DSE Reproduction (API Required, Hours–Days)

```bash
# Set API credentials
export ANTHROPIC_API_KEY=...

# Run the full experiment
make reproduce-main
make table-2              # Generate Table 2
make table-3              # Generate Table 3
```

---

## A.6 Evaluation and Expected Results

### Smoke Test Expected Output

```
[OK] input ODB SHA-256: d8e58c24...
[OK] instance_count: 14676
[OK] instance_area_micron2: 18648.2
[OK] final_hpwl_micron: 176845.1
[OK] placement_violations: (none)
[OK] AES smoke test PASSED
```

### Baseline Expected Output

Table 1 generated at `results/tables/table_1_baseline_comparison.csv`.

### Main Results

The full experiment generates Tables 2 and 3. Due to the non-deterministic
nature of LLM-based search, exact numerical reproduction is not expected.
Results should be within the tolerances specified in
`docs/expected-results.md`.

---

## A.7 Experiment Customization

All experiment parameters are in `configs/`. To run on fewer cases, edit the
case list in the appropriate YAML file. To adjust search breadth or iteration
budget, modify the experiment plan configuration.

---

## A.8 Notes

- The artifact requires the sibling repositories `dpl_evolve_agent/` and
  `OpenROAD-flow-scripts/` at the exact commits recorded in
  `provenance/source-commits.json`.
- Yosys version is critical: using a different version changes the synthesized
  netlist and invalidates the baseline comparison.
- The full DSE pipeline consumes significant LLM tokens (~2.15B per design).
  Reviewers should be aware of this cost before running the complete experiment.
