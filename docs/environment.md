# Environment and resource planning

## Author reference machine

The paper experiments were run on:

- Rocky Linux 8.10, x86-64;
- 2 × Intel Xeon Platinum 8462Y+;
- 64 physical / 128 logical CPU cores;
- 314 GiB RAM;
- 22 TiB `/home` filesystem;
- no GPU and no commercial EDA license.

This is a reproducibility reference, not a fixed minimum. Resource use varies
substantially by design and concurrency. OpenROAD alone can exceed 2 GiB on a
modest case; parallel compilation, four concurrent BO trials, and large
Ariane/SWERV/BPQUAD inputs require server-class memory and storage. The complete
campaign is not advertised for an 8/16 GiB RAM or 10 GiB disk host.

On a smaller machine:

- start with `CASE=aes_nangate45`;
- reduce `THREADS`;
- run one case at a time;
- avoid concurrent BO/case execution; and
- monitor peak memory and free disk.

## Required host software

The pinned build uses Bash 4+, GNU Make 4+, Python 3.11+, Git, rsync, CMake,
GCC/G++, bison, flex, SWIG, and the OpenROAD/Yosys build dependencies.

```bash
make doctor
```

Doctor is read-only and prints Rocky/RHEL-family package suggestions for
missing commands. The artifact never invokes `sudo`; reviewers should inspect
the suggestion and use their normal administrator process.

## Pinned EDA workspace

```bash
make bootstrap
make build-tools THREADS=16
make check
```

`bootstrap` creates the sibling ORFS/OpenROAD workspace at revisions recorded
in `provenance/source-commits.json` and applies the DPLEvolve patches.
`build-tools` compiles Yosys/OpenROAD and prepares Python environments. `check`
inspects the prepared revisions, binaries, and shared libraries.

## Path configuration

Defaults are sibling directories of the repository:

```text
ORFS_ROOT=../OpenROAD-flow-scripts
DPL_EVOLVE_STATE_ROOT=../dpl_evolve_state
PAPER_DATA_ROOT=./paper-data
```

Override them on the command line or in an untracked `env.local.sh`:

```bash
export ORFS_ROOT=/path/to/OpenROAD-flow-scripts
export DPL_EVOLVE_STATE_ROOT=/path/to/dpl_evolve_state
export PAPER_DATA_ROOT=/path/to/paper-data
```

Generated ODBs and OpenROAD logs live under `$ORFS_ROOT/flow`. Builds, BO
trials, DSE rounds, and summaries live under `$DPL_EVOLVE_STATE_ROOT`. External
Table 6 data lives under `$PAPER_DATA_ROOT`. Fresh experiments never overwrite
`artifacts/*/expected/`.

## Record for an evaluation report

Record the Git commit, OS, CPU model, physical/logical cores, RAM, filesystem,
thread count, external-data version, and observed peak resource use. Numerical
drift should be reported with the fresh metrics rather than hidden by changing
an expected value.
