# DPLEvolve — Artifact Evaluation

[![MLCAD 2026](https://img.shields.io/badge/MLCAD-2026-blue)](https://mlcad.org/)
[![License](https://img.shields.io/badge/License-BSD%203--Clause-blue.svg)](LICENSE)
[![Artifact Evaluation](https://img.shields.io/badge/AE-Available-brightgreen)](docs/artifact-overview.md)

Artifact Evaluation repository for the MLCAD 2026 short paper:

> **From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA**

This repository provides scripts, configurations, and documentation to
reproduce the key experimental results in the paper. It validates the
ReviewDSE framework for LLM-agent-driven, source-code-level design space
exploration of OpenROAD's detailed placement.

---

## Quick Start (For Reviewers)

```bash
# 1. Check your environment
make check

# 2. Build dependencies (Yosys + OpenROAD, ~30 min)
make setup

# 3. Validate everything works (AES smoke test, ~5 min, no API calls)
make smoke
```

**That's it.** If `make smoke` passes with `[OK]`, your environment is correctly
set up and can reproduce the paper's baseline results.

---

## What This Artifact Validates

| Claim | Validation Method | API Required | Time |
|---|---|---|---|
| Environment is correctly configured | `make check` | No | < 1 min |
| Input synthesis is reproducible | `make smoke` (checksum) | No | ~5 min |
| OpenROAD default baseline matches paper | `make smoke` (HPWL check) | No | ~5 min |
| 9-case baseline results | `make reproduce-baseline` | No | ~30 min |
| Main ReviewDSE results | `make reproduce-main` | Yes (LLM) | Hours–days |
| Ablation studies | `make reproduce-ablation` | Yes (LLM) | Hours |

For a detailed claim-to-artifact mapping, see [docs/claims-to-artifacts.md](docs/claims-to-artifacts.md).

---

## Directory Layout

```
DPLEvolve-AE/
├── README.md              ← You are here
├── Makefile               ← All entry points
├── docs/                  ← Human-readable documentation
├── agent/                 ← Agent/machine-facing documentation & constraints
├── scripts/
│   ├── human/             ← Reviewer-facing scripts (thin wrappers)
│   ├── agent/             ← Agent-facing scripts (thin wrappers)
│   └── internal/          ← Shared implementation (both call this)
├── configs/               ← Experiment configuration (YAML)
├── provenance/            ← Version locks and checksums
├── results/               ← Reference results + reproduced output
├── benchmarks/            ← Benchmark design manifests
├── env/                   ← Python requirements, module setup, version locks
└── third_party/           ← External dependency documentation
```

**Key design principle**: `scripts/human/` and `scripts/agent/` are thin
wrappers. The actual implementation lives in `scripts/internal/`. One source
of truth, two interfaces.

---

## Resource Requirements

### Minimal Validation (no API)
- **OS**: Linux x86-64 (tested on RHEL 8 / Rocky 8)
- **CPU**: 4+ cores recommended
- **RAM**: 16 GB
- **Disk**: ~10 GB (source trees + binaries)
- **Time**: ~30 min for setup, ~5 min for smoke test
- **Network**: Required for initial Git submodule fetch only
- **No GPU required**
- **No LLM API key required**

### Full Reproduction (with LLM)
Additional requirements:
- **Claude API access** (or compatible LLM endpoint)
- **Token budget**: ~2.15B tokens per design (refer to Section 5 of the paper)
- **Time**: Hours to days depending on search breadth and concurrency
- **Cost**: Significant — see paper for detailed cost analysis

---

## Available Make Targets

| Command | Description | API | Time |
|---|---|---|---|
| `make check` | Validate environment | No | < 1 min |
| `make setup` | Build all dependencies | No | ~30 min |
| `make smoke` | AES smoke test (verify pipeline) | No | ~5 min |
| `make reproduce-baseline` | All 9-case baselines | No | ~30 min |
| `make reproduce-main` | Main paper results | Yes | Hours |
| `make table-1` | Generate Table 1 (baseline comparison) | No | < 1 min |
| `make table-2` | Generate Table 2 (main HPWL results) | Yes | < 1 min |
| `make table-3` | Generate Table 3 (constraint repair) | Yes | < 1 min |
| `make provenance` | Record machine provenance | No | < 1 min |
| `make clean` | Remove reproduced results | No | < 1 min |
| `make distclean` | Remove all build artifacts | No | < 1 min |

---

## Interpreting Results

### Smoke Test Success

```
[OK] AES smoke test PASSED
```

All validation checks pass:
- Input ODB checksum matches reference
- Instance count: 14,676
- Instance area: 18,648.2 µm²
- Global HPWL: 188,569.2 µm
- Final HPWL: 176,845.1 µm
- No placement violations

### Smoke Test Failure

If the smoke test fails, check:
1. **Wrong Yosys version**: The most common failure. Run `make setup` to build
   the pinned Yosys 0.64 (commit `8449dd470`). The system Yosys may produce
   a different netlist.
2. **Missing modules**: Some servers require `module load gcc/default openroad`.
   The setup script attempts this automatically.
3. **Build errors**: See [docs/troubleshooting.md](docs/troubleshooting.md).

---

## Key Design Decisions

### Why pin the exact Yosys commit?
The Phase 2 audit revealed that using a different Yosys version (0.63 vs 0.64)
changed the synthesized netlist by 7.4% in instance count, causing an 8.88%
HPWL deviation. Synthesis must use the exact Yosys commit recorded in
`provenance/source-commits.json`.

### Why separate human/agent entry points?
Human reviewers want simple, well-documented commands. Agents need machine-
readable constraints, invariants, and schemas. Both call the same
`scripts/internal/` implementation, so there is one source of truth.

### Why timestamped run directories?
Every experiment run creates a new timestamped directory. Existing results
are never overwritten. This preserves provenance and allows side-by-side
comparison of multiple runs.

---

## Provenance

All source revisions are pinned in [`provenance/source-commits.json`](provenance/source-commits.json).

Key commits:
- DPLEvolve agent: `96d8c613`
- ORFS: `9e2467a6`
- OpenROAD: `d5ff63ab`
- Yosys: `8449dd470` (0.64)

Reference binary checksums are recorded in
[`provenance/original-artifact-checksums.txt`](provenance/original-artifact-checksums.txt).

Generate a machine-specific provenance record:
```bash
make provenance
```

---

## Contact

- **Paper authors**: See paper for contact information
- **Artifact issues**: Open an issue on the [GitHub repository](https://github.com/CODA-Team/DPLEvolve)
- **Build/run questions**: See [docs/troubleshooting.md](docs/troubleshooting.md)

---

## License

BSD 3-Clause License. See [LICENSE](LICENSE).

## Citation

See [CITATION.cff](CITATION.cff) or cite as:

```bibtex
@inproceedings{dplevolve2026,
  title     = {From Tool Invocation to Source-Mechanism Exploration:
               Protected White-Box DSE for Open-Source EDA},
  author    = {See paper for author list},
  booktitle = {Proceedings of the 2026 ACM/IEEE Workshop on Machine
               Learning for CAD (MLCAD '26)},
  year      = {2026},
}
```
