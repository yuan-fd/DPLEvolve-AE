# Artifact submission draft

This document provides the artifact abstract and the expanded instructions for
the AE submission form. The paper contains the shorter two-page version.

## Artifact Abstract

This artifact accompanies *From Tool Invocation to Source-Mechanism
Exploration: Protected White-Box DSE for Open-Source EDA*. It includes the
ReviewDSE source code, a pinned open-source EDA environment, the experiment
configurations, the protected evaluator, the source programs used in the
paper, and command-line and Web interfaces for the detailed-placement
experiments.

The supplied workflows regenerate the nine placement inputs used in Table 4,
run the OpenROAD default and 400-trial Bayesian-optimization baselines, rebuild
and evaluate the 18 selected ReviewDSE programs, run the 27 cut-row experiments
reported in Table 6, and reconstruct Figures 4 and 5 from either the paper
outputs or a new campaign. The repository also provides the complete
Teacher--Student loop for source editing, building, protected evaluation, and
review. A full nine-target search is stochastic and requires authenticated
model access and a paper-scale token budget. The functional workflow verifies
access to both configured models before it reruns the reported experiments.

The protected evaluator measures stage-wise HPWL, strict placement legality,
displacement, runtime, mechanism liveness, and source/binary/metric
consistency for every new candidate. Results are compared using documented
cross-host tolerances; bit-identical binaries are not required. Table 5
uses three checksummed legalizer snapshots and regenerates the AES, JPEG, and
SWERV inputs at utilization values of 70, 90, and 60, respectively.

The artifact runs on Rocky Linux 8.10 (x86-64) and requires authenticated
Codex access. It does not require a GPU, a commercial EDA license, or a
proprietary PDK. The source code is released under the BSD 3-Clause License.
The evaluated release is archived on Zenodo at
<https://doi.org/10.5281/zenodo.21629308>, and the live repository is available
at <https://github.com/yuan-fd/DPLEvolve-AE>.

## Artifact Evaluation Appendix

### A. Artifact identification and scope

- The artifact accompanies *From Tool Invocation to Source-Mechanism
  Exploration: Protected White-Box DSE for Open-Source EDA*.
- The live repository is available at
  <https://github.com/yuan-fd/DPLEvolve-AE>.
- The evaluated archive is available at
  <https://doi.org/10.5281/zenodo.21629308>.
- The source code is released under the BSD 3-Clause License.
- Wenjie Yuan of Fudan University
  (<25303060069@m.fudan.edu.cn>) maintains the artifact package and its
  reproduction workflow.

The artifact provides workflows that reproduce Tables 4--6 and Figures 4 and
5, run the ReviewDSE search, and perform the Ariane mechanism diagnostic. The
repository tests and AES toolchain check help diagnose installation problems,
while the paper experiments use dedicated entry points.

### B. Hardware and software requirements

The reference platform runs Rocky Linux 8.10/x86-64 and has two Intel Xeon
Platinum 8462Y+ processors, 314 GiB of RAM, and a 22-TiB home filesystem. This
is a reference configuration rather than a minimum requirement. A smaller
machine can process one target at a time with a lower `THREADS` value. Complete
BO, cut-row, and search campaigns require server-class memory and storage
because they preserve their build trees, logs, metrics, and ODB files.

The host must provide Linux x86-64, Bash 4 or later, GNU Make 4 or later,
Python 3.11 or later, Git, rsync, and the standard ORFS/OpenROAD build
dependencies. The workflow needs network access while it downloads the source
code and data and prepares the environment. It does not require a GPU or a
commercial EDA license. A ReviewDSE search also requires authenticated model
access. The OpenROAD baseline follows the upstream `master` branch. The README
records the tested tool versions and points to the exact pinned revisions.

### C. Installation

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE
make doctor
make bootstrap
make build-tools THREADS=8
make prepare-paper-inputs THREADS=8
make check
make check-demo-models
```

`make doctor` inspects the host without changing it. The next three commands
create the ORFS and state workspaces, build the pinned Yosys and OpenROAD
environment, and generate the placement inputs. `make check` verifies the
prepared environment. The final command sends one small request to each
configured model and must report `MODEL_READY` for both the Teacher and
Student.

### D. Evaluation workflow

#### D.1 Table 4: QoR and runtime

```bash
bash artifacts/01-table4-qor/reproduce.sh --threads 10
```

This command runs the OpenROAD default flow on nine targets, performs 400 BO
trials per target, and replays 18 ReviewDSE programs across the two selection
tracks. Reviewers can first exercise the same workflow on one target:

```bash
make reproduce-default CASE=aes_nangate45 THREADS=8
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=ghr THREADS=8
```

The command writes the aggregate result to `table4-fresh.tsv` in the configured
state directory. Relative to the OpenROAD default, the paper reports mean
changes of -0.38% for BO-DSE, -1.78% / 1.34x for ReviewDSE-HPWL, and -1.68% /
1.11x for ReviewDSE-GHR. The acceptance windows are 0.06 percentage points for
the mean HPWL change and 0.20 for the runtime ratio. The AES Nangate45 check
also requires the absolute HPWL to agree within 0.5%.

#### D.2 Table 5: stage composability

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

The first command verifies the LEGALM, Diamond, and Negotiation snapshots. The
runner regenerates the AES, JPEG, and SWERV inputs at utilization values of 70,
90, and 60, respectively. It then executes the selected/reference mappings
LEGALM/Diamond, Negotiation/Negotiation, and Diamond/Negotiation and writes a
new `table5-fresh.tsv`. The paper values serve only as references; all reported
observations come from new runs. Each selected/reference pair
must satisfy $\Delta H_{lg}<0$ and $\Delta H_f>0$. The reported AES, JPEG, and
SWERV changes are -0.76%/+1.48%, -14.96%/+20.96%, and -0.12%/+0.02%,
respectively.

#### D.3 Table 6: hard cut-row legality

```bash
bash artifacts/03-table6-cutrow/reproduce.sh --fetch --threads 10
```

The runner verifies the downloaded data package and executes Diamond,
Negotiation, and the ReviewDSE repair on nine DEF/Verilog/SDC patterns. Each
job has a 7200-s limit and must pass strict `check_placement`. A complete run
writes 27 new rows to `table6-fresh.tsv` in the configured state directory.
Status and strict-legality classifications must match exactly, while runtime
may vary by 35%. All nine ReviewDSE cases, one Diamond case, and one
Negotiation case must be legal.

#### D.4 ReviewDSE search

```bash
# Print the configured launch without starting a run.
bash artifacts/05-reviewdse-search/reproduce.sh --plan

# Run one bounded Teacher--Student iteration.
bash artifacts/05-reviewdse-search/reproduce.sh \
  --small --case aes_nangate45 --threads 8

# Run the paper-scale Level 1 and Level 2 profiles.
bash artifacts/05-reviewdse-search/reproduce.sh \
  --level1 --acknowledge-cost --threads 10
bash artifacts/05-reviewdse-search/reproduce.sh \
  --paper --run-prefix review_run_01 --acknowledge-cost --threads 10
```

The paper reports a search conducted in April and May 2026. The paper's Level 2
configuration uses nine targets, one Teacher, four Students, ten iterations,
and a 2x runtime gate. The paper configuration uses `gpt-5.5` for the Teacher
and `gpt-5.4` for the Students, all with `xhigh` reasoning effort. The AE
configuration uses `gpt-5.6-sol` for the Teacher and `gpt-5.6-terra` for the
Student, also with `xhigh` reasoning effort. The reported usage averages 2.15B
logged tokens (about 0.10B active tokens) per target.

Reproducing the search means running the disclosed protected process and
reporting the new trajectory; the model is not expected to propose identical
source edits. The public Level 1 profile reconstructs the calibration stage
because the exact Student count and frozen packet are not available from the
original run.

#### D.5 Figures and supporting diagnostic

```bash
bash artifacts/04-figures/reproduce.sh
bash artifacts/06-ariane-diagnostic/reproduce.sh --threads 10
```

Figure 4 must contain 96 observed points and identify the three unavailable
SWERV points instead of imputing them. Figure 5 recomputes the runtime-ratio
Pareto set. The Ariane command rebuilds and evaluates all six archived Ariane
implementations and derives the reported group means from freshly generated
metrics. This diagnostic provides supporting evidence about the mechanism; it
is not a controlled ablation.

### E. Result interpretation

Every new candidate records the incoming, post-legalization, post-DPO, and
final HPWL together with strict legality, average and maximum displacement,
runtime, and mechanism-liveness signals. A ReviewDSE candidate is eligible only
when its source, build, binary, and evaluation records agree. The scripts use
the expected files only for comparison and never copy their values into a new
observation.

Some regenerated inputs do not have the same hashes or linked binaries as the
original paper run. The artifact evaluates those cases by requiring a complete
legal run within the documented numerical windows. The Web interface, started
with `bash web-demo/start.sh`, invokes the same fixed Make commands and displays
their live logs. It does not implement a separate workflow.

### F. Reproducibility limitations

1. The exact Level 1 Student count and frozen calibration packet are not
   available from the original run. The public Level 1 profile therefore
   reconstructs that stage from the disclosed procedure.
2. A complete model search is stochastic and expensive. The functional
   workflow verifies the required Teacher and Student access and then reruns
   the reported experiments. A separate cost-gated command runs the
   paper-scale search.
3. When regenerated inputs differ from the original hashes or linked binaries,
   the evaluation therefore requires numerical agreement across hosts rather
   than bit-for-bit replay.
