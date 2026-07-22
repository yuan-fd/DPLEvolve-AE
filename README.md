# DPLEvolve Artifact Evaluation

Artifact for **“From Tool Invocation to Source-Mechanism Exploration:
Protected White-Box DSE for Open-Source EDA.”**

This repository is organized around the paper experiments. Its primary job is
to let a reviewer execute the protected detailed-placement evaluator, reproduce
the default and BO baselines, rebuild and replay the selected ReviewDSE source
programs, and—when API access and budget are available—rerun the Teacher/Student
search itself.

`make audit-archive` is only a quick integrity check of packaged records. It is
not presented as experimental reproduction. The optional AES smoke command is
only a toolchain diagnostic and is not a paper experiment.

## What is reproducible

| Paper result | Fresh command | What the command actually executes |
|---|---|---|
| Table 4 default | `make reproduce-default` | OpenROAD default DPL on all nine target ODBs |
| Table 4 BO | `make reproduce-bo` | 400 Optuna-TPE trials per target, four trials in parallel |
| Table 4 ReviewDSE-HPWL | `make replay-reviewdse TRACK=hpwl` | Builds nine frozen selected source trees and runs their complete DPL trajectories |
| Table 4 ReviewDSE-GHR | `make replay-reviewdse TRACK=ghr` | Builds and runs the nine runtime-aware selected source trees |
| Table 5 | `make reproduce-table5` | Replays both complete source candidates for each stage-composability counterexample |
| Table 6 | `make reproduce-table6` | Replays Diamond, Negotiation, and ReviewDSE on nine exact cut-row ODBs with a 7200 s cap |
| ReviewDSE Level 1 | `make reproduce-level1` | Runs the three calibration instances and freezes reviewed mechanism/source-start evidence |
| ReviewDSE Level 2 | `make run-dse-small` / `make run-dse-paper` | Runs the target Teacher/Student source-edit, build, evaluate, and review loop |

`make reproduce-paper-results` is the complete no-LLM result path: it checks
the external data first, then executes Table 4, Table 5, and Table 6. It is not
a shortcut to archived numbers. `make reproduce-paper-search
ACKNOWLEDGE_LLM_COST=yes` executes Level 1 followed by the full Level 2 search.

Important current data status: the Table 4 selected source trees are included,
and its nine input ODBs can be regenerated from the pinned flow. The exact
paper-time Table 5/6 ODB/SDC pairs and complete source candidates have not yet been
recovered into the public artifact. Their fresh commands are complete but stop
with an explicit missing-file report until those assets are installed. See
[Exact paper-data layout](docs/paper-data-layout.md). The artifact must not be
called a complete Table 5/6 reproduction package before that recovery is done.

## Paper experiment being reproduced

![ReviewDSE architecture](images/dplevolve-architecture.png)

The evaluation target is OpenROAD **detailed placement**, starting from
`3_4_place_resized.odb`; it is not an RTL-to-GDS experiment. Every candidate is
rebuilt and run through the protected evaluator, which records:

- incoming global-placement HPWL, `H_g`;
- post-legalization HPWL, `H_lg`;
- post-DPO HPWL, `H_ip`;
- final post-DPL HPWL after final processing, `H_f`;
- strict legality, average/maximum displacement, runtime, liveness, and
  source/binary/metric consistency.

The machine-readable experiment contract is
[`configs/reproduction/paper-experiments.json`](configs/reproduction/paper-experiments.json).
It records the nine targets, Level 1 calibration cases, BO budget, model
profiles, iteration count, selection tracks, and Table 5/6 stress patterns.

## 1. Prepare the environment

The authors ran the experiments on Rocky Linux 8.10 x86-64 with two Intel Xeon
Platinum 8462Y+ CPUs (64 physical/128 logical cores total), 314 GiB RAM, and a
22 TiB `/home` filesystem. No GPU or commercial EDA license is required.

That is the measured reference machine, not a claim that every command consumes
all 314 GiB. OpenROAD alone can exceed 2 GiB even on modest cases; compilation,
parallel BO, large Ariane/SWERV/BPQUAD cases, and concurrent agent evaluations
need substantially more headroom. We therefore do not advertise the previous
8/16 GiB RAM or 10 GiB disk configuration as sufficient for full reproduction.
For planning, use a server-class Linux host, start with at least tens of GiB of
RAM and ample local disk, reduce parallelism on smaller machines, and monitor
actual peak memory on the selected cases.

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE

make doctor
make bootstrap
make build-tools THREADS=16
make prepare-paper-inputs THREADS=16
make validate-evaluator CASE=aes_nangate45 THREADS=8
```

`bootstrap` checks out the recorded ORFS/OpenROAD revisions and applies the
tracked DPLEvolve patches. `build-tools` builds pinned Yosys/OpenROAD. Input
preparation runs the real ORFS place target for the nine designs. The evaluator
validation then runs the canonical DPL lines and emits new `metrics.json`
records containing the full stage trajectory.

The setup scripts never invoke `sudo`. On Rocky Linux, run `make doctor` and
ask the system administrator to install only the missing build packages it
reports. Exact repository revisions are in
[`provenance/source-commits.json`](provenance/source-commits.json).

## 2. Reproduce Table 4

Run one target first:

```bash
make reproduce-default CASE=aes_nangate45 THREADS=8
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=ghr THREADS=8
```

Then run all nine cases:

```bash
make reproduce-table4 THREADS=10
```

`setup-bo` creates the isolated Ray Tune/Optuna environment used only by the BO
baseline. This is a large run: BO alone executes 9 × 400 fresh placements. Outputs go
under `DPL_EVOLVE_STATE_ROOT` and ORFS `flow/reports/`; they are not written
over the archived expected values. The frozen-source replay does not call an
LLM, but it does compile and execute each selected C++ source program. After
all nine cases finish, `summarize-table4` writes
`$DPL_EVOLVE_STATE_ROOT/paper_reproduction/table4/table4-fresh.tsv` directly
from the new default `metrics.json`, BO `best.json`, and replay `results.tsv`.

Regenerated ODBs are used with the stable `paper9_place` flow variant. Exact
paper-time ODBs can instead be installed under the variant in the selected
program manifest. The AES Nangate45 input is checksum-pinned; input checksums
for the other eight cases still need to be added before claiming bit-exact
paper-input identity.

## 3. Run the ReviewDSE method

The paper method first constructs frozen global evidence on three calibration
instances (JPEG N45 UTIL=90, AES N45 UTIL=70, and SWERV N45 UTIL=60):

```bash
make plan-level1
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes LEVEL1_CHILDREN=50 THREADS=10
```

This generates a read-only Level 1 packet that the Level 2 launcher copies into
every target context. The main paper does not disclose Level 1 Student breadth;
50 is the framework's documented public breadth-calibration profile, not an
assertion about the unrecovered author run. The author-time value must be added
to the AE appendix/configuration before claiming exact search-process
reproduction.

A small run exercises the actual method rather than inspecting a final binary:

```bash
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1 THREADS=8
```

It creates a Teacher, gives a Student a private source workspace, applies a
source edit, builds a private OpenROAD variant, evaluates the complete flow,
and returns the evidence to Teacher review. It requires a working Codex/API
configuration and incurs model usage.

The exact paper launch is deliberately cost-gated:

```bash
make plan-dse-paper
make run-dse-paper ACKNOWLEDGE_LLM_COST=yes THREADS=10
```

The paper protocol is one GPT-5.5 xhigh Teacher, four GPT-5.4 xhigh Students,
10 iterations per target, and a 2× runtime gate. The paper reports a mean of
2.15 billion logged tokens and 0.10 billion active tokens per target; over nine
targets that is about 19.35 billion logged and 0.90 billion active tokens. A
reviewer is **not required** to pay for this full search. The artifact must,
however, contain the runnable full-search path and all non-LLM replay paths.

## 4. Reproduce Tables 5 and 6

Check whether the separately distributed, checksummed exact assets are installed:

```bash
make paper-data-check
```

Once the check reports complete inputs:

```bash
PAPER_DATA_ROOT=/path/to/paper-data make reproduce-table5 THREADS=10
PAPER_DATA_ROOT=/path/to/paper-data make reproduce-table6 THREADS=10
```

To execute every non-LLM result experiment in order:

```bash
PAPER_DATA_ROOT=/path/to/paper-data make reproduce-paper-results THREADS=10
```

Table 5 reruns the legalization-selected candidate and its full-flow reference,
then compares `H_lg` with downstream `H_ip`/`H_f`. Table 6 stages each exact
cut-row ODB and executes fixed Diamond, fixed Negotiation, and the reported
ReviewDSE repair source using the same legality checker and timeout. Neither
command reads the archived expected JSON as an experimental result.

## Supporting audit and diagnostic commands

```bash
make audit-archive       # seconds; recomputes packaged Table 4/5/6 records
make toolchain-smoke     # optional AES toolchain diagnostic
make test
```

`make evidence` remains only as a compatibility alias for
`make audit-archive` and prints that limitation. Archived records are useful
for provenance and quick review, but agreement between a TSV and the paper is
not reproduction.

## Repository layout

```text
DPLEvolve-AE/
├── configs/reproduction/     # paper-derived execution manifest and case plans
├── scripts/reproduce/        # public fresh-experiment entry points
├── src/dpl_evolve_agent/     # protected evaluator and Teacher/Student framework
├── artifacts/                # archived records and 18 frozen Table 4 source trees
├── docs/                     # environment, data, and reviewer guidance
├── images/                   # repository figures
├── provenance/               # pinned source revisions
├── tests/                    # command/manifest/evaluator regression tests
└── web-demo/                 # optional browser frontend to the same Make targets
```

The command overview is always available with `make help`.
