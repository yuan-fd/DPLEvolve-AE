# Artifact appendix

## Artifact purpose

This artifact provides executable paths for the experiments in *From Tool
Invocation to Source-Mechanism Exploration: Protected White-Box DSE for
Open-Source EDA*.

It can:

- build the pinned open-source EDA environment;
- regenerate nine Table 4 placement inputs;
- rerun OpenROAD default and 400-trial BO baselines;
- rebuild and replay 18 selected ReviewDSE source programs;
- run a bounded or complete Teacher/Student search;
- execute all 27 Table 6 cut-row jobs;
- regenerate Figures 4/5 from retained or fresh campaign products; and
- replay the six-source Ariane diagnostic.

Table 5 is the only blocked reported table. Its SWERV DENSE_2 configuration and
six source trees were not retained.

## Reference platform

| Property | Value |
|---|---|
| OS | Rocky Linux 8.10, x86-64 |
| CPU | 2 × Xeon Platinum 8462Y+, 64 physical / 128 logical cores |
| Memory | 314 GiB |
| Storage | 22 TiB `/home` filesystem |
| GPU / commercial license | Not required |
| EDA framework | Pinned ORFS, OpenROAD, and Yosys revisions |
| Languages | Bash, Python, C++, Tcl |
| License | BSD 3-Clause |

The reference platform is not a minimum. Large cases and parallel BO require
substantial server memory and disk.

## Installation and minimum fresh review

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE
make doctor
make bootstrap
make build-tools THREADS=16
make prepare-paper-inputs CASE=aes_nangate45 THREADS=8
make validate-evaluator CASE=aes_nangate45 THREADS=8
make reproduce-default CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
```

## Complete result commands

```bash
make reproduce-table4 THREADS=10
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10
make reproduce-figures FIGURE_SOURCE=retained
make reproduce-ariane-diagnostic THREADS=10
```

The current aggregate is:

```bash
make reproduce-available-results THREADS=10
```

It excludes Table 5 explicitly.

## Search-cost boundary

No LLM is needed for fresh baseline, BO, selected-source, Table 6, figure, or
diagnostic runs. `make run-dse-small` exercises the real method with bounded
model use. The complete nine-case search is runnable but requires explicit
`ACKNOWLEDGE_LLM_COST=yes`; a reviewer is not expected to fund it.

## Reproducibility interpretation

Paper-time hashes and exact linked binaries were not retained for every input
and selected program. That does not prevent the reviewer from running the
scripts. Acceptance is based on complete legal execution and numerical
HPWL/runtime windows. The artifact therefore claims scientific/numerical
reproduction where bit-for-bit replay cannot be established.

The AES toolchain diagnostic is a supporting environment check, not a reported
paper experiment.
