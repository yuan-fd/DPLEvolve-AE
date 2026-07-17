# Project Map

Machine-readable file map and dependency graph for the DPLEvolve AE repository
and its sibling projects.

---

## Repository Topology

```
projects/                            ← Working root
├── DPLEvolve-AE/                    ← THIS REPO — artifact evaluation
│   ├── scripts/                     ← All executable entry points
│   │   ├── human/                   ← Reviewer-facing (thin wrappers)
│   │   ├── agent/                   ← Agent-facing (thin wrappers)
│   │   ├── internal/                ← Shared implementation
│   │   └── lib/                     ← Utility functions
│   ├── configs/                     ← Experiment configuration (YAML)
│   ├── docs/                        ← Human documentation
│   ├── agent/                       ← Agent documentation & constraints
│   ├── provenance/                  ← Version locks & checksums
│   ├── results/                     ← Reference + reproduced results
│   └── env/                         ← Python reqs, modules, version locks
│
├── dpl_evolve_agent/                ← CORE FRAMEWORK — READ-ONLY for AE
│   ├── baseline/                    ← Baseline runner (used by smoke/main)
│   ├── scripts/
│   │   ├── ae/                      ← AE scripts (check env, setup, smoke)
│   │   ├── workspace/               ← Build scripts (OpenROAD core)
│   │   └── codex_exec/              ← LLM execution runner
│   ├── configs/                     ← DSE experiment configs
│   ├── patches/                     ← OpenROAD source patches
│   ├── problems/                    ← Benchmark case definitions
│   ├── experiments/                 ← Experiment launchers
│   ├── metadata/                    ← Reproduction lock file
│   └── agent/                       ← Teacher/Student agent definitions
│
├── OpenROAD-flow-scripts/           ← ORFS WORKSPACE — BUILD TARGET
│   ├── flow/                        ← ORFS flow (Makefile-based)
│   │   ├── designs/                 ← Platform + design configs
│   │   ├── results/                 ← Synthesis & placement outputs
│   │   └── reports/                 ← Metric reports
│   └── tools/
│       ├── OpenROAD/                ← OpenROAD source (patched by dpl_evolve)
│       └── yosys/                   ← Yosys submodule (pinned commit)
│
└── dpl_evolve_state/                ← BUILD & RUN ARTIFACTS — GENERATED
    ├── yosys/8449dd470/             ← Pinned Yosys build
    ├── openroad_core/d5ff63a/       ← Pinned OpenROAD build
    ├── seed_sources/                ← DPL-Evolve source variants
    ├── smoke/                       ← Smoke test checkpoints
    ├── ae/environment.sh            ← Machine-local env (generated)
    └── packets/                     ← Experiment packet outputs
```

---

## File Dependency Graph

```
provenance/source-commits.json ─────────────────────┐
provenance/original-artifact-checksums.txt ─────────┤
env/versions.lock ──────────────────────────────────┤
                                                     ▼
scripts/lib/env_vars.sh ──→ scripts/internal/runtime_env.sh
                                     │
                                     ▼
scripts/human/setup.sh ──→ scripts/internal/ (build logic)
scripts/human/smoke_test.sh ──→ dpl_evolve_agent/baseline/run_baseline.sh
                              ──→ dpl_evolve_agent/scripts/ae/validate_aes_smoke.py
scripts/human/reproduce_baseline.sh ──→ dpl_evolve_agent/baseline/run_baseline_suite.sh
scripts/human/reproduce_main.sh ──→ dpl_evolve_agent/experiments/launchers/
                                 ──→ dpl_evolve_agent/scripts/codex_exec/
```

---

## Key Files (by function)

### Environment & Setup
| File | Role |
|---|---|
| `scripts/internal/runtime_env.sh` | Bootstrap environment variables |
| `scripts/lib/env_vars.sh` | Resolve all path variables |
| `scripts/lib/utils.sh` | Shared bash utility functions |
| `scripts/human/setup.sh` | Automated environment build |
| `scripts/human/check_environment.sh` | Read-only environment validation |

### Experiments
| File | Role |
|---|---|
| `configs/smoke/aes_nangate45.yaml` | Smoke test config |
| `configs/paper/baseline_9case.yaml` | Baseline suite config |
| `configs/paper/evolve_search.yaml` | ReviewDSE Level 2 config |
| `dpl_evolve_agent/baseline/run_baseline.sh` | Single baseline line |
| `dpl_evolve_agent/baseline/run_baseline_suite.sh` | Three-line baseline suite |
| `dpl_evolve_agent/scripts/codex_exec/runner.py` | LLM DSE runner |

### Validation
| File | Role |
|---|---|
| `dpl_evolve_agent/scripts/ae/validate_aes_smoke.py` | AES smoke validator |
| `dpl_evolve_agent/scripts/ae/check_environment.py` | Python env checker |
| `dpl_evolve_agent/baseline/collect_metrics.py` | Metric extraction |

### Reference Data
| File | Role |
|---|---|
| `provenance/source-commits.json` | Pinned source revisions |
| `provenance/original-artifact-checksums.txt` | Binary and ODB checksums |
| `dpl_evolve_agent/metadata/ae_reproduction_lock.json` | Complete lock file |
| `results/reference/` | Paper's published results |

---

## Immutable Zones

These directories MUST NOT be modified by agent scripts:
- `provenance/source-commits.json`
- `provenance/original-artifact-checksums.txt`
- `results/reference/`
- `dpl_evolve_agent/` (except via explicit patch-apply commands)
- `OpenROAD-flow-scripts/tools/OpenROAD/` (except via workspace prepare)
- `OpenROAD-flow-scripts/tools/yosys/` (except via setup)

These directories are GENERATED and must not be committed:
- `results/reproduced/`
- `results/tables/`
- `dpl_evolve_state/`
- `provenance/current-machine.json`
