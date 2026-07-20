# Legalization Cards: Resource Allocation And Flow

Scope: legalizers that treat rows/sites as scarce resources and repair conflict
through prices, flows, or augmenting paths.  These mechanisms are natural for
NEGOTIATION-family work and for difficult fragmented-resource designs.

## L3. History-Based Network-Flow Legalization

Source status: `source-handle-only`.

Source handles: `history_flow_2010`.

Best path: NEGOTIATION.

Core thesis: repeated legalization failures should raise the price of congested
resources so later assignments avoid the same conflict.

Pseudo code:

```text
initialize cell-to-row/site preference graph from global placement
history_price[resource] = 0
repeat until legal or iteration cap:
    build min-cost flow:
        source -> cells -> row/site resources -> sink
        cost = movement + local HPWL proxy + history_price
    solve or approximate the assignment
    realize assigned cells into row intervals
    if all cells legal:
        return legal placement
    for each overflowed or unrealized resource:
        history_price[resource] += penalty
    shrink candidate graph around unresolved components
report first infeasible component
```

Implementation handles:

- sparse candidate graph, not full cell-to-site expansion;
- deterministic resource-price updates;
- component-level realization after flow assignment;
- logs for overflow resources, price updates, flow size, realized cells, and
  unresolved components.

Failure pattern: a flow solution can be mathematically plausible but fail during
site realization.  The realization step and infeasible-certificate logs are as
important as the assignment score.

## L4. BonnPlaceLegal / Iterative Augmentation

Source status: `source-handle-only`.

Source handles: `bonnplacelegal_2013`.

Best path: NEGOTIATION, or bounded repair of a local greedy legalizer.

Core thesis: move a small connected set of cells through augmenting paths rather
than repacking the full design.  This preserves most of the existing placement
while relieving local overload.

Pseudo code:

```text
build zone graph from rows, intervals, and movable cells
while overloaded zones exist:
    choose highest-impact overloaded zone
    search augmenting path to a zone with spare capacity
    if path exists:
        move ordered cells along path with minimum displacement increase
        update zone capacities and local row order
    else:
        mark zone as structurally blocked
validate all cells and row intervals
```

Implementation handles:

- zone graph with incremental capacity updates;
- bounded path length and deterministic tie breaks;
- fixed-order packing along each path;
- logs for overloaded zones, path attempts, path successes, moved cells, and
  structural blockers.

Failure pattern: augmenting paths can improve legality while damaging final
HPWL if moved cells are selected only by displacement.  Add touched-net or
DPO-recoverability terms before increasing path depth.

## L5. Darav-Style Maximum-Movement Network Flow

Source status: `source-handle-only`.

Source handles: `darav_network_flow_2017`.

Best path: NEGOTIATION stress repair and cut-row-like resource pressure.

Core thesis: minimizing worst-case displacement is useful when legality is hard
and a few cells are pushed far away by local greedy decisions.

Pseudo code:

```text
for movement_bound in increasing bounds:
    build legal resource graph using only assignments within movement_bound
    solve capacity assignment or approximate it component-wise
    if assignment can be realized into row intervals:
        return legal placement
if no bound succeeds:
    report cells and intervals causing infeasibility
```

Implementation handles:

- monotone movement-bound search;
- component-wise graph construction for speed;
- row-interval realization that respects fixed cells and fragmented capacity;
- logs for bound trials, graph size, realized count, and max displacement.

Failure pattern: maximum-movement minimization can be too conservative for HPWL.
Use it to close legality or cap displacement, then add HPWL-aware local polish.

## L6. NBLG / Negotiation-Based Mixed-Height Legalization

Source status: `derived-from-openroad/context` plus `source-handle-only` for the
paper mechanism.

Source handles: `openroad_dpl_docs`, `nblg`.

Best path: NEGOTIATION.

Core thesis: cells negotiate for legal resources using target, congestion, and
history terms.  Conflict resolution is the mechanism, not a wrapper around
Diamond.

Pseudo code:

```text
initialize resource grid from legal rows and multi-height constraints
for round in negotiation rounds:
    for cell in priority order:
        candidates = legal resources near target
        score = target distance + congestion price + history price + HPWL proxy
        reserve best candidate or queue conflict
    resolve conflicts by raising prices and rerouting affected cells
    if all reservations are legal and realizable:
        materialize placement
        run bounded native postopt
        return legal placement
report unresolved conflict queues
```

Implementation handles:

- explicit resource grid with multi-height/fence compatibility;
- price updates tied to real conflict counters;
- bounded native postopt, not cross-path fallback;
- logs for resource candidates, conflict queue size, price updates, resolved
  conflicts, and fallback counters.

Failure pattern: negotiation often fails by leaving a small residual conflict
set.  The next mechanism should be a complete residual-component solver or a
clear infeasible certificate, not repeated global reruns.
