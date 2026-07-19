# Troubleshooting

## `make evidence` cannot find Python

Install Python 3.11 or newer, or select an interpreter explicitly:

```bash
DPL_EVOLVE_PYTHON=/path/to/python make evidence
```

The evidence verifiers use only the Python standard library.

## Permission denied when running a bundle

The documented commands invoke scripts through Bash and work even if archive
extraction removed executable bits:

```bash
bash artifacts/01-table4-qor/run.sh
```

Repository tests still check executable bits because Git preserves them.

## A digest or paper-claim check fails

Do not edit the expected value to make the check pass. Run `git status`, then
compare the affected `inputs/` or `expected/` file with the released archive.
Generated files belong only in `output/`.

## `make smoke-check` cannot find ORFS

The smoke path requires a sibling ORFS workspace. Prepare it with:

```bash
make bootstrap
make setup
make check
```

If the workspace is elsewhere, set `ORFS_ROOT`. See
[`environment.md`](environment.md) for all overrides.

## Smoke input hash mismatch

The generated AES ODB does not match the reproduction lock. Confirm both the
ORFS and nested OpenROAD prepared tree hashes in
`provenance/source-commits.json`, then regenerate the input with a new smoke
run. Do not reuse an ODB from a different commit or flow variant.

## Smoke metrics differ

Check the complete log for tool errors and confirm placement legality. Also
verify the pinned binary hashes, design configuration, input stage, and thread
count. The validator applies the tolerances in the reproduction lock; it does
not use approximate string matching.

## The selected-program replay asks for ODB inputs

This is expected. The 18 selected source trees are packaged, but the original
nine paper-time ODB inputs were deleted because they occupied several
terabytes. The default Table 4 command therefore performs source integrity and
archived-record checks only.

## Full DSE is not available

The artifact does not expose a one-command full discovery search. Such a run
would need authenticated model access, many persistent agent sessions,
hundreds of candidate generations and compilations, repeated EDA evaluations,
and the complete original intermediate state. This is outside the supported
review path.
