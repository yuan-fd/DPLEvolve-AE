# Environment

## Evidence-only path

The default reviewer command, `make evidence`, requires:

| Requirement | Minimum |
|---|---|
| Operating system | Linux x86-64 |
| Shell | Bash 4 or newer |
| Build driver | GNU Make |
| Python | 3.11 or newer, standard library only |
| Disk | Less than 1 GB beyond the checkout |
| Network | Not required |

Run `python3 --version`, `bash --version`, and `make --version` if the command
fails before a verifier starts.

## AES smoke path

The optional `make smoke` path additionally needs Git, CMake 3.20 or newer,
GCC 9 or newer, approximately 10 GB of free disk, and 16 GB of RAM. Four or
more CPU cores are recommended. Bootstrap and dependency installation may need
network access.

The source revisions and prepared tree hashes are recorded in
`provenance/source-commits.json`. The expected input hash, binary hashes,
metrics, and tolerances are recorded in
`artifacts/04-aes-smoke/expected/ae_reproduction_lock.json`.

The default layout is:

```text
workspace/
  DPLEvolve-AE/
  OpenROAD-flow-scripts/
  dpl_evolve_state/
```

The ReviewDSE implementation itself is bundled at
`framework/dpl_evolve_agent/`; it is not a required sibling repository.

You may override paths before running setup:

```bash
export ORFS_ROOT=/path/to/OpenROAD-flow-scripts
export DPL_EVOLVE_STATE_ROOT=/path/to/dpl_evolve_state
export DPL_EVOLVE_PYTHON=/path/to/python
make check
```

Machine-local values are written outside tracked evidence. Do not commit
credentials, API keys, compiled binaries, or generated ODB files.
