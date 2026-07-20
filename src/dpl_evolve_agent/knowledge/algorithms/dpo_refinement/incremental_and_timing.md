# DPO Cards: Incremental Legality, Timing, And Multi-Row Moves

Scope: refine a legal placement with legality-preserving move or swap kernels.
These mechanisms should be bounded and should expose counters showing that the
intended move class actually ran.

## D5. Jezz / Incremental Legalization As A DPO Move Oracle

Source status: `source-handle-only`.

Source handles: `jezz_2015`.

Best use: legality dry run for swaps, insertions, and local reorders.

Core thesis: DPO can test more meaningful moves if legality can be checked and
rolled back incrementally.

Pseudo code:

```text
for candidate move:
    trial = incremental_row_oracle.try_insert_or_swap(candidate)
    if trial illegal:
        reject
    delta = exact_touched_net_hpwl(trial)
    if delta improves:
        commit oracle state
    else:
        rollback oracle state
```

Implementation handles:

- rollback-safe row-node data structure;
- exact delta after legal dry run;
- touched-row and touched-net caches;
- logs for oracle attempts, illegal rejects, rollbacks, and commits.

Failure pattern: the oracle is infrastructure, not a search strategy.  It needs
a strong candidate generator from nets, handoff queues, density stress, or
timing/criticality.

## D6. Hippocrates / First-Do-No-Harm Constraints

Source status: `source-handle-only`.

Source handles: `hippocrates_2007`.

Best use: protect timing/electrical constraints or avoid moves that worsen
known critical structures while optimizing HPWL.

Core thesis: placement transforms should be filtered so they do not harm
critical constraints, even when local HPWL improves.

Pseudo code:

```text
identify protected nets, pins, or cells from timing/electrical proxies
for candidate transform:
    if transform violates protected-region or critical-net guard:
        reject
    if exact HPWL improves and legality holds:
        commit
```

Implementation handles:

- cheap protected-net/cell tags;
- filter before expensive exact delta when possible;
- logs for protected rejects and accepted safe moves.

Failure pattern: strong do-no-harm filters can block HPWL progress.  Use them
only when the current objective includes those constraints or when stage logs
show harmful moves.

## D7. Timing-Driven Quadratic DPO With Incremental Legalization

Source status: `source-handle-only`.

Source handles: `timing_quadratic_2015`.

Best use: future timing-aware detailed placement; HPWL-only work can borrow the
quadratic target-generation idea as a bounded candidate source.

Core thesis: generate smoother target positions from timing or net objectives,
then use incremental legalization to realize only beneficial moves.

Pseudo code:

```text
compute target positions from quadratic objective
for cell in target-error order:
    candidate = nearest legal realization of target
    if candidate legal and exact objective improves:
        commit
```

Implementation handles:

- target field generation separated from legal realization;
- incremental legality oracle;
- exact objective check before commit;
- logs for target-error distribution, realized targets, and rejected targets.

Failure pattern: a target field that is not connected to legal realization can
look good mathematically while producing no legal HPWL gain.

## D8. MrDP / Multi-Row Detailed Placement

Source status: `source-handle-only`.

Source handles: `mrdp_2017`.

Best use: mixed-height or macro-edge DPO where single-row swaps cannot repair
legalizer artifacts.

Core thesis: some useful detailed-placement moves span multiple rows; DPO needs
bounded multi-row windows rather than only same-row swaps.

Pseudo code:

```text
identify hot multi-row windows from HPWL stress or handoff markers
for each bounded window:
    enumerate compatible cell moves across rows
    dry-run legal packing of affected rows
    score exact HPWL delta and displacement cost
    commit best legal non-conflicting moves
```

Implementation handles:

- small multi-row windows with strict caps;
- compatibility and mixed-height filters;
- exact local row repack before commit;
- logs for multi-row windows, legal candidates, accepted moves, and runtime.

Failure pattern: multi-row DPO can become too expensive or destabilizing.  Use
handoff/stress markers to target it; do not scan every row pair blindly.
