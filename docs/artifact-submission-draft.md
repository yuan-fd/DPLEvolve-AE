# Artifact submission draft

This document is a working draft for the AE submission form and the Artifact
Appendix in the paper. Replace the bracketed release fields after the final
Zenodo deposit is created.

## Artifact Abstract

This artifact accompanies *From Tool Invocation to Source-Mechanism
Exploration: Protected White-Box DSE for Open-Source EDA*. It provides the
source code, pinned open-source EDA environment, experiment configurations,
protected evaluator, retained source programs, and command-line and Web entry
points needed to reproduce the paper's detailed-placement experiments.

The artifact regenerates the nine incoming placement inputs used in Table 4,
runs the OpenROAD default and 400-trial Bayesian-optimization baselines,
rebuilds and evaluates the 18 selected ReviewDSE programs, executes the 27-job
cut-row matrix in Table 6, and reconstructs Figures 4 and 5 from retained or
fresh campaign outputs. It also exposes the complete Teacher/Student
source-edit, build, protected-evaluation, and review loop. The full nine-target
ReviewDSE search is executable but stochastic and requires authenticated model
access and the paper-scale paid token budget; selected-program replay does not
require an LLM.

Fresh candidates are accepted from newly generated OpenROAD evidence: complete
stage-wise HPWL, strict placement legality, displacement, runtime,
mechanism-liveness signals, and source/binary/metric consistency. Numerical
agreement is evaluated within documented cross-host tolerances rather than by
requiring bit-identical binaries. Table 5 uses three checksummed legalizer
snapshots and regenerates its AES, JPEG, and SWERV inputs with recorded local
utilization values 70, 90, and 60.

The tested platform is Rocky Linux 8.10 x86-64. No GPU or commercial EDA
license is required. The source release is under the BSD 3-Clause License.
The archival artifact is available at **[Zenodo DOI and URL]**; development and
review access is available at <https://github.com/yuan-fd/DPLEvolve-AE>.

## Artifact Evaluation Appendix

### A. Artifact identification and scope

- Paper: *From Tool Invocation to Source-Mechanism Exploration: Protected
  White-Box DSE for Open-Source EDA*.
- Development repository: <https://github.com/yuan-fd/DPLEvolve-AE>.
- Archival release: **[Zenodo DOI and URL]**.
- Release tag / commit: **[final release tag and commit SHA]**.
- License: BSD 3-Clause.

The artifact supports fresh execution of Tables 4--6, Figures 4 and 5, the
ReviewDSE search process, and the Ariane mechanism diagnostic. Supporting
repository tests and the AES toolchain check diagnose installation but are not
substitutes for the paper experiments.

### B. Hardware and software requirements

The author reference machine runs Rocky Linux 8.10 x86-64 with two Intel Xeon
Platinum 8462Y+ processors, 314 GiB RAM, and a 22 TiB home filesystem. This is
the tested configuration, not a claimed minimum. Smaller machines can execute
one target at a time with reduced `THREADS`; complete BO, cut-row, and search
campaigns require server-class memory and storage because they retain build
trees, logs, metrics, and ODB files.

The host requires Linux x86-64, Bash 4+, GNU Make 4+, Python 3.11+, Git,
rsync, and the standard ORFS/OpenROAD build dependencies. A GPU and commercial
EDA licenses are not required. Network access is needed during source/data
download and environment setup. Live ReviewDSE search additionally requires
authenticated model access. Exact tested and pinned versions are listed in
`docs/requirements.md` and `docs/environment.md`.

### C. Installation

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE
make doctor
make bootstrap
make build-tools THREADS=8
make prepare-paper-inputs THREADS=8
```

`make doctor` is read-only. The remaining commands create sibling ORFS and
state workspaces and build the pinned Yosys/OpenROAD environment. A prepared
environment can be inspected with `make check`.

### D. Evaluation workflow

#### D.1 Table 4: QoR and runtime

```bash
bash artifacts/01-table4-qor/reproduce.sh --threads 10
```

This command runs nine OpenROAD defaults, 400 BO trials per target, and both
nine-program ReviewDSE replay tracks. A reviewer can enter the same path on one
target first:

```bash
make reproduce-default CASE=aes_nangate45 THREADS=8
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=ghr THREADS=8
```

The aggregate fresh result is written to
`../dpl_evolve_state/paper_reproduction/table4/table4-fresh.tsv`. The paper
means relative to OpenROAD default are -0.38% for BO-DSE, -1.78% / 1.34x for
ReviewDSE-HPWL, and -1.68% / 1.11x for ReviewDSE-GHR. The default numerical
windows are 0.06 percentage point for the reported mean HPWL differences and
0.20 for runtime ratio; AES Nangate45 also checks absolute HPWL within 0.5%.

#### D.2 Table 5: stage composability

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

The first command verifies the LEGALM, Diamond, and Negotiation snapshots. The
runner regenerates AES/JPEG/SWERV inputs with Table-5-local utilization values
70/90/60, executes the mappings LEGALM/Diamond, Negotiation/Negotiation, and
Diamond/Negotiation, and writes a fresh `table5-fresh.tsv`. Retained paper
values remain comparison targets and are never treated as observations.

#### D.3 Table 6: hard cut-row legality

```bash
bash artifacts/03-table6-cutrow/reproduce.sh --fetch --threads 10
```

The runner verifies the external package and executes Diamond, Negotiation,
and the retained ReviewDSE repair on nine DEF/Verilog/SDC patterns. Each job has
a 7200-second cap and must pass strict `check_placement`. The complete summary
contains 27 fresh rows under
`../dpl_evolve_state/paper_reproduction/table6_*/table6-fresh.tsv`.

#### D.4 ReviewDSE search

```bash
# Inspect the configured launch; this is not an alternative method path.
bash artifacts/05-reviewdse-search/reproduce.sh --plan

# Execute one bounded but real Teacher/Student loop.
bash artifacts/05-reviewdse-search/reproduce.sh \
  --small --case aes_nangate45 --threads 8

# Execute the paper-scale Level 1 and Level 2 profiles.
bash artifacts/05-reviewdse-search/reproduce.sh \
  --level1 --acknowledge-cost --threads 10
bash artifacts/05-reviewdse-search/reproduce.sh \
  --paper --run-prefix review_run_01 --acknowledge-cost --threads 10
```

The paper Level 2 profile uses nine targets, one Teacher, four Students, ten
iterations, and an exact 2x runtime gate. The paper reports approximately
2.15B logged tokens (about 0.10B active tokens) per target. Search reproduction
means executing the disclosed protected process and reporting the fresh
trajectory; identical model proposals are not expected. The released Level 1
profile is a runnable reconstruction because the exact author-time Level 1
Student breadth and frozen packet were not retained.

#### D.5 Figures and supporting diagnostic

```bash
bash artifacts/04-figures/reproduce.sh
bash artifacts/06-ariane-diagnostic/reproduce.sh --threads 10
```

Figure 4 preserves three explicitly missing SWERV points instead of imputing
them. Figure 5 recomputes runtime-ratio Pareto membership. The Ariane command
rebuilds and evaluates six retained source trees and derives the reported group
means from fresh metrics; it is supporting mechanism evidence rather than a
controlled paper ablation.

### E. Result interpretation

Every fresh candidate records incoming, post-legalization, post-DPO, and final
HPWL; strict legality; average and maximum displacement; runtime; and
mechanism-liveness signals. ReviewDSE candidates additionally require matching
source, build, binary, and evaluation provenance. Expected files are comparison
targets only and are never copied into fresh observed fields.

Paper-time hashes and identical linked binaries are unavailable for some
regenerated inputs. Those cases are evaluated by complete legal execution and
the documented numerical windows. The Web Demo (`bash web-demo/start.sh`)
invokes the same fixed Make commands and exposes their live logs; it does not
implement a separate evaluation path.

### F. Reproducibility limitations

1. The original Level 1 breadth and frozen packet were not retained; the public
   Level 1 profile is a disclosed reconstruction.
2. The full LLM search is stochastic and expensive. Teacher and Student
   configuration remains mandatory; selected-program, baseline, Table 5/6,
   figure, and diagnostic replays may validate fixed results without issuing
   new model requests, but do not replace the model-backed method.
3. Cross-host numerical reproduction, not bit-for-bit replay, is claimed where
   paper-time input hashes or linked binaries are unavailable.
