# Quick start

This guide gives reviewers the shortest path to each supported result. All
commands are run from the repository root.

## Archived evidence

Requirements: Linux x86-64, Bash, GNU Make, and Python 3.11 or newer. No
network, EDA tool, GPU, API key, or Python package is needed.

```bash
make evidence
```

This runs the Table 4, Table 5, and Table 6 bundles. A normal run takes a few
seconds. To inspect one result only, use one of:

```bash
make table4
make table5
make table6
```

Each command reads only its own `artifacts/<bundle>/inputs/` and `expected/`
directories. Generated reports stay in that bundle's ignored `output/`
directory. `make clean` removes generated reports without touching evidence.

## Fresh AES smoke flow

This optional path builds and runs a pinned Yosys/OpenROAD environment. It
needs network access during bootstrap, about 10 GB of disk, 16 GB of RAM, and
four or more CPU cores.

```bash
make bootstrap
make setup
make smoke
```

`make bootstrap` creates a sibling `OpenROAD-flow-scripts` workspace at the
recorded source trees. `make setup` builds the pinned tools. `make smoke`
regenerates the AES Nangate45 input, runs native OpenROAD detailed placement,
and checks the result against the reproduction lock.

Expected completion message:

```text
[OK] AES smoke test PASSED
```

To validate the prepared reference result without starting a new run:

```bash
make smoke-check
```

## Machine-facing entry point

Automation should use the stable dispatcher instead of parsing this guide:

```bash
bash scripts/agent/run_artifact.sh --artifact table4
```

See `agent/README.md` for the accepted artifact IDs and manifest behavior.

## If a command fails

Run `make check` for the EDA environment, then consult
[`troubleshooting.md`](troubleshooting.md). When reporting a problem, include
the command, complete terminal output, operating system, and Python version.
