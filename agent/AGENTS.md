# Automation rules

These rules apply to agents operating in this repository.

## Stable interface

Run supported artifacts through:

```bash
bash scripts/agent/run_artifact.sh --artifact table4|table5|table6|smoke
```

The smoke artifact is check-only unless `--run-smoke` is explicit. Use
`--dry-run` before an expensive or environment-dependent command.

## Read-only evidence

Never modify files under:

- `artifacts/*/inputs/`
- `artifacts/*/expected/`
- `artifacts/01-table4-qor/selected-programs/inputs/`
- `provenance/`
- `paper/`

Generated records belong only in the relevant `output/` directory. Do not
change an expected value to turn a failing comparison into a pass.

## Supported claims

- Table 4 BO is recomputed from 3,600 normalized records.
- Table 4 ReviewDSE is recomputed from selected-candidate records.
- Tables 5 and 6 are compact archived-summary checks.
- The 18 selected programs receive an integrity check by default.
- AES smoke is the only supported fresh EDA execution.

Do not describe archived-summary checks as fresh EDA reproduction. Do not
claim exact selected-program replay: the original nine ODB inputs are absent.
Do not launch a full LLM search from this repository.

## Validation sequence

For a repository-wide audit, run:

```bash
make evidence
make test
make validate-configs
```

Run `make smoke-check` only when the pinned ORFS workspace exists. Run
`make zenodo-audit` for archive-content validation; a formal `make zenodo`
release must remain blocked until real author metadata is installed.

## Failure reporting

Record the artifact ID, exact command, exit code, and complete output. Separate
environment failures from evidence mismatches. A missing dependency or ODB is
not evidence that a paper value is wrong, and a digest mismatch is not safe to
repair automatically.
