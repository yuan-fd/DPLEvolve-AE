# DPLEvolve

Artifact for **“From Tool Invocation to Source-Mechanism Exploration:
Protected White-Box DSE for Open-Source EDA.”**

DPLEvolve searches source-level modifications to OpenROAD detailed placement.
This repository provides the pinned EDA environment and scripts for running and
evaluating the paper experiments with newly generated results.

## Repository structure

```text
DPLEvolve-AE/
├── artifacts/
│   ├── 01-table4-qor/              # Default, BO, and ReviewDSE QoR
│   ├── 02-table5-composability/    # Stage composability (data incomplete)
│   ├── 03-table6-cutrow/           # Hard cut-row legality
│   ├── 05-figures/                 # Figures 4 and 5
│   ├── 06-reviewdse-search/        # Level 1 and Teacher/Student search
│   └── 07-ariane-diagnostic/       # Ariane diagnostic
├── configs/reproduction/           # Paper experiment configurations
├── paper-data/                     # Retained/fetched experiment inputs
├── scripts/
│   ├── human/                      # Environment setup
│   ├── reproduce/                  # Experiment and evaluation scripts
│   └── agent/                      # Fixed agent-facing wrappers
├── src/dpl_evolve_agent/           # DPLEvolve implementation
├── web-demo/                       # Optional browser interface
├── docs/                           # Detailed reviewer documentation
└── Makefile                        # Main command interface
```

Each experiment directory contains its own `README.md`, `reproduce.sh`, inputs,
expected interpretation, and output contract.

## 1. Install the environment

The released environment was tested on Rocky Linux 8.10 x86-64. Detailed
software versions and hardware guidance are in
[docs/environment.md](docs/environment.md).

```bash
git clone https://github.com/yuan-fd/DPLEvolve-AE.git
cd DPLEvolve-AE

# Read-only prerequisite check
make doctor

# Fetch and build pinned ORFS, OpenROAD, and Yosys
make bootstrap
make build-tools THREADS=8

# Generate the nine incoming placement inputs
make prepare-paper-inputs THREADS=8
```

Generated tools and experiment state are stored outside the checkout in
`../OpenROAD-flow-scripts` and `../dpl_evolve_state` by default.

For a bounded one-target setup and evaluator check:

```bash
make reviewer-prepare THREADS=8
```

## 2. Run the experiments

### Table 4: QoR and runtime

```bash
# All nine default, 400-trial BO, and ReviewDSE-HPWL/GHR runs
make reproduce-table4 THREADS=10
```

This path executes OpenROAD and replays the selected source programs. It does
not require an LLM API. To start with one AES default and one selected program:

```bash
make reviewer-aes-result THREADS=8
```

### Table 5: stage composability

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

Table 5 currently reports `BLOCKED`: the paper-time SWERV `DENSE_2`
configuration and six source trees were not retained. It does not substitute
old numbers for a fresh run.

### Table 6: hard cut-row legality

```bash
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10
```

The complete matrix runs Diamond, Negotiation, and ReviewDSE on nine cut-row
patterns. To run one Ariane row first:

```bash
make reviewer-table6-one THREADS=10
```

### Figures 4 and 5

```bash
make reproduce-figures FIGURE_SOURCE=retained
```

### Optional ReviewDSE search

```bash
# One bounded live Teacher/Student iteration
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1 THREADS=8

# Print the full paper search plan without making API calls
make plan-dse-paper
```

The complete nine-target discovery search requires model credentials and a
very large paid token budget. It is not required for selected-program replay.
See [artifacts/06-reviewdse-search/README.md](artifacts/06-reviewdse-search/README.md).

## 3. Evaluate the results

`make reproduce-table4` and `make reproduce-table6` evaluate their fresh
outputs automatically. Table 4 can be summarized again after all runs finish:

```bash
make summarize-table4
```

Results are written under:

```text
../dpl_evolve_state/paper_reproduction/
```

Evaluation checks successful EDA execution, placement legality, runtime, and
numerical agreement within the documented tolerances. See
[docs/expected-results.md](docs/expected-results.md) for the output fields and
acceptance rules.

## More information

- [Reviewer walkthrough](docs/reviewer-walkthrough.md)
- [Environment setup](docs/environment.md)
- [Troubleshooting](docs/troubleshooting.md)
- [Optional Web Demo](web-demo/README.md)

Run `make help` for all supported commands. See [LICENSE](LICENSE) for license
information.
