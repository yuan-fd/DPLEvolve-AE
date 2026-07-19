# cutRow Adversarial Legalization Insight

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


Use this card when a Teacher/Student round targets cutRow-generated difficult
legalization inputs instead of ordinary ORFS `3_4_place_resized.odb` snapshots.
Default Teacher context should reason from input features: removed-row pattern,
available whitespace, fragmented legal rows, conflict pressure, and whether
default legalizers fail or time out.

## Feature-Level Evidence

Promoted strict evolved repair/acceleration rows share these features:

- center-band row removal or separator-like whitespace disruption,
- at least one default OpenROAD line fails, times out, or produces a much slower
  legal result,
- evolved negotiation/resource-allocation legalizers can close the fragmented
  row-capacity problem faster or more reliably,
- `check_placement` must pass; HPWL alone is not success evidence.

Default-all-fail non-hband rows are real hard-case candidates, but only rows
with a strict evolved replay should be claimed as repaired.  Keep unresolved
rows as future replay targets, not as successful evidence.

Excluded patterns:

- Do not use hband-like patterns as legalizer evidence when the failure is
  dominated by fixed physical-cell site-alignment artifacts such as tap,
  endcap, or edge cells.
- Treat those rows as cutRow construction issues until the generation flow also
  preserves the physical-cell context.

## Teacher advice

For cutRow rounds, start from `default_negotiation` for center-band separator
quality/runtime, but explicitly distinguish three buckets:

1. promoted strict evolved repair/acceleration rows in the archived tables,
2. unresolved non-hband hard rows that require evolved replay,
3. excluded hband-like construction artifacts.

Do not let a Student optimize or report against hband-like rows unless the task
is specifically to repair the cutRow generation method.
