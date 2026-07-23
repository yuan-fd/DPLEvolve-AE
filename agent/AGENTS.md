# Automation Rules

These rules apply to agents operating in this repository.

## Execution interface

- Use `scripts/agent/run_artifact.sh` for fixed machine-facing stages.
- Use root Make targets or `artifacts/*/reproduce.sh` only when a task recipe
  requires arguments not exposed by the dispatcher.
- Run `make doctor` before environment preparation.
- Use one `CASE=` or `--dry-run` before launching a large campaign.

## Immutable inputs

Never modify:

- `artifacts/*/inputs/`;
- `artifacts/*/expected/`;
- `artifacts/01-table4-qor/selected-programs/inputs/`;
- `configs/reproduction/`; or
- `provenance/`.

Fresh outputs belong under `DPL_EVOLVE_STATE_ROOT`, `PAPER_DATA_ROOT`, or the
sibling ORFS workspace. Never change an expected value to make a result pass.

## Experiment rules

- Table 4 replay is fresh OpenROAD execution, not a packaged-number check.
- Table 6 requires its published package; the agent dispatcher fetches and
  verifies it before execution.
- Table 5 returns `BLOCKED` while the exact configuration and six sources
  listed in `docs/table5-status.md` are missing. Never substitute another
  SWERV configuration or retained numbers.
- ReviewDSE model-backed search requires explicit user authorization and the
  repository cost acknowledgement. Planning commands make no model calls.
- `make toolchain-smoke` is a non-paper diagnostic under `tests/toolchain/`.

## Result acceptance

Report successful execution, legality, canonical trajectory metrics, runtime,
and the generated numerical verdict. Hash availability is provenance metadata,
not a replacement for scientific execution.

## Failure reporting

Record the exact command, manifest, exit code, result directory, and first
failure. Distinguish `BLOCKED` missing data from environment failure and from a
completed run outside its numerical acceptance window.
