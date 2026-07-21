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

Run `make doctor` for a non-mutating first-run diagnosis. Unlike `make check`,
Doctor does not require ORFS or prebuilt EDA binaries. Run `make check` after
bootstrap/setup to inspect pinned commits, binary hashes, and shared libraries.

### Smoke Flow (Built After Host Prerequisites)

The following EDA tools are fetched and built by `make bootstrap && make setup`
after the reviewer has installed any missing host packages reported by Doctor.
The artifact never invokes `sudo` automatically. Exact commit hashes are
recorded in `provenance/source-commits.json`.

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

Doctor prints an OS-specific command containing only the missing command-line
packages it can detect. It never executes package installation. After
`make bootstrap`, the pinned ORFS dependency installer is the authoritative
source for the complete C++ dependency set.

Typical Ubuntu 22.04 build prerequisites are:

```bash
sudo apt-get install build-essential cmake tcl-dev tk-dev \
  libboost-all-dev libeigen3-dev libspdlog-dev
```

Do not paste installation commands blindly on a managed server. Review them or
ask the system administrator, then repeat `make doctor`.
