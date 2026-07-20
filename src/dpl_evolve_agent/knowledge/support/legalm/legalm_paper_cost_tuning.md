# LEGALM Paper-Cost Tuning

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


This card summarizes the useful conclusions from the long LEGALM tuning log.
Raw run tags and case-specific artifact names are intentionally omitted from
default insight context; use archived experiment artifacts only when exact
reproduction is required.

## Core Lesson

Paper-faithful LEGALM structure can close legality, but quality depends on the
cost model inside assignment and no-overflow refinement.  The useful tuning
axis is not scalar weight retuning; it is where and how the algorithm estimates
wirelength, displacement, capacity pressure, and candidate locality.

## What Helped

- Bounded row and interval probes made large cases tractable.
- Local occupancy updates made ALM guidance more stable.
- Topological or touched-net delta scoring was more informative than pure
  displacement scoring.
- Candidate stencils that include desired, original, midpoint, and neighbor
  positions were better than one-dimensional nearest-site choices.
- Stage telemetry by assignment, overflow, no-overflow polish, candidate count,
  and legality veto was necessary to explain quality failures.

## What Did Not Solve Quality

- More ALM iterations alone did not guarantee better final HPWL.
- Larger stencils without better commit criteria increased runtime and could
  worsen displacement tail.
- Same-row no-overflow polish was too local to recover assignment mistakes.
- Treating legalizer-stage proxy improvements as final wins was unreliable;
  complete flow metrics could erase them.

## Current Algorithmic Gap

The remaining gap is assignment quality under strict capacity:

- row choice needs net-aware and size-aware scoring,
- segment selection needs cheap touched-net HPWL deltas,
- no-overflow refinement needs local swap/shift moves that preserve capacity,
- partition/local-optimum escape is needed when rows become congested or
  displacement stagnates.

## Guidance For Future Agents

When changing LEGALM:

- preserve bounded candidate generation,
- avoid full-row or full-net scans in hot loops,
- use cached net boxes and dirty-row structures,
- record candidate caps and touched scopes in telemetry,
- evaluate complete flow HPWL before promoting the change,
- compare against the best evolved donor, not only the basic legalizer path.

When a LEGALM variant is slow but improves quality, the next experiment should
make the same mechanism cheaper through caching, pruning, or parallelism rather
than discarding the mechanism.
