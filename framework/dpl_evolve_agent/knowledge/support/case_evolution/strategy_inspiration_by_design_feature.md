# Strategy Inspiration By Design Feature

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.


## Evidence Type

Measured observation distilled into feature-level guidance.  This card is not a
case-name lookup table and not a selector among complete legalizers.  Use it to
choose source mechanisms to compose, then prove the mechanism with full-flow
stage metrics and counters.

## Core Policy

For each route, select two or three mechanisms to compose inside one candidate
source flow:

1. an early global/resource/locality signal,
2. a native legal closure or repair mechanism,
3. a DPO consumer that preserves or exploits the produced frontier.

The useful object is the staged mechanism and its producer/consumer evidence,
not the algorithm family name alone.

## Feature Patterns

### High Utilization With Large Legalization HPWL Inflation

Good inspiration:

- lightweight global or differential target guidance to reduce gross movement,
- row-band or adjacent-row exact repair to keep legal closure local,
- residual/touched-net handoff into exact DPO windows.

Failure pattern:

- improving `HPWLlg` while creating a legal basin that DPO cannot recover,
- broad target reshaping without a DPO survival check,
- assuming a global legalizer is automatically the final parent.

Review signal:

- preserve the global route as a stage donor when legal counters and `HPWLlg`
  improve, but require an improve-placement consumer before promotion.

### Local Conflict With DPO-Friendly Basins

Good inspiration:

- Diamond/Tetris/Abacus-style ordered local repair,
- bounded row/segment compaction,
- exact touched-net scoring for local swaps or reorder windows.

Failure pattern:

- rerunning unchanged local endpoints,
- adding one-cell jitter that produces low accepted-gain rate,
- treating fast runtime as success when raw HPWL barely moves.

Review signal:

- keep local greedy as a second-stage repair inside a hybrid when it consumes a
  frontier from global/resource guidance; do not use it as hidden fallback.

### Scarce Row Resources, Fragmented Rows, Or Cut-Row-Like Pressure

Good inspiration:

- negotiation/resource pricing,
- sparse component assignment,
- row-segment capacity certificates,
- bounded exact packing of residual components.

Failure pattern:

- partial conflict reduction with residual illegal cells,
- unbounded full-design retry loops,
- using another legalizer as closure instead of fixing native resource repair.

Review signal:

- require conflict/resource counters, bounded component size, and either clean
  legal closure or a fast infeasible certificate.

### Macro Edges Or Row-Rich Wrappers

Good inspiration:

- segment-aware local windows around fixed obstacles,
- boundary-active row repair,
- DPO windows seeded from macro-edge touched nets.

Failure pattern:

- treating broken artificial row construction as normal legalizer evidence,
- wide global searches that ignore row segments and fixed cells,
- local macro-tail edits whose legal gain is erased by DPO.

Review signal:

- test whether the candidate changes a real row/segment or DPO frontier; do not
  promote macro-edge metadata without consumer counters.

### Transfer-Oriented Or Unknown Features

Good inspiration:

- robust bounded mechanisms with modest raw HPWL gain,
- exact affected-net scoring and deterministic candidate caps,
- lightweight logs that classify failure causes.

Failure pattern:

- promoting high-runtime discovery code without distillation,
- writing rules from one design name,
- copying a broad donor instead of extracting its mechanism.

Review signal:

- separate `HPWL elite`, `HPWL donor`, and `runtime-repair donor`.  A high-HPWL
  but slow mechanism should be rewritten with caches, caps, or parallel scoring.

## Teacher Checklist

- State the design feature, not a benchmark name.
- Pick mechanism inspiration from this card and one algorithm/pseudocode card.
- Ask Student to inspect current source before coding and to adapt the idea if
  code evidence contradicts the route.
- Require a staged source flow: producer, closure/repair, consumer.
- Require counters proving each staged component ran.

## Student Checklist

- Do not hard-code case names or route by design id.
- Do not implement a case/scenario selector among complete legalizers.
- Translate the feature into source-local objectives, candidate generators,
  compact state, and acceptance rules.
- After evaluation, report which stage produced the gain and which stage erased
  it if final HPWL is weak.
