# Artifact appendix

This artifact accompanies the MLCAD 2026 paper *From Tool Invocation to
Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA*.

## Artifact purpose

The artifact exposes runnable experiment paths for OpenROAD detailed placement:

- regenerate the nine target ODBs with pinned sources;
- rerun the default and 400-trial BO baselines;
- rebuild and replay the 18 selected ReviewDSE source trees;
- rerun the Level 1 calibration and Level 2 Teacher/Student search;
- regenerate the deleted Table 5 dense inputs and rerun its six candidates if
  the missing SWERV DENSE_2 config and source commits are recovered;
- rerun all 27 Table 6 OpenROAD jobs from retained cut-row DEF/V/SDC data.

The compact archived records are supplied for provenance and fast inspection.
They are not the primary evaluation path.

| Property | Value |
|---|---|
| Tested author OS | Rocky Linux 8.10, x86-64 |
| Author machine | 64 physical / 128 logical CPU cores, 314 GiB RAM, 22 TiB `/home` |
| GPU / commercial license | Not required |
| Languages | Bash, Python, C++, Tcl |
| EDA framework | OpenROAD-flow-scripts and OpenROAD at pinned revisions |
| Evaluation scope | Table 4/5 detailed placement from regenerated ODB; Table 6 from retained cut-row DEF/V/SDC |
| License | BSD 3-Clause |

The author machine is a reference configuration, not a universal minimum.
Large cases, four concurrent BO trials, and parallel agent evaluations require
server-class memory and storage. Reproduce one case at a time and lower
parallelism when using a smaller host.

## Installation and evaluator validation

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE
make doctor
make bootstrap
make build-tools THREADS=16
make prepare-paper-inputs CASE=aes_nangate45 THREADS=8
make validate-evaluator CASE=aes_nangate45 THREADS=8
```

The final two commands generate a real input ODB and execute the protected
trajectory `H_g -> H_lg -> H_ip -> H_f`, including strict legality,
displacement, runtime, liveness, and consistency checks.

## Paper experiment commands

```bash
# Table 4, all nine cases
make setup-bo
make reproduce-table4 THREADS=10

# Two-level search (real model use and substantial cost)
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes
make plan-dse-paper
make run-dse-paper ACKNOWLEDGE_LLM_COST=yes

# Download, verify, and replay retained Table 6 data
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10

# Table 5 reconstruction; awaits one input config and six sources
make prepare-table5-inputs THREADS=10
make check-table5-data
make reproduce-table5 THREADS=10
```

Fresh products are written outside the Git checkout under
`$DPL_EVOLVE_STATE_ROOT` and ORFS `flow/{results,reports,logs}`. They never
overwrite packaged expected values.

## Reviewer cost boundary

A reviewer is not expected to fund the entire LLM discovery search. The paper
reports a mean 2.15B logged and 0.10B active tokens per target over ten target
iterations. The artifact therefore provides three independent levels:

1. no-LLM fresh EDA baselines and frozen-source replay;
2. a small real Teacher/Student method run;
3. the exact cost-gated nine-case launch for an author or reviewer with budget.

The runnable launch is part of reproducibility even when the evaluator elects
not to spend that budget.

## Current completeness boundary

Table 4 execution code, selected source trees, and regenerable inputs are
present. Table 6's nine exact DEF/Verilog pairs, three SDC files, and single
evolved source survived; the exported 200 MB data archive and replay were
validated against a real Ariane run. Table 5 is not complete: its ODBs can be
regenerated for AES/JPEG, but SWERV's untracked `config_dense2.mk` and all six
selected/reference source commits are missing from the original workspace and
retained backup. Its command intentionally exits `BLOCKED` instead of using
the standard config or treating the archived TSV as reproduction.

The main paper also omits the exact Level 1 Student breadth. The public
50-Student calibration profile is explicitly marked as a reconstruction.
These limitations must be resolved before claiming bit-exact reproduction of
every paper experiment and search decision.

For an audit of packaged records only, run `make audit-archive`. The optional
`make toolchain-smoke` command is a one-case toolchain diagnostic; neither is a
paper experiment.
