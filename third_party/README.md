# Third-Party Dependencies

This directory documents external dependencies required by DPLEvolve.

## Dependency List

| Component | Version | Source | Role |
|---|---|---|---|
| OpenROAD | d5ff63a | GitHub | Detailed placement engine |
| OpenROAD-flow-scripts | 9e2467a | GitHub | Synthesis and physical design flow |
| Yosys | 0.64 (8449dd470) | GitHub | RTL synthesis |
| OpenSTA | 8e42af1 | GitHub | Static timing analysis |
| ABC | 17cadca | GitHub | Logic synthesis backend |

## Build Instructions

All third-party tools are built via `make setup`, which:
1. Clones/verifies exact Git commits
2. Builds Yosys from source
3. Builds the DPLEvolve-customized OpenROAD core
4. Installs everything under the project state directory

No system-wide installation or root privileges are required.

## Version Locks

Exact commit hashes are recorded in:
- `../provenance/source-commits.json` — human-readable
- `../env/versions.lock` — machine-readable

## License Compliance

All third-party tools are open-source:
- OpenROAD: BSD 3-Clause
- Yosys: ISC License
- OpenSTA: GPLv3
- ABC: MIT/BSD-style
