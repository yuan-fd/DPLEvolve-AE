# Script interfaces

The root Makefile is the stable reviewer interface. This directory separates
the implementations by audience and responsibility.

```text
scripts/
├── human/          host diagnosis, bootstrap, build, and environment checks
├── reproduce/      fresh paper-experiment implementations
├── agent/          fixed machine-facing dispatcher and inspection helpers
├── maintenance/    data export, provenance, token, and release tooling
└── shared/         common environment and utility functions
```

## Human reviewer scripts

Reviewers normally use `make doctor`, `make reviewer-prepare`, experiment
`make reproduce-*` targets, or the Web Demo. `scripts/human/` contains the
underlying setup helpers and never starts a paper experiment implicitly.

## Fresh experiment scripts

`scripts/reproduce/` implements Table 4 baselines/replay, Table 5/6, figures,
Level 1/2 search, and the Ariane diagnostic. Each paper experiment directory
under `artifacts/` has a short `reproduce.sh` wrapper that calls these stable
Make targets.

## Automation-agent scripts

`scripts/agent/run_artifact.sh` accepts fixed artifact IDs and invokes their
fresh experiment wrappers. Paid search is never launched by this dispatcher.
Machine instructions live under `agent/`.

## Maintainer scripts

`scripts/maintenance/` is not part of the reviewer workflow. It exports large
data packages, prepares release archives, and records release provenance.
Reviewer commands must not depend on private maintainer state.

Generated outputs belong under ORFS, `DPL_EVOLVE_STATE_ROOT`, or
`PAPER_DATA_ROOT`, never inside an immutable expected-value directory.
