# DPO Cards: Rule-Aware And Incremental Refinement

Scope: bounded detailed-placement refinement for rule-aware, ECO, and
macro/prototype-driven scenarios.  These are detailed-placement refinements, not
global placement from scratch.

## D10. DFM / Spacing / Implant-Aware Detailed Placement Refinement

Source status: `source-handle-only`.

Source handles: `dfm_spacing_implant_dp`, `pin_access_refinement_2021`.

Best use: candidate filters and tie-breaks when technology or spacing rules
limit legal local moves.

Core thesis: a local HPWL-improving move can be bad if it creates spacing,
implant, or pin-access risk.  Rule-aware DPO filters such moves before commit.

Pseudo code:

```text
build cheap rule-risk features per row segment and cell neighborhood
for candidate move/swap/reorder:
    if rule filter rejects:
        skip candidate with reason code
    delta = exact HPWL after legal dry run
    score = delta + rule-risk penalty
    commit if legal and score improves
```

Implementation handles:

- reason-coded rule rejects;
- cheap filters before exact HPWL when possible;
- optional rule-risk tie-break only when HPWL deltas are close;
- logs for spacing/access/implant rejects and accepted rule-safe moves.

Failure pattern: rule-aware DPO can suppress HPWL improvement if the penalty is
too strong.  Keep the default HPWL target primary unless the design feature
requires stronger rule protection.

## D11. ECO / Incremental Placement Refinement

Source status: `source-handle-only`.

Source handles: `eco_incremental_dp`.

Best use: changed-region repair, handoff neighborhoods, or small invalid
components after legalization.

Core thesis: when only a bounded region is bad, repair the touched region and
its boundary rather than rerunning a full-design pass.

Pseudo code:

```text
changed = cells, nets, rows, or regions marked by legalization/DPO logs
expand changed by one or two local neighborhoods
freeze external placement
run bounded legal move/swap/reorder inside changed region
repair boundary conflicts
accept only if full legality and objective improve
```

Implementation handles:

- compact changed-region representation;
- boundary repair after local optimization;
- exact final check against full placement;
- logs for changed-region size, boundary repairs, accepted local moves, and
  skipped oversized regions.

Failure pattern: too-small regions miss the true HPWL source; too-large regions
become slow full-design reruns.  Region expansion must be tied to counters and
gain rate.

## D12. Macro / Placement-Prototype Refinement

Source status: `source-handle-only`.

Source handles: `macro_prototype_refinement`.

Best use: post-legalization local refinement around macro edges or stable
prototype placements.  Not a global placement algorithm.

Core thesis: macro-edge and prototype-induced stress should guide local
refinement windows, but detailed placement must still operate on legal standard
cell rows and intervals.

Pseudo code:

```text
identify macro-edge or prototype-stress windows
freeze stable external cells
for each window:
    generate legal local moves, swaps, and row reorders
    score exact HPWL and displacement
    repair boundary cells
    commit only if legal and objective improves
```

Implementation handles:

- macro-edge window extraction;
- stable prototype freeze rules;
- local row/interval legality;
- logs for selected windows, accepted moves, boundary repairs, and rejected
  oversized windows.

Failure pattern: macro/prototype refinement becomes global placement if it
moves unconstrained large regions.  Keep it as a bounded DPO window mechanism.
