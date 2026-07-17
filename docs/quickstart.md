# Quick Start Guide

This guide gets you from zero to a validated environment in under 40 minutes.

## Prerequisites

- Linux x86-64 server (RHEL/Rocky 8+, Ubuntu 20.04+, or similar)
- Git, GCC ≥ 9, CMake ≥ 3.20, GNU Make, Python ≥ 3.11
- Environment Modules (`module` command) — optional but recommended
- 10 GB free disk space
- Internet access (for initial submodule fetch)

## Step 1: Open a Terminal

SSH into your server and navigate to the artifact directory:

```bash
cd /path/to/DPLEvolve-AE
```

## Step 2: Check Your Environment

```bash
make check
```

This prints what's available and what's missing. Don't worry about warnings
yet — setup will fix most of them.

**Expected output**: A table showing your system info, toolchain versions,
Python setup, and whether Yosys/OpenROAD binaries exist.

## Step 3: Build Dependencies

```bash
make setup
```

This does (idempotently):
1. Loads required server modules
2. Verifies repository commits
3. Initializes ORFS/Yosys submodules
4. Creates a Python virtual environment
5. Builds Yosys 0.64 (if not already built)
6. Builds OpenROAD (if not already built)
7. Writes machine-local environment configuration

**Expected time**: ~30 minutes for first build, ~1 minute if already built.

**If it fails**: See `docs/troubleshooting.md`. Most failures are missing build
dependencies (Bison, Flex, etc.) — the setup script uses server modules to
find these.

## Step 4: Run the Smoke Test

```bash
make smoke
```

This validates the complete pipeline without any LLM API calls:
1. Generates the AES Nangate45 input snapshot using pinned Yosys
2. Runs the native OpenROAD detailed-placement baseline
3. Validates output against expected values

**Expected time**: ~5 minutes.

**Expected output**:
```
[OK] AES smoke test PASSED
```

**If it fails**: The most common cause is using the wrong Yosys version. Run
`make setup` to build the pinned version. See `docs/troubleshooting.md` for
detailed diagnosis.

## Step 5: (Optional) Reproduce Baselines

```bash
make reproduce-baseline
```

Runs the three canonical baseline lines on all paper cases. No API required.

**Expected time**: ~30 minutes.

## What Next?

- **All green?** Your environment is validated. You're ready to reproduce paper results.
- **Want to run the full LLM-powered DSE?** See `docs/experiments.md` for API setup.
- **Curious about expected results?** See `docs/expected-results.md`.
- **Found a problem?** See `docs/troubleshooting.md`.
