# Experiment semantics

## `table4`

Runs `artifacts/01-table4-qor/run.sh`. It recomputes the BO and ReviewDSE
summary columns and verifies 18 selected source-tree digests. BO starts from
per-trial records; ReviewDSE starts from selected-candidate records. Neither
path launches EDA or an LLM.

## `table5`

Runs `artifacts/02-table5-composability/run.sh`. It recomputes percentage
changes for three compact archived rows and verifies that each improves the
local legalization stage while worsening the final post-DPL result.

## `table6`

Runs `artifacts/03-table6-cutrow/run.sh`. It checks nine archived hard cut-row
outcomes, selected runtimes, and archived legality fields against the paper
transcription. It does not invoke `check_placement` on a new design database.

## `smoke`

Defaults to validating the prepared AES reference run. With `--run-smoke`, it
regenerates one Nangate45 input, executes native OpenROAD detailed placement,
and validates hashes, instance data, HPWL, error state, and legality.

## Unsupported semantics

The dispatcher has no IDs for ablations, multi-seed search, full DSE, or exact
nine-case selected-program replay. An agent must reject requests to imply that
these are packaged, even if legacy local files exist outside the public tree.
