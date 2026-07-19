# Insight: Paper-Faithful Legalization vs Flow Integration

## Agent Use

- Role: LEGALM reference/support. Open only when the selected route uses LEGALM-style guidance or the legalizer is the diagnosed bottleneck.
- Treat paper alignment and LEGALM tuning as support for a full-flow mechanism, not as a route selector by itself.


## Claim

For the next `detailed_placement_evolve` mainline, the algorithm stage should
first respect the source paper's mechanism as closely as our OpenROAD data model
allows.  Whether the result is better or worse than diamond is a separate
evaluation question, not a reason to silently rewrite the stage into an
experience-tuned heuristic.

This matters because the project currently has two different goals that are
easy to mix up:

- **Algorithm implementation:** build a recognizable, inspectable version of a
  staged LEGALM-like detailed legalization method.
- **Flow integration:** decide how that method should hand off to diamond,
  negotiation, repair, or polish inside OpenROAD.

The first goal should be paper-faithful.  The second goal should be controlled
by telemetry, experiments, and outer policy.

## Calibration Against Case Evolution

Paper-faithful implementation is not the same thing as parent selection.
LEGALM-like stages should remain inspectable and faithful enough to study, but
the active case parent is chosen by the complete strict flow result.

A paper-faithful route that legalizes cleanly but loses final HPWL is still
valuable as a donor or ablation.  It should not replace a better evolved parent
unless it beats both the baseline and the best-so-far evolved artifact on final
strict metrics.

Conversely, a stage-local metric can be misleading in either direction:

- legalizer-stage HPWL may look worse while downstream polish creates a final
  strict win,
- local HPWL or displacement proxy improvements may still lose after the full
  flow,
- seed counts or assignment coverage are explanations, not promotion gates.

## Current Interpretation

The evolved command remains:

```tcl
detailed_placement_evolve
```

The top-level scaffold may keep stages, but each stage should report what it
actually implements:

1. `relaxed_guidance`
   - Paper-aligned role: relaxed/static-constraint projection or differentiable
     target guidance.
   - Movable overlaps are allowed here.
   - Output is a target placement or guidance field, not a final legalization.

2. `alm_bgd_overflow_reduction`
   - Paper-aligned role: multiplier update loop plus bounded BGD candidate
     stencil.
   - Telemetry must expose overflow/penalty convergence, candidate count,
     accepted moves, and stagnation.
   - Do not tune this stage only to mimic diamond HPWL.

3. `partition_escape`
   - Paper-aligned role: detect local optimum / overflow stagnation and move
     work to a larger or more promising component/partition.
   - This should be implemented as an explicit stage or substage, not hidden
     inside diamond fallback.

4. `stage3_no_overflow_refinement`
   - Paper-aligned role: improve placement while preserving no-overflow or
     strictly bounded overflow.
   - This is where bounded polish belongs if it is derived from the paper.

5. `final_handoff`
   - Engineering role: call OpenROAD diamond or negotiation only as a controlled
     bridge when the paper-faithful stages are not yet complete enough to emit
     a fully legal placement.
   - Telemetry must make this handoff visible so success is not misattributed
     to the earlier stages.

## Do Not Collapse These Decisions

Avoid this pattern:

```text
stage behaves badly on one wirelength-sensitive target
-> add hidden local guard
-> call it LEGALM
```

Prefer this pattern:

```text
paper-faithful stage behaves badly on a design class
-> record stage telemetry and final metrics
-> add an explicit integration policy or ablation variant
-> compare paper-faithful vs policy-assisted flow
```

Quality-improving guards are allowed, but they should be named as policy,
ablation, or flow-integration mechanisms.  They should not overwrite the
paper-faithful reference implementation.

## Integration Policy Notes

- The canonical OpenROAD DPL / Diamond-style flow remains the strict comparison
  anchor and can remain a controlled final bridge while the staged algorithm
  matures.  Do not describe it as the universal strongest parent; evolved
  legalizer seeds and case-specific elites may be better starts for a given
  Teacher/Student round.
- If diamond runs after guidance, the report must state how much was done by
  guidance and how much was resolved by diamond.
- Negotiation/NBLG is a repair donor, not a LEGALM paper stage.  If it runs
  unconditionally inside the default LEGALM path, it can dominate the final
  placement and make later Stage 1/2/3 edits appear ineffective.  Keep it as an
  explicit policy-assisted fallback or ablation path, and label those results
  separately from `legalm_only`.
- Stage metrics should be produced by each stage directly.  Avoid heavyweight
  generic callbacks between stages.
- Use ODB as the handoff artifact; do not add DEF/SDC/Verilog writes to the
  stage flow unless a test explicitly needs them.

## Required Telemetry

At minimum, a paper-faithful LEGALM-like run should emit:

- relaxed projection assigned/unassigned counts,
- initial overflow bins/sites and max density,
- ALM multiplier update iterations,
- BGD stencil size and candidate evaluations,
- accepted BGD moves per iteration,
- penalty/overflow convergence trace,
- partition/local-optimum escape trigger count,
- Stage3 no-overflow refinement move count,
- final handoff used or not used,
- final placement legality status from strict evaluator.

## How To Use This Insight

When a future agent modifies `detailed_placement_evolve`, ask first:

```text
Is this a paper-faithful implementation detail,
or is this a flow-integration policy?
```

If it is paper-faithful, keep it in the core stage and cite the mechanism it is
trying to reproduce.

If it is flow policy, keep it explicit, measurable, and easy to disable for
ablation.  Do not let it obscure whether the paper-derived algorithm is
actually working.
