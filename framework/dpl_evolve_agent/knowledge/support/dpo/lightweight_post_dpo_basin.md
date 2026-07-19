# Lightweight Post-DPO Basin Search

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


Evidence type: measured cross-case mechanism observation.  This card describes
a reusable improve-placement donor pattern, not a universal default.

## Mechanism Summary

Add one bounded post-DPO basin search after the stock improve-placement passes:

- start from a legal placement produced by a stable primary legalizer,
- rank candidate cells by compact local HPWL or displacement stress,
- probe a capped set of local legal targets,
- score each accepted move with exact affected-net HPWL,
- stop when accepted count or marginal gain falls below a threshold.

This mechanism is best treated as a light donor:

- it is cheap enough to test broadly,
- it often improves final HPWL without requiring a new legalizer,
- it is a clean regression guard when stronger DPO search is under development.

## Why It Works

Stock improve placement often leaves a small number of locally trapped cells
after the main swap/reorder/random passes.  A short exact local search can
recover part of that residue without changing the legalization route.

The mechanism is especially useful when:

- legality is already clean,
- stage-wise evidence says legalization is not the dominant bottleneck,
- runtime headroom exists but is not large enough for broad multi-stage search,
- a design still contains local HPWL basins after the normal DPO sequence.

## Typical Structure

1. Preserve the existing legalizer and stock DPO sequence as the control path.
2. Build a ranked candidate list from moved cells, local HPWL regressions, or
   displacement outliers.
3. For each selected candidate, enumerate a bounded local target set.
4. Use exact affected-net HPWL plus legality check before commit.
5. Aggregate compact pass counters and stop early on low marginal gain.

## Good Signs

- same-case final HPWL improves against the canonical reference,
- cross-case improvement exists only as extra confidence, not as a replacement
  for same-case final HPWL proof,
- runtime stays near the canonical flow or increases only slightly,
- accepted exact moves are nonzero,
- pass-level gain decays smoothly rather than collapsing immediately,
- large designs with active DPO residue benefit more strongly than tiny clean
  designs.

## Boundaries

- Do not promote this as proof that the legalizer is solved; it is a DPO donor.
- Do not keep deepening the same basin pass once accepted gain becomes tiny.
- If the mechanism gives only micro-gains on the current design, preserve it as
  an elite donor and redirect search to a larger mechanism source.
- If runtime rises without accepted exact moves, treat it as an implementation
  failure rather than an algorithm win.

## Teacher Guidance

- Use this donor when the legalizer is already stable and final HPWL is still
  worse than desired.
- Keep one route on this light donor as a regression guard.
- Do not allocate all students to this donor family once it is validated.

## Student Guidance

- Keep the search bounded: candidate cap, local target cap, pass cap, gain-rate
  early stop.
- Log ranked cells, selected cells, legal probes, accepted moves, rejected
  moves, exact gain, and elapsed time per pass.
- If the donor is already live and safe, prefer mechanism redesign over more
  threshold polishing.
