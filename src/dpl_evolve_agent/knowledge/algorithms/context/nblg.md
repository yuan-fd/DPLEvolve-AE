# NBLG Context Card

## Core thesis
NBLG reformulates mixed-height legalization as a **resource allocation task over placement-site grids**. Each cell negotiates for grid resources using:
- target cost
- history cost
- congestion penalty

## Five components
1. object = cell
2. cost = displacement + congestion/history
3. action = enumerate candidate legal sites
4. order = overflowed cells first, guided by cell geometry
5. region = surrounding window, not fixed global box

## Why it matters for OpenROAD
OpenROAD public docs explicitly say the current NegotiationLegalizer is based on NBLG.
So this paper is not an external idea dump; it is a close mechanism donor for
the negotiation engine.  It does not prove that negotiation is the best primary
strategy for every strict-track case.

## Mechanisms worth testing first
- adaptive penalty schedule `pf`
- history update strength
- max-displacement penalty weighting
- search window geometry
- post-negotiation greedy / swap refinement
- legal-cell skipping / isolation point logic

## What to avoid
- full paper reimplementation before verifying current repo deltas
- broad multithreading rewrite before strict-track metrics are stable
- promoting a negotiation-only result without comparing against the current
  OpenROAD DPL flow baseline and best evolved artifact
