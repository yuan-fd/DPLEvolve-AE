# Table 4: QoR comparison

This archived bundle checks the paper's retained nine-case records among the OpenROAD
default, black-box BO-DSE, ReviewDSE-HPWL, and runtime-aware ReviewDSE-GHR
columns. It also verifies the integrity of the 18 selected HPWL/GHR source
programs.

Run this compact audit from the repository root:

```bash
bash artifacts/01-table4-qor/run.sh
```

The command uses Python's standard library and makes no EDA, network, API, or
LLM call.

## What the command checks

For BO-DSE, `verify.py` reads 400 normalized trial records for each of nine
cases. It checks the recorded seed and trial count, confirms that `best.json`
selects the minimum valid score, recomputes the selected HPWL delta, and then
aggregates the nine case results.

For ReviewDSE, it reads the HPWL and GHR selected-candidate manifests and their
metric JSON files. It recomputes final-versus-default HPWL deltas, checks the
archived legality, runtime, and iteration fields, and aggregates the nine
selected candidates in each column.

Finally, `selected-programs/verify.py` hashes every file in the nine HPWL and
nine GHR source trees and compares each tree digest with the release manifest.

Expected summary values are:

```text
BO-DSE:          0.3813% mean HPWL reduction
ReviewDSE-HPWL:  1.7840% mean reduction at 1.3367x runtime
ReviewDSE-GHR:   1.6761% mean reduction at 1.1103x runtime
Selected source programs: 18/18 digests match
```

Generated CSV and JSON reports are written to `output/`. Packaged inputs are
read-only.

## Evidence boundary

The BO result is regenerated from normalized per-trial records; the 3,600 EDA
runs themselves are not repeated. The ReviewDSE columns are regenerated from
the records for the selected candidates, not from the complete search
populations.

The selected source programs are integrity-checked but are not compiled by this
command. For fresh execution, regenerate the nine ODBs and run:

```bash
make reproduce-table4
```

That command reruns the default, all 3,600 BO trials, and both selected-source
tracks. Only AES Nangate45 currently has a retained paper-time ODB checksum;
the other eight regenerated inputs are revision-recorded but cannot yet be
claimed bit-identical to the paper-time inputs. Single-program details are in
[`selected-programs/README.md`](selected-programs/README.md).

## Directory guide

- `inputs/bo_paper/`: nine 400-trial BO record sets.
- `inputs/reviewdse/`: ReviewDSE selection manifests and selected metrics.
- `selected-programs/inputs/programs/`: 18 selected source trees.
- `config/`: recorded paper protocol configurations.
- `expected/`: values transcribed from the reviewed paper.
- `output/`: generated reports; safe to delete and regenerate.
