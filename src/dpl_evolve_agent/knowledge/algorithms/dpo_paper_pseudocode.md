# DPO Paper Pseudocode Index

This file is a compatibility entry point for existing Teacher/Student prompts
and `knowledge/index/skill_cards.jsonl`.  The detailed cards are split under
`dpo_refinement/` so agents can inspect the relevant improve-placement
mechanism without reading a long mixed note.

Use targeted queries such as
`rg -n "D9|handoff|recoverability" knowledge/algorithms/dpo_refinement`.

## Evidence Tiers

- `checked-source-text`: checked against available source text or committed
  reference.
- `source-handle-only`: source handle is known, but exact paper text was not
  rechecked for this card.  Verify before copying literal algorithm details.
- `derived-from-openroad/context`: maps current OpenROAD behavior or repo
  context rather than a single paper.
- `implementation-hypothesis`: synthesized project mechanism; validate with
  counters and strict full-flow metrics.

## Hard Rules

- DPO starts from a legal placement and must leave a legal placement.
- Primary objective remains final HPWL; runtime is a cost/value signal.
- Spend runtime on deeper cached, parallel, or better-targeted candidate
  evaluation. Randomized or repeated search must be scoped, instrumented, and
  tied to a concrete HPWL/recoverability hypothesis.
- DPO success must be measured as `HPWLlg -> HPWLimprove -> HPWLfinal`.
- If legalization creates a basin DPO cannot improve, fix handoff or local
  recoverability instead of only tuning constants.

## Card Map

| id | mechanism | card |
|---|---|---|
| D0 | FastDP-style global swap/local reorder/segment clustering | `dpo_refinement/classic_hpwl_descent.md` |
| D1 | OpenROAD DPO command taxonomy | `dpo_refinement/classic_hpwl_descent.md` |
| D2 | GPU-DPO / LSMC large-step escape | `dpo_refinement/handoff_and_runtime.md`, `dpo_refinement/gpu_dpo_lsmc.md` |
| D3 | ABCDPlace / batch-concurrent detailed placement | `dpo_refinement/classic_hpwl_descent.md` |
| D4 | Density-aware detailed placement with instant legalization | `dpo_refinement/classic_hpwl_descent.md` |
| D5 | Jezz incremental legalization as DPO move oracle | `dpo_refinement/incremental_and_timing.md` |
| D6 | Hippocrates / first-do-no-harm constraints | `dpo_refinement/incremental_and_timing.md` |
| D7 | Timing-driven quadratic DPO with incremental legalization | `dpo_refinement/incremental_and_timing.md` |
| D8 | MrDP / multi-row detailed placement | `dpo_refinement/incremental_and_timing.md` |
| D9 | Handoff-aware DPO from legalizer signals | `dpo_refinement/handoff_and_runtime.md` |
| D10 | DFM / spacing / implant-aware detailed placement refinement | `dpo_refinement/refinement_extensions.md` |
| D11 | ECO / incremental placement refinement | `dpo_refinement/refinement_extensions.md` |
| D12 | Macro / placement-prototype refinement | `dpo_refinement/refinement_extensions.md` |

## Review Checklist

- Does the diff modify `improve_placement_evolve` source or a real handoff
  consumer when DPO is in scope?
- Does it preserve legality after every accepted move?
- Are exact HPWL deltas cached or bounded enough for the runtime budget?
- Do counters show candidate attempts, accepts, rejects, and exact-delta time?
- Does stage-wise evidence explain whether legalizer output is recoverable by
  DPO?
