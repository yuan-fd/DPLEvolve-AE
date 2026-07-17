# Artifact Overview

## Paper Information

- **Title**: From Tool Invocation to Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA
- **Venue**: MLCAD 2026 (Short Paper)
- **Paper ID**: 150
- **Presentation**: Short Oral + Poster

## What ReviewDSE Does

ReviewDSE is an LLM-agent-driven framework that performs **white-box Design
Space Exploration** on OpenROAD's detailed placement. Unlike traditional DSE
that treats EDA tools as black boxes (tuning only parameters and macro flags),
ReviewDSE:

1. **Reads OpenROAD's internal C++ source code** for the detailed placement
   pipeline (legalizer, DPO, handoff, etc.).
2. **Proposes source-code-level mechanism changes** (algorithm variants,
   heuristic modifications).
3. **Compiles candidate OpenROAD binaries** with the proposed changes.
4. **Evaluates each candidate** on real benchmark designs.
5. **Uses a Teacher-Student multi-agent workflow** to screen results and guide
   further exploration.

## Two-Level Architecture

### Level 1: Calibration & Warm-up
- Creates a reusable pre-built database of DPL mechanism variants.
- Pre-compiles source-code branch candidates.
- Reduces Level 2 cold-start overhead.

### Level 2: Teacher-Student Multi-Agent DSE
- **Multiple Student Agents**: Each explores different source-code branches,
  proposes modifications, compiles, and evaluates.
- **Single Teacher Agent**: Reviews results, filters effective mechanisms,
  eliminates dead ends, and guides Students to promising regions.

## Key Results (Paper Claims)

1. **HPWL improvement**: 1.78% average across benchmarks, vs 0.38% for
   traditional black-box Bayesian optimization.
2. **Constraint repair**: Fixes 9 complex layout constraint scenarios where
   traditional methods time out or fail.
3. **Source-mechanism discovery**: Identifies concrete, reusable OpenROAD
   source-code improvements rather than one-off parameter settings.

## Artifact Scope

This artifact evaluation repository validates:

| Scope | Included? | Notes |
|---|---|---|
| Environment setup | ✅ | Pinned tool versions, automated setup |
| Input reproducibility | ✅ | Checksum-verified synthesis |
| Baseline results | ✅ | OpenROAD default/negotiation/evolve |
| Black-box BO baseline | ✅ | Configs provided; run with API |
| Level 2 DSE results | ✅ | Configs provided; **requires LLM API** |
| Ablation studies | ⬜ | Configs provided for post-deadline work |
| Joint optimization | ⬜ | Configs provided for post-deadline work |
| Multi-seed statistics | ⬜ | Configs provided for post-deadline work |

## How to Navigate This Repository

- **Want to run something quickly?** → `README.md` Quick Start
- **Want to understand the experiments?** → `docs/experiments.md`
- **Want to map paper claims to commands?** → `docs/claims-to-artifacts.md`
- **Hit an error?** → `docs/troubleshooting.md`
- **Want to write Agent automation?** → `agent/AGENTS.md`
