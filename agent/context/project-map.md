# Project map

```text
artifacts/
  01-table4-qor/          Table 4 records and 18 selected source programs
  02-table5-composability/ Table 5 compact counterexamples
  03-table6-cutrow/       Table 6 compact cut-row outcomes
  04-aes-smoke/           Fresh pinned AES EDA flow
framework/
  dpl_evolve_agent/       Bundled research implementation
scripts/
  human/                  Environment preparation for reviewers
  agent/                  Stable machine dispatcher and validation
  shared/                 Cross-path runtime utilities
  maintenance/            Provenance, token, and release tooling
docs/                      Human-facing guides
agent/                     Machine-facing instructions and schemas
schemas/                   Public experiment-config schema
provenance/                Source locks and integrity records
paper/                     Reviewed paper
```

## Entry points

| Intent | Entry point |
|---|---|
| All packaged evidence | `make evidence` |
| One supported artifact | `scripts/agent/run_artifact.sh --artifact ID` |
| Fresh AES EDA run | artifact ID `smoke` plus `--run-smoke` |
| Environment inspection | `scripts/agent/inspect_environment.sh` |
| Repository validation | `make test` |
| Archive audit | `make zenodo-audit` |

## Dependency direction

Artifact evidence scripts may read only their own bundle and Python's standard
library. The smoke artifact may call `scripts/shared/`, the bundled framework,
and the sibling ORFS workspace. Human and agent wrappers may call artifact
entry points; artifact verifiers must not call human or agent wrappers.
