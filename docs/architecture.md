# Repository architecture

The repository separates reviewer commands, experiment implementations,
machine-facing instructions, and maintainer-only release tools.

```text
README.md                         human entry point
Makefile                          stable reviewer command interface
artifacts/<experiment>/           one package per paper experiment
configs/reproduction/             machine-readable paper protocol
scripts/human/                    environment setup and diagnosis
scripts/reproduce/                fresh experiment implementations
scripts/agent/                    fixed automation dispatcher
scripts/maintenance/              export/release tools, not reviewer flow
src/dpl_evolve_agent/             evaluator and Teacher/Student framework
agent/                             automation-agent rules, contexts, schemas
docs/                              reviewer and maintainer documentation
web-demo/                          browser frontend to fixed Make targets
```

## Experiment package contract

Every paper experiment directory contains:

```text
README.md        experiment purpose, command, and boundary
reproduce.sh     direct fresh-execution wrapper
inputs/          retained inputs or input-location/generation documentation
expected/        paper values and acceptance interpretation
output/          generated-output contract and placeholder
```

## Fresh execution contract

A fresh experiment must:

1. create or install the required physical-design input;
2. build the fixed or evolved OpenROAD source when applicable;
3. run OpenROAD rather than read an archived observed value;
4. emit canonical HPWL stages, legality, displacement, runtime, and status;
5. compare the new result with a documented numerical/qualitative criterion;
6. write products outside immutable `expected/` directories.

Paper-time hashes are useful provenance but not a prerequisite for execution.
When unavailable, the result is judged by complete legal execution and
scientific tolerance and is labeled numerical rather than bit-for-bit replay.

## Generated-state layout

- `$ORFS_ROOT/flow/{results,reports,logs}`: placement inputs and OpenROAD runs;
- `$DPL_EVOLVE_STATE_ROOT`: builds, BO trials, DSE rounds, and summaries;
- `$PAPER_DATA_ROOT`: separately distributed Table 6 inputs and any future
  Table 5 recovery.

The Git checkout remains small and does not contain multi-terabyte ODB/result
trees.

## Human and agent separation

Human reviewers use `README.md`, `docs/reviewer-walkthrough.md`, the root
Makefile, experiment `reproduce.sh` wrappers, and the Web Demo.

Automation agents use `agent/AGENTS.md`, bounded recipes under `agent/tasks/`,
and the fixed commands under `scripts/agent/`. Agents call the same Make targets
and may not rewrite expected values or bypass cost gates.

Maintainers use `scripts/maintenance/` and `docs/maintainers/`. Reviewer paths
must never depend on maintainer-only or locally ignored material.
