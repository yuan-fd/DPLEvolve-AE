# Aggressive Basin Schedule After A Valid Light Donor

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


Evidence type: measured mechanism refinement.  This card captures how to
strengthen a validated post-DPO basin donor without losing control.

## Mechanism Summary

Once a light basin donor is proven live, a stronger version can spend more of
the runtime budget through:

- more passes,
- larger top-K candidate windows,
- wider local row radius,
- more local targets per candidate,
- deterministic pass-by-pass schedule growth,
- tie-breaks that favor cells with larger displacement or stronger local stress.

This is a continuation strategy, not a first-round search default.

## When To Use

Use an aggressive basin schedule only after all of the following are true:

- a light basin donor already improves final HPWL,
- legality remains stable,
- pass counters prove the basin consumer is active,
- runtime is still clearly under the hard budget,
- final HPWL is still limited by residual post-DPO local basins.

## Practical Schedule Pattern

- Keep the same exact affected-net objective and local journal.
- Start from the validated light basin configuration.
- Increase candidate count and target budget gradually by pass.
- Grow row radius only every few passes rather than immediately.
- Stop when marginal gain rate falls or accepted count collapses.

This schedule is usually more robust than jumping to one huge window at pass 1.

## Why It Helps

Large designs or designs with strong residual DPO basins may need more than one
short cleanup pass.  A gradual schedule lets the search allocate more runtime only
when earlier smaller passes were already productive.

## Risks

- Runtime can grow with little HPWL benefit if candidate ranking is weak.
- Broadening windows without exact scoring or gain-rate stops can turn into a
  slow blind search.
- If the localizer/frontier source is weak, a deeper basin alone may saturate.

## Teacher Guidance

- Route one continuation student here when the light donor is live but under
  budget and still HPWL-limited.
- Compare it directly against the light donor, not against a weak unrelated
  route.
- If the deeper schedule wins, keep it as the stronger donor and freeze the
  lighter one as the safety baseline.

## Student Guidance

- Keep per-pass logs for top-K, radius, target cap, accepted count, gain, and
  pass runtime.
- Prefer deterministic schedule growth over ad hoc threshold changes.
- If gain falls sharply after early passes, stop and preserve the earlier
  lighter donor.
