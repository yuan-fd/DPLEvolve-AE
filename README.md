# DPLEvolve Artifact Evaluation

[![MLCAD 2026](https://img.shields.io/badge/MLCAD-2026-blue)](https://mlcad.org/)
[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)

This repository accompanies our MLCAD 2026 paper on using LLM agents to
explore OpenROAD detailed-placement source mechanisms. It is organized around
four independent artifact bundles, so a reviewer can inspect or run one paper
result without first learning the complete ReviewDSE framework.

If you are reviewing the artifact, start with `make evidence`. It checks the
reported tables and the integrity of the selected programs without rebuilding
OpenROAD or invoking an LLM. The reviewed paper is included at
[`paper/OpenROAD_Evolve.pdf`](paper/OpenROAD_Evolve.pdf).

## Quick start

From the repository root, run:

```bash
make evidence
```

The command takes seconds and performs these checks:

- Table 4 BO: selects the minimum-score valid trial for each case from 3,600
  normalized trial records, recomputes nine HPWL deltas, and aggregates them.
- Table 4 ReviewDSE: recomputes the two reported columns from the archived
  selected-candidate manifests and metric records.
- Table 5: recomputes six percentages from the three compact archived HPWL
  rows and compares them with the paper transcription.
- Table 6: compares nine compact archived status/runtime rows with the paper
  transcription, including each archived legality result.
- Selected programs: hashes all files in the nine HPWL and nine GHR source
  trees and compares the digests with the release manifest.

A successful run includes `Paper claim check: PASS`, `9/9 archived cut-row
rows`, `3/3 stage-local counterexamples`, and `18 HPWL/GHR frozen source
programs`. Generated reports stay inside each bundle's ignored `output/`
directory; packaged inputs are not modified.

The [claims-to-artifacts map](docs/claims-to-artifacts.md) traces each check to
its inputs and explains where the evidence chain stops.

## Optional OpenROAD smoke test

To exercise a real pinned EDA flow on AES Nangate45:

```bash
make bootstrap
make setup
make smoke
```

`make bootstrap` creates a sibling `OpenROAD-flow-scripts` checkout at the
recorded source trees and requires network access. Omit it only when that
sibling workspace is already prepared at the required revisions. `make setup`
builds the pinned Yosys and OpenROAD binaries; `make smoke` creates a new
timestamped ORFS result directory, regenerates the AES input, and runs the
native OpenROAD detailed-placement baseline.

Success is reported as `[OK] AES smoke test PASSED`. This fresh execution
checks the input hash, instance count, HPWL, and placement legality. It does
not run a selected ReviewDSE program or reproduce all nine Table 4 cases.

## Artifact bundles

| Bundle | Reviewer command | Evidence type |
|---|---|---|
| [Table 4 QoR](artifacts/01-table4-qor/) | `make table4` | Recomputed archived records and selected-source integrity |
| [Table 5 composability](artifacts/02-table5-composability/) | `make table5` | Compact archived-summary verification |
| [Table 6 cut-row repair](artifacts/03-table6-cutrow/) | `make table6` | Compact archived-summary verification |
| [AES smoke flow](artifacts/04-aes-smoke/) | `make smoke` | Fresh pinned one-case EDA execution |

Every bundle contains its own README, entry script, inputs or input-generation
instructions, expected values, and output location.

## Reproduction scope

| Paper-facing item | Available check | Limitation |
|---|---|---|
| Table 4 BO column | Regenerated from 3,600 normalized trial records | The EDA trials are not rerun |
| Table 4 ReviewDSE columns | Regenerated from 18 selected-candidate records | The full candidate populations are not re-evaluated |
| Table 5 counterexamples | Percentages regenerated from a compact original summary | Per-run EDA logs are not packaged |
| Table 6 cut-row results | Statuses and runtimes checked against compact original summaries | No fresh OpenROAD or legality-checker replay |
| 18 selected source programs | Source-tree integrity check | No compilation or numerical replay without the original ODB inputs |
| AES Nangate45 default flow | Fresh pinned Yosys/OpenROAD execution | One default-baseline case only |

The original nine paper-time `place_batch_20260421_220319` ODB inputs were not
retained. Exact selected-program replay is therefore unavailable. Repeating
the Teacher/Student discovery search would also require authenticated model
access and the original search budget; it is outside the artifact review path.

## Requirements

The evidence-only path requires Linux x86-64, Bash, GNU Make, Python 3.11 or
newer, and write access to the artifact `output/` directories. Its Python
scripts use only the standard library. It requires no network connection, EDA
installation, GPU, API key, or sibling repository.

The OpenROAD path additionally needs Git, GCC 9+, CMake 3.20+, about 10 GB of
disk space, and 16 GB of RAM; four or more CPU cores are recommended. Network
access may be needed by bootstrap and setup. See the
[environment guide](docs/environment.md) for pinned versions and build details.

## Documentation

- To trace a paper number to its input records and checker, use the
  [claims-to-artifacts map](docs/claims-to-artifacts.md).
- To follow clean-machine setup step by step, use the
  [quick-start guide](docs/quickstart.md).
- To compare concrete values and tolerances, see
  [expected results](docs/expected-results.md).
- To inspect tool revisions and checksums, see
  [source revisions](provenance/source-commits.json).
- To diagnose a failed setup or smoke run, see
  [troubleshooting](docs/troubleshooting.md).
- Human-facing guides live in [`docs/`](docs/). Machine-facing instructions
  and schemas live separately in [`agent/`](agent/); automation enters through
  [`scripts/agent/run_artifact.sh`](scripts/agent/run_artifact.sh).

Run `make help` for the remaining reviewer and maintenance commands.

## Citation and license

Machine-readable citation metadata is provided in
[`CITATION.cff`](CITATION.cff). The artifact is released under the BSD
3-Clause License; see [`LICENSE`](LICENSE).
