# Adapters

This directory contains thin compatibility/control-plane adapters.

Adapters may wrap evaluation, locking, archiving, or external workflow ideas,
but they must not become the primary algorithm source.  Detailed-placement
algorithm changes belong in private `dpl_evolve` source workspaces created from
OpenROAD, and the protected baseline/evaluator path remains the judge.

Current contents:

- `science_codeevolve/`: small local helpers inspired by CodeEvolve-style
  locking, guarded execution, archive discipline, and knowledge lookup.

Default Teacher/Student runs do not need to inspect this directory unless a
packet or launcher explicitly names one of these adapter helpers.
