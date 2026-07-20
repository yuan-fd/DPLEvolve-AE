# DPO Cards: Handoff And Runtime-Controlled Search

Scope: connect legalizer outputs to improve-placement search so broader DPO work
is targeted by recoverability evidence instead of dominated by blind random
trial-and-error.

## D2. GPU-DPO / LSMC Large-Step Escape

Source status: `checked-source-text`.

Source handles: `gpu_dpo_lsmc_2026`.

Best use: algorithm shape for escaping DPO local minima; CPU work should borrow
bounded large-step search and cached kernels before any GPU assumption.

Core thesis: strong descent plus bounded legal large-step perturbation can find
better basins than repeated local passes, but acceptance must be objective-based
and runtime-bounded.

Pseudo code:

```text
best = current legal placement
for restart until failure cap:
    work = copy(best)
    run fast legal descent kernels on work
    if exact HPWL improves:
        best = work
        continue
    kicked = legality-preserving large-step perturbation(best)
    run fast legal descent kernels on kicked
    if kicked legal and exact HPWL improves:
        best = kicked
    else:
        increment failure count
restore best
```

Implementation handles:

- legality-preserving kick operator;
- cached exact deltas in descent kernels;
- deterministic acceptance;
- logs for kicks, descent attempts, accepted basin changes, failed restarts,
  and runtime per kernel.

Failure pattern: if runtime grows but accepted basin changes are rare, rewrite
candidate generation, caching, or parallelism before increasing restart count.

## D9. Handoff-Aware DPO From Legalizer Signals

Source status: `implementation-hypothesis` synthesized from stage-wise evidence
and paper mechanisms.

Source handles: `openroad_dpl_docs`, `fastdp_2005`, `gpu_dpo_lsmc_2026`.

Best use: co-optimization when legalizer gains disappear during improve
placement or when DPO cannot find useful neighborhoods.

Core thesis: legalization should pass compact signals about high-change,
recoverable, or high-priority regions so DPO searches the right neighborhoods.

Pseudo code:

```text
legalizer produces bounded handoff records:
    moved cells with large displacement
    nets whose HPWL changed substantially
    rows/segments with remaining whitespace or recoverability
    conflict or overflow components that were repaired
DPO consumes handoff:
    seed candidate cells/nets/segments from records
    run exact-delta move/swap/reorder around those neighborhoods
    accept only legal objective-improving changes
report producer and consumer counters
```

Implementation handles:

- bounded handoff storage, preferably in internal data structures rather than
  large text logs;
- producer and consumer counters;
- reason-coded candidate rejection;
- logs for records produced, records consumed, accepted handoff moves, and
  handoff runtime.

Failure pattern: handoff metadata is harmful if no later stage consumes it or if
it floods logs.  Aggregate records and expose only compact counters plus a few
top diagnostics.

## Runtime-Controlled Large Search

Source status: `implementation-hypothesis`.

Source handles: `gpu_dpo_lsmc_2026`, `abcdplace_2020`.

Best use: keep runtime as a usable resource while allocating it to higher-quality
search.

Pseudo code:

```text
budget = runtime budget from canonical baseline multiplier
while elapsed < budget and useful accepts continue:
    generate bounded candidates from strongest source
    evaluate candidates with caches or parallel batches
    commit deterministic non-conflicting improvements
    if accept rate and gain rate fall below threshold:
        switch mechanism or stop
```

Implementation handles:

- elapsed time by mechanism, not only total runtime;
- accept-rate and gain-rate counters;
- candidate caps tied to design size;
- deterministic early stop when no useful gain remains.

Failure pattern: a low runtime can mean the algorithm is under-exploring, but a
high runtime is only justified if counters show useful additional accepted
improvements.
