# Legalization Cards: Global ALM And Exact Local Repair

Scope: legalizers that use a global continuous view to reduce overflow, then
translate the result into legal row/site assignments.  These mechanisms are
natural for LEGALM-family work and for exact local repair around mixed-height
or adjacent-row conflicts.

## L7. LEGALM / Linearized Augmented Lagrangian Legalization

Source status: `checked-source-text` for the main LEGALM source handle when
available; use `source-handle-only` for follow-up details not rechecked in the
current repo.

Source handles: `legalm_ispd2025`, `legalm_2_0`.

Best path: LEGALM.

Core thesis: use a global augmented-Lagrangian overflow model to guide cells
toward legal resource regions, then refine to zero overflow with a legal
assignment and local polish.

Pseudo code:

```text
initialize placement from global coordinates
lambda = zeros for resource overflows
for outer iteration:
    compute cell gradients from HPWL proxy, displacement, and overflow penalty
    update cell targets with bounded step
    assign cells to compatible row/resource regions
    overflow = measure resource violations
    if overflow decreases:
        keep state and reduce step conservatively
    else:
        escape or adjust penalty/step
    update lambda using overflow
    if overflow is zero:
        break
run zero-overflow refinement:
    legal row/site assignment
    local HPWL/displacement repair without losing legality
```

Implementation handles:

- global guidance must remain connected to the final legal assignment;
- lambda/overflow state should be logged and bounded;
- local refinement should consume the LEGALM target field instead of replacing
  it with a different legalizer;
- logs for overflow, lambda norm, accepted target updates, legal/refined
  candidate count, and zero-overflow closure.

Failure pattern: broad target-field shaping can reduce legal-stage HPWL while
destroying DPO recoverability.  Accept only if full-flow metrics improve or if
the result is explicitly kept as a stage donor for handoff repair.

## L8. Fixed-Ordering / Double-Row Exact Subproblem

Source status: `source-handle-only`.

Source handles: `double_row_2021`.

Best path: LEGALM local closure, DIAMOND local repair, or NEGOTIATION residual
component closure.

Core thesis: when a small adjacent-row component is hard to close legally,
solve the fixed-order subproblem exactly or near-exactly instead of perturbing a
large region.

Pseudo code:

```text
identify adjacent-row component with bounded cell count
freeze external cells and fixed blockages
preserve relative order where required
build dynamic program over two row intervals:
    state = next cell index, used width in row A, used width in row B
    transition = place next cell in a compatible row interval
    cost = displacement + exact local HPWL proxy + constraint penalty
choose minimum-cost legal state
commit if exact legality and full-flow objective proxy pass
```

Implementation handles:

- strict component-size cap;
- compatibility checks for mixed-height cells and fragmented intervals;
- exact or cached touched-net delta after DP placement;
- logs for component size, DP states, legal solutions, rejected components, and
  max displacement.

Failure pattern: exact local repair is attractive but can become exponential.
Use it only for bounded residual components and fall back to an infeasible
certificate when the component is too large.
