# Legalization Cards: Local Order And Cluster Repair

Scope: fast row-local legalizers that preserve enough global-placement order for
later DPO to recover HPWL.  These mechanisms are most natural for DIAMOND-family
work, but their objectives can be translated into other paths without calling a
different legalizer as fallback.

## L0. Tetris / Diamond Ordered Greedy

Source status: `derived-from-openroad/context` plus `source-handle-only` for
legacy Tetris references.

Source handles: `openroad_dpl_docs`, `tetris_2002`.

Best path: DIAMOND.

Core thesis: a very fast ordered greedy legalizer can be DPO-friendly when it
preserves nearby row/order structure and avoids early choices that block later
cells.

Pseudo code:

```text
build rows, legal site intervals, and fixed blockages
cells = movable cells sorted by global x, global y, stable id
for cell in cells:
    best = none
    for radius in increasing diamond shells:
        for site in candidate sites in shell:
            if cell fits the site interval and constraints pass:
                score = distance to global location + touched-net HPWL tie break
                best = min(best, site, score)
        if best exists:
            break
    if best missing:
        report first blocker and fail
    commit cell and update row occupancy
```

Implementation handles:

- bounded nearest-site enumeration;
- row-segment occupancy cache;
- stable ordering with a net-aware tie break;
- logs for placed count, average radius, max radius, first blocker, and exact
  HPWL tie-break usage.

Failure pattern: in high-utilization or fragmented-row designs, early greedy
placement can consume scarce intervals and make later cells expensive or
illegal.  Repair the ordering or local reordering mechanism; do not hide the
problem behind LEGALM or negotiation fallback.

## L1. Abacus / PlaceRow Cluster Legalization

Source status: `source-handle-only`.  Verify exact Abacus details before
copying the paper's implementation order.

Source handles: `abacus_2008`, `dreamplace_2019`.

Best path: DIAMOND, or as a row-assignment subproblem inside LEGALM/NEGOTIATION.

Core thesis: a row should be evaluated as an ordered cluster problem instead of
treating every occupied site as a fixed obstacle.  This can reduce displacement
and preserve a smoother basin for improve placement.

Pseudo code:

```text
cells = movable cells sorted by global x
for cell in cells:
    best_trial = none
    for row in bounded candidate rows near global y:
        trial = row state with cell inserted by global x order
        place_row_with_clusters(trial)
        score = row movement cost + vertical movement + local HPWL delta
        best_trial = min(best_trial, trial, score)
    commit best_trial

function place_row_with_clusters(row_state):
    clusters = empty
    for item in row_state.ordered_cells:
        append singleton cluster
        while last two clusters overlap:
            merge them
            set merged x to clamped weighted center
    expand clusters into site-aligned cell coordinates
```

Implementation handles:

- incremental row trials instead of full row copies;
- candidate-row caps that still include enough vertical alternatives;
- fragmented intervals and fixed cells in the row feasibility model;
- logs for row trials, cluster merges, committed rows, and movement cost.

Failure pattern: if cluster legalization improves legal-stage HPWL but final
HPWL worsens, the row order may be too smooth for DPO's current candidate
generator.  Keep it as a stage donor and fix the handoff or DPO consumer.

## L2. Jezz Incremental Legalization

Source status: `source-handle-only`.

Source handles: `jezz_2015`.

Best path: DPO legality oracle, DIAMOND local repair, or bounded incremental
repair inside another legalizer path.

Core thesis: represent each row as ordered nodes of whitespace, blockages, and
cells so candidate insertions can be tested locally and cheaply.

Pseudo code:

```text
row_nodes = ordered intervals of whitespace, blockages, and cells
build nearest insertion-window cache per row segment

function try_insert(cell, target_row, target_x):
    segment = locate segment around target_x
    if segment is blocked:
        return reject
    evaluate shifting neighbors left, right, or both sides
    choose minimum displacement legal shift
    return accepted dry-run placement with touched intervals

for candidate move:
    trial = try_insert(cell, row, x)
    if trial legal and exact HPWL improves:
        commit only touched row nodes
```

Implementation handles:

- persistent row-node state;
- dry-run insertion that can be rolled back cheaply;
- exact touched-net delta after the local legal dry run;
- logs for insertion attempts, accepted inserts, shifted-cell count, rollback
  count, and max local displacement.

Failure pattern: a Jezz-style oracle can become a no-op if candidate generation
never reaches scarce intervals.  Use it with a clear candidate source, not as a
standalone claim of improvement.
