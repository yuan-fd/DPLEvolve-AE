# Repository architecture

The repository separates executable paper experiments from compact archived
records.

```text
configs/reproduction/     paper-derived machine-readable experiment contract
scripts/reproduce/        fresh default, BO, replay, Level 1/2, Table 5/6 paths
src/dpl_evolve_agent/     protected evaluator and Teacher/Student framework
artifacts/                archived records, reference values, selected sources
images/                   repository figures
docs/                     reviewer and maintainer guidance
agent/                    machine-facing instructions
provenance/               pinned source revisions
```

## Fresh-execution contract

The primary public interface is the root `Makefile`, which delegates to
`scripts/reproduce/`. A fresh experiment must:

1. start from a recorded/checksummed design state (ODB or cut-row DEF/V/SDC);
2. build the required fixed or edited OpenROAD source;
3. run the protected detailed-placement evaluator;
4. emit status, `H_g/H_lg/H_ip/H_f`, legality, displacement, runtime, and
   source/binary/metric provenance;
5. write new products under `DPL_EVOLVE_STATE_ROOT` and ORFS, never under an
   immutable `expected/` directory.

Missing exact inputs are a blocking error. A fresh path must not read an
archived expected value as its observed result.

## Archived-bundle contract

Each directory under `artifacts/` contains compact provenance/reference data:

```text
README.md   scope and evidence boundary
run.sh      independent archive-audit entry point
inputs/     compact retained records or generation notes
expected/   paper transcription or diagnostic lock
output/     ignored generated audit reports
```

The 18 selected Table 4 source trees are executable inputs and are reused by
the fresh replay path. The Table 4/5/6 bundle `run.sh` files themselves only
audit retained records and are deliberately labeled non-reproduction.

## External EDA state

Large generated ODBs, builds, BO trials, and DSE workspaces live outside the
Git checkout:

- `$ORFS_ROOT/flow/{results,reports,logs}` for EDA products;
- `$DPL_EVOLVE_STATE_ROOT` for builds, matrices, DSE rounds, and summaries;
- `$PAPER_DATA_ROOT` for the retained Table 6 DEF/V/SDC/source package and any
  future recovery of the missing Table 5 SWERV config and six source trees.

This keeps a clone small without pretending that compact JSON/TSV files are the
experiment.

## Human and machine interfaces

Reviewers use `README.md`, `make help`, and `docs/`. Automation agents use
`agent/` and may call the same Make targets or reproduction wrappers. Both
interfaces share the same execution contract and blocking semantics.

`extras/unsupported/` is a local ignored preservation area. Public commands,
tests, and releases must not depend on it.
