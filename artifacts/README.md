# Artifact bundles

Each directory below is a reviewer-facing unit with its own entry point,
inputs, expected values, provenance, and generated output directory. You can
run one bundle without first learning the ReviewDSE framework.

| Bundle | Paper item | Command | Typical time |
|---|---|---|---:|
| [`01-table4-qor/`](01-table4-qor/) | Table 4 QoR comparison and selected programs | `bash artifacts/01-table4-qor/run.sh` | Under 1 second |
| [`02-table5-composability/`](02-table5-composability/) | Table 5 stage-composability counterexamples | `bash artifacts/02-table5-composability/run.sh` | Under 1 second |
| [`03-table6-cutrow/`](03-table6-cutrow/) | Table 6 hard cut-row repair | `bash artifacts/03-table6-cutrow/run.sh` | Under 1 second |
| [`04-aes-smoke/`](04-aes-smoke/) | Fresh pinned AES Nangate45 EDA flow | `bash artifacts/04-aes-smoke/run.sh --run` | About 2-5 minutes after setup |

The first three bundles inspect packaged records and require no EDA tools or
network access. The smoke bundle is a fresh execution and requires the pinned
ORFS, Yosys, and OpenROAD environment.

Generated files stay in each bundle's `output/` directory, except for the
smoke flow, whose EDA products are written to a new timestamped directory in
the sibling ORFS workspace.
