# Calibration Mode Rules

## Calibration Objective

- This is a one-iteration mechanism calibration sweep, not a multi-iteration
  evolution round. Assign one independent mechanism hypothesis per Student.
  Do not route continuation, donor guarding, or staged follow-up as the primary
  purpose of any Student.
- The goal is to validate many source-level mechanisms quickly and aggressively:
  each Student should produce evidence that a mechanism is effective,
  ineffective, non-live, over-cost, or repairable. Negative evidence is useful
  when the failure bucket is clear.
- Use all available prepared source starts as calibration parents:
  `framework`, `diamond`, and `default_negotiation`. Spread Students across
  these starts unless current start-seed evidence proves a start is broken.
  Each assigned packet must include an explicit `start/parent:` with exactly one
  prepared start branch so the Student helper switches source before coding.
- Spread mechanism ownership across stages and links: legalization producer,
  legalizer-to-DPO handoff/frontier, DPO candidate generation, DPO exact
  scoring/acceptance, transaction/rollback, reorder/local-window consumer,
  post-consumer preservation, and runtime-budget substitution. Do not fill the
  roster with variants of one stage or one file.

## Mechanism Pool

- Prefer mechanism families that can materially change final HPWL reachability:
  new producer payloads, frontier handoff state, endpoint/source/cluster
  candidate generation, exact multi-cell transactions, acceptance quality,
  rollback discipline, local DP/reorder consumers, and budget-fused searches.
- Mirror is not a primary calibration path. Assign mirror-internal work only if
  it explicitly preserves a named accepted DPO/reorder/handoff gain. Keep mirror
  routes rare.
- Do not assign guard lanes. A calibration child should be a real mechanism
  probe, not "protect current best", "keep baseline safe", threshold tuning,
  telemetry-only work, or late polish without a named upstream mechanism.
- Be aggressive within the hard runtime budget. Prefer bounded experiments that
  can fail loudly with counters over conservative no-op guards. Aggressive means
  stronger transactions, broader but capped candidate families, new handoff
  payloads, or budget substitution; it does not mean unbounded loops.
- Avoid duplicate mechanisms. If two Students touch a similar family, they must
  differ in start branch, stage link, candidate object, acceptance rule, or
  proof target.

## Branch And Stage Coverage

- For large pools such as 50 Students, aim for approximate balance:
  framework-oriented mechanisms, diamond-oriented mechanisms, and
  default-negotiation mechanisms should all be represented.
- Cover at least these stage buckets when enough Students exist:
  legalizer producer, legalizer payload ranking, handoff/frontier lifetime,
  DPO source/endpoint candidates, DPO cluster/window transactions, exact
  acceptance/scoring, reorder/local DP consumer, post-consumer preservation,
  runtime-budget substitution, and implementation repair for known live-but-bad
  workbenches.
- For each Student, name the unique mechanism axis and the expected proof
  counters. If a route cannot be distinguished from another Student in one
  sentence, redesign it.

## Knowledge Use

- Use knowledge as a mechanism menu and failure checklist, not as a route
  selector. Name at most one primary stack/card and one support card per
  Student. Current case metrics and branch source behavior still decide the
  assigned proof target.
- Assign mechanisms that can update knowledge after review: the Student's
  evidence must identify the branch, stage, changed functions, counters,
  final/stage HPWL, runtime, legality, and failure bucket.
