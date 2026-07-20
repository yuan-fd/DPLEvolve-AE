# Evidence Policy

This repo separates experiment facts from algorithm hypotheses. Agents should
not treat every note as a rule.

## Evidence Levels

- `contract`: hard project boundary. Examples: keep
  `detailed_placement_evolve` as the evolved entrypoint, do not edit
  evaluator/scoring, and keep runtime artifacts out of git.
- `measured_observation`: result seen in a specific run or local matrix. It
  should include the case, binary, command, date, and metrics path when used
  for decisions.
- `working_hypothesis`: plausible explanation or design direction. It may
  guide a patch, but must be tested before promotion.
- `reference_donor`: code or paper mechanism worth studying. It is not a
  direct apply target unless a patch inventory explicitly marks it as such.
- `deprecated_context`: archived evidence kept only for forensics. It should
  not appear in default prompts.

## Current Calibration

- OpenROAD DPL flow is the current strict-track comparison anchor in this repo:
  `detailed_placement`, `improve_placement`, `optimize_mirroring`.
- The active-case best-so-far artifact is also a comparison anchor.  A new
  child result must beat that artifact on the final strict evaluator result
  before it becomes the next parent.
- OpenROAD diamond remains a local-search donor and prepass reference, not a
  standalone canonical baseline runner in the current harness.
- OpenROAD negotiation/NBLG is a repair and mechanism donor. Prior local runs
  showed useful speed work but also exposed that negotiation alone is not the
  root answer for every case.
- The `default_negotiation` source parent is negotiation-primary by contract:
  Teacher should require route logs or counters proving negotiation executed as
  the primary legalizer, not merely as a fallback after another legalizer.
- Differential/LEGALM-style guidance is allowed as a seed/frontier producer in
  non-LEGALM routes only when the downstream primary route consumes it.  The
  guidance objective, stop point, and consumer are working hypotheses until
  full-flow metrics prove them.
- LEGALM and DREAMPlace/Abacus are idea and source donors. Their stages,
  objectives, and data structures should be adapted, not copied blindly.
- The constrained `EvolveLegalizer` stage flow is a scaffold. Stage order,
  objective details, and concrete algorithms remain open to evidence-driven
  replacement.
- Paper-faithful or donor-faithful code is not automatically a promoted
  algorithm.  It is a reference, ablation, or donor until final strict metrics
  prove that it improves the active case.
- Legalizer-stage HPWL, local HPWL proxies, row-assignment seed counts, and
  stage-local move counts are diagnostics.  They must not override final strict
  legality and metrics.
- The canonical HPWL value for strict comparisons is `metrics.json:hpwl`,
  parsed from OpenROAD/DPL pin-based log reports such as `[INFO DPL-0022]
  HPWL after`.  `hpwl_proxy` is a legacy cell-bbox diagnostic kept only for
  debugging and must not drive promotion, Teacher planning, or published
  result tables.
- External handoff experiments, such as importing an outside DEF and running
  only `optimize_mirroring`, are valid diagnostics but are not the same class
  as a complete canonical run: `openroad_dpl_flow`,
  `openroad_dpl_negotiation`, or `evolve_default`.

## Parent Selection Policy

Every case-evolution round should distinguish these roles:

- `baseline`: independent OpenROAD DPL flow anchor for the case.
- `elite_parent`: best known evolved artifact for the case.
- `candidate_parent`: a new result that beats both baseline and elite parent on
  the final strict gate, or is explicitly selected for a protected secondary
  objective.
- `donor`: a mechanism worth borrowing, even if the full candidate loses.
- `negative_evidence`: a result that explains a regression and should not be
  continued unchanged.

The orchestrator and Teacher prompts should preserve the elite parent across
rounds.  If all children regress, the next round restarts from the elite parent
and uses the regressions only as analysis material.

## Prompt Hygiene

When adding knowledge, state one of:

- `observed`: what happened, where, and with which artifacts.
- `hypothesis`: why it may have happened.
- `next_test`: the smallest experiment that would confirm or reject it.

Avoid writing unverified hypotheses as permanent instructions.

Avoid writing a stage-local win as a project rule.  Record it as a diagnostic
unless the complete strict flow also improves.
