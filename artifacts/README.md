# Archived evidence bundles

These directories preserve compact paper records, expected values, provenance,
and the 18 selected Table 4 source trees. Their `run.sh` files audit archived
data; they are not the primary fresh-experiment interface. Use the root
`Makefile` and `scripts/reproduce/` for paper reproduction.

| Bundle | Paper item | Command | Typical time |
|---|---|---|---:|
| [`01-table4-qor/`](01-table4-qor/) | Table 4 QoR comparison and selected programs | `bash artifacts/01-table4-qor/run.sh` | Under 1 second |
| [`02-table5-composability/`](02-table5-composability/) | Table 5 stage-composability counterexamples | `bash artifacts/02-table5-composability/run.sh` | Under 1 second |
| [`03-table6-cutrow/`](03-table6-cutrow/) | Table 6 hard cut-row repair | `bash artifacts/03-table6-cutrow/run.sh` | Under 1 second |
| [`04-aes-smoke/`](04-aes-smoke/) | Optional AES toolchain diagnostic | `bash artifacts/04-aes-smoke/run.sh --run` | Machine dependent |

The first three bundles inspect packaged records and require no EDA tools. The
smoke bundle invokes EDA but does not reproduce a reported paper experiment.

Generated files stay in each bundle's `output/` directory, except for the
smoke flow, whose EDA products are written to a new timestamped directory in
the sibling ORFS workspace.
