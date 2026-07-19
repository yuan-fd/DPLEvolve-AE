# LEGALM 2.0 Context Card

## Core thesis
LEGALM 2.0 treats legalization as a staged optimization problem:

1. scanline-based initial legalization
2. ALM-based overflow elimination
3. zero-overflow refinement

It also adds:
- connectivity-based local optimum escape
- routability-aware penalties
- strong parallelization ideas

## Why it matters for OpenROAD
LEGALM is **not** the closest code donor for the current OpenROAD legalizer,
but it is an excellent **idea donor** for:
- initialization
- escape from stuck local regions
- explicit refinement after legality is reached

## Current calibration

Treat a LEGALM-style implementation as a donor or ablation until strict final
metrics prove otherwise.  A self-legal LEGALM path can be useful and
inspectable, but legality and paper fidelity do not by themselves mean it is
the best parent for a case.

When the task is specifically to implement or repair LEGALM, preserve the paper
mainline: relaxed/static legalization, ALM/BGD overflow reduction, local
optimum escape, and zero-overflow refinement.  Flow-level repair or negotiation
may be an outer integration stage, but should not be hidden inside the LEGALM
paper stage.

Promote a LEGALM-derived candidate only when the complete flow beats the active
case baseline and best-so-far evolved artifact on canonical pin-based HPWL.

## Mechanisms worth borrowing first
- better initialization
- overflow-stagnation detection
- connected-component / plate aware escape
- no-overflow refinement stage

## Implementation notes

- Map code to paper equations or pseudocode when changing the LEGALM stage.
- Keep paper-sourced parameters explicit; if a parameter is a CPU-reference
  default rather than a paper constant, label it that way.
- CPU implementation should use OpenMP and compact row/site/net data
  structures; do not replace the paper flow with a simpler heuristic only to
  reduce runtime.
