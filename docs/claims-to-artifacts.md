# Claims-to-artifacts map

This map states exactly what each reviewer command establishes and where the
evidence chain ends.

| Paper item | Bundle and command | Packaged evidence | What is recomputed | Boundary |
|---|---|---|---|---|
| Table 4 BO-DSE | `artifacts/01-table4-qor/`; `make table4` | 3,600 normalized trial rows, selected-trial records, paper transcription | Best valid trial per case, nine deltas, aggregate | Does not rerun the 3,600 EDA trials |
| Table 4 ReviewDSE-HPWL/GHR | `artifacts/01-table4-qor/`; `make table4` | 18 selected-candidate metric records and manifests | Per-case deltas, runtimes, legality fields, aggregates | Complete search populations are not packaged |
| Selected programs | `artifacts/01-table4-qor/selected-programs/` | Nine HPWL and nine GHR source trees plus digest manifest | All source-tree SHA-256 digests | Numerical replay needs the deleted paper-time ODB inputs |
| Table 5 | `artifacts/02-table5-composability/`; `make table5` | Three compact HPWL rows and provenance | Six percentage changes and counterexample predicates | Original EDA logs are not packaged |
| Table 6 | `artifacts/03-table6-cutrow/`; `make table6` | Nine compact status/runtime rows and provenance | Row equality, selected runtime, archived legality | Cut-row ODBs and binaries are not packaged |
| AES smoke | `artifacts/04-aes-smoke/`; `make smoke` | Reproduction lock and generated input recipe | Fresh synthesis, placement, metrics, and legality | One default OpenROAD case, not a ReviewDSE replay |

## Table 4 details

The BO checker validates 400 records for each of nine cases, rejects invalid
records when selecting the minimum score, verifies the recorded winner, and
recomputes the paper aggregate. The ReviewDSE checker starts from the archived
selected candidates. It does not claim to reconstruct or re-rank candidates
that were not retained.

The 18 selected source programs are useful for source inspection and integrity
verification. Their manifest proves that the released trees match the archived
selection, not that they can be numerically replayed without their exact input
databases.

## Tables 5 and 6 details

These bundles intentionally describe themselves as archived-summary checks.
Their provenance files identify the original summary and record its digest.
The verifier independently recalculates values from the compact rows and
compares them with the paper transcription, but it cannot replace missing
per-run logs or design databases.

## Unsupported reproduction claims

The repository does not claim exact nine-case selected-program replay or full
Teacher/Student search reproduction. The original nine paper-time ODB inputs
were deleted because they occupied several terabytes. Full search would also
require the original model access, candidate budget, tool execution budget,
and complete intermediate state.
