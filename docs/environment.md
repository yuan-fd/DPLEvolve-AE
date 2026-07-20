# Environment Configuration

This document records the exact tool versions used to produce the results
in this artifact. All versions are pinned to prevent bit-rot.

## Required Tools

### Evidence Verification

| Tool | Minimum Version | Tested Version | Check Command |
|------|----------------|----------------|---------------|
| Python | 3.11 | 3.11.9 | `python3 --version` |
| GNU Make | 4.0 | 4.3 | `make --version` |
| Bash | 4.0 | 5.1 | `bash --version` |

Run `make check` to verify your environment against these requirements.

### Smoke Flow (Auto-Installed)

The following tools are fetched and built automatically by
`make bootstrap && make setup`. Exact commit hashes are recorded in
`provenance/source-commits.json`.

| Tool | Version / Commit | Build Time |
|------|-----------------|------------|
| Yosys | 0.41 | ~5–10 min |
| OpenROAD | v2.0-17598 | ~15–25 min |

## Dependency Resolution

All Python dependencies for evidence verification are from the standard
library. No pip packages are required.

Smoke flow dependencies are resolved by the build system during `make setup`.
If a build fails, check that your system has:

- A C++17 compiler (GCC ≥ 9 or Clang ≥ 12)
- CMake ≥ 3.16
- Tcl/Tk development headers (for Yosys)
- Boost development libraries (for OpenROAD)

On Ubuntu 22.04:

```bash
sudo apt-get install build-essential cmake tcl-dev tk-dev \
  libboost-all-dev libeigen3-dev libspdlog-dev
```
