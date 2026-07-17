# Environment Requirements

## Hardware

| Resource | Minimum | Recommended |
|---|---|---|
| CPU cores | 4 | 8+ |
| RAM | 16 GB | 32 GB |
| Disk | 10 GB | 20 GB |
| GPU | Not required | N/A |
| Network | Initial setup only | Persistent (for LLM API) |

## Operating System

Tested on:
- **RHEL 8 / Rocky 8** (primary development platform)
- **Ubuntu 20.04 / 22.04** (expected to work)

The code should work on any Linux x86-64 distribution with the required
toolchain versions.

## Software Dependencies

### Build Tools

| Tool | Minimum Version | Check Command |
|---|---|---|
| GCC | 9.0 | `gcc --version` |
| G++ | 9.0 | `g++ --version` |
| CMake | 3.20 | `cmake --version` |
| GNU Make | 4.0 | `make --version` |
| Bison | 3.6 | `bison --version` |
| Flex | 2.6 | `flex --version` |
| Git | 2.0 | `git --version` |
| Python | 3.11 | `python3 --version` |

### Python Packages

| Package | Version | Purpose |
|---|---|---|
| PyYAML | 6.0.3 | Configuration parsing |

Install with:
```bash
pip install -r env/requirements.txt
```

Or let `make setup` handle it automatically.

### EDA Tools (Built from Source)

| Tool | Version | Commit | Build Time |
|---|---|---|---|
| Yosys | 0.64 | `8449dd470` | ~5 min |
| OpenROAD | custom | `d5ff63a` | ~20 min |

**Important**: Yosys **must** be the exact pinned version. A different Yosys
version changes the synthesized netlist and invalidates the baseline comparison.
The setup script builds the correct version automatically.

### System Modules (Server-Specific)

On HPC-style servers with Environment Modules:
```bash
module load gcc/default    # GCC ≥ 9
module load openroad       # Provides Bison, Flex, shared libs
```

The setup script (`make setup`) attempts to load these automatically.

## Filesystem Layout

The artifact expects the following sibling directories:

```
projects/
├── DPLEvolve-AE/           # This repository
├── dpl_evolve_agent/       # Core framework (scripts, patches, prompts)
├── OpenROAD-flow-scripts/  # ORFS workspace (flow + tools)
├── dpl_evolve_state/       # Build artifacts and run outputs
└── .venvs/dplevolve/       # Python virtual environment
```

If your layout differs, set these environment variables before running:
```bash
export DPL_EVOLVE_AGENT_ROOT=/path/to/dpl_evolve_agent
export ORFS_ROOT=/path/to/OpenROAD-flow-scripts
export DPL_EVOLVE_STATE_ROOT=/path/to/state
export DPL_EVOLVE_PYTHON=/path/to/python
```

Or create `env.local.sh`:
```bash
# env.local.sh — machine-local overrides
export DPL_EVOLVE_AGENT_ROOT=/custom/path
```

## Network Requirements

| Phase | Network Needed | Purpose |
|---|---|---|
| Initial setup | Yes | Git submodule fetch, pip install |
| Smoke test | No | Fully offline |
| Baseline reproduction | No | Fully offline |
| LLM DSE | Yes | API calls to LLM endpoint |

## Verifying Your Environment

Run the checker:
```bash
make check
```

A successful output shows green `[OK]` markers for all components.

Commit verification and binary hash checking are informational by default.
Use `--strict-hashes` for bit-for-bit comparison with the reference machine
(expect differences due to compiler/linker variations).
