# Troubleshooting

Common issues encountered during artifact evaluation and their solutions.

## Start With Doctor

```bash
make doctor
```

If GNU Make itself is missing, invoke the same check directly:

```bash
bash scripts/human/doctor.sh
```

Doctor is read-only. It works on a fresh checkout before ORFS exists and groups
the result into evidence, web-console, rebuild-tool, and prepared-smoke
readiness. When a command is missing it prints a distro-specific command but
does not run it. Review that command in a normal terminal, run it manually if
permitted, and repeat Doctor.

For the complete source-rebuild path:

```bash
make doctor-smoke
```

## Environment Issues

### `python3: command not found`

Install Python 3.11 or later, or override the interpreter:

```bash
DPL_EVOLVE_PYTHON=/path/to/python3 make evidence
```

### `make: command not found`

On Ubuntu:

```bash
sudo apt-get install build-essential
```

On CentOS/RHEL:

```bash
sudo yum install make
```

### `make check` reports missing tools

`make check` inspects an environment that has already been prepared. On a fresh
checkout, use `make doctor` instead. See `docs/environment.md` for minimum
versions, install only the missing tools reported by Doctor, then proceed.

## Evidence Verification Issues

### Digest or claim mismatch

This indicates the expected values in `artifacts/*/expected/` have been
modified or the evidence bundles are corrupted.

1. Run `git status` to see which files changed.
2. Do **not** manually edit files under `expected/` — these are the
   authoritative reference values.
3. If evidence bundles were corrupted, re-extract them from the original
   archive or re-clone the repository.

### `Permission denied` on verification scripts

Scripts do not require executable permissions. Run them via Bash:

```bash
bash artifacts/01-table4-qor/run.sh
```

## Smoke Flow Issues

### `make bootstrap` fails

Check your internet connection. `bootstrap` clones Yosys and OpenROAD
from their upstream repositories. If behind a firewall, configure your
Git proxy settings before running.

### `make setup` fails during compilation

1. Verify you have the build dependencies installed (see `docs/environment.md`).
2. Check that your system has at least 8 GB of free RAM.
3. Try reducing build parallelism: `JOBS=2 make setup`

### `make smoke` reports hash mismatch

The smoke output does not match the expected values. Verify:

1. Tool commits match `provenance/source-commits.json`:
   ```bash
   git -C ../OpenROAD-flow-scripts log --oneline -1
   git -C ../OpenROAD-flow-scripts/tools/OpenROAD log --oneline -1
   git -C ../OpenROAD-flow-scripts/tools/yosys log --oneline -1
   ```
2. If commits differ, re-run `make bootstrap` to fetch the correct versions.

### OOM (Out of Memory) during smoke

Reduce thread count:

```bash
SMOKE_THREADS=1 make smoke
```

If the issue persists, ensure your machine meets the minimum requirement
of 8 GB RAM.

### Smoke cannot find ORFS

The ORFS directory is expected at `../OpenROAD-flow-scripts` relative to
the repository root. If your ORFS checkout is elsewhere:

```bash
ORFS_ROOT=/path/to/your/OpenROAD-flow-scripts make smoke
```

Or run `make bootstrap` to let the build system handle this automatically.

## Still Stuck?

File an issue at the project's GitHub repository or contact the authors
listed in the README.
