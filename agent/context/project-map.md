# Project map

```text
artifacts/
  01-table4-qor/          Table 4 records and 18 selected source programs
  02-table5-composability/ Table 5 compact counterexamples
  03-table6-cutrow/       Table 6 compact cut-row outcomes
  04-aes-smoke/           Optional one-case toolchain diagnostic
  05-figures/             Figure 4/5 reproduction wrapper
  06-reviewdse-search/    Level 1/2 search wrapper
  07-ariane-diagnostic/   Six-source diagnostic wrapper
configs/reproduction/     Paper experiment contract and case plan
src/
  dpl_evolve_agent/       Bundled research implementation
scripts/
  reproduce/              Fresh paper experiment entry points
  human/                  Environment preparation for reviewers
  agent/                  Stable machine dispatcher and validation
  shared/                 Cross-path runtime utilities
  maintenance/            Provenance, token, and release tooling
docs/                      Human-facing guides
agent/                     Machine-facing instructions and schemas
schemas/                   Public experiment-config schema
provenance/                Source locks and integrity records
paper/                     Paper metadata (PDF not tracked)
```

## Entry points

| Intent | Entry point |
|---|---|
| Prepare paper inputs | `make prepare-paper-inputs` |
| Table 4 default/BO/replay | `make reproduce-table4` |
| Table 6 cut-row replay | `make reproduce-table6` |
| Small real DSE | `make run-dse-small` |
| Environment inspection | `scripts/agent/inspect_environment.sh` |
| Repository validation | `make test` |
| Release-package check | `make zenodo-audit` |

## Dependency direction

Archive verifiers read only their own bundles. Fresh reproduction wrappers call
the bundled framework and sibling ORFS workspace and write generated state only
outside immutable expected-value directories.
