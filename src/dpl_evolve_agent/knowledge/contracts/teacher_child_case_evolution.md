# Teacher/Child Case Evolution Loop

## Agent Use

- Role: workflow or metric contract. Use to keep experiments and reporting consistent, not to choose an algorithmic route.
- Route selection still starts from the core case-type blueprint map and current metrics.


The current DPL-Evolve direction is no longer "grow many family variants."
The stable entrypoint is `detailed_placement_evolve`, and the algorithmic
implementation inside `tools/OpenROAD/src/dpl_evolve` is intentionally
evolvable case by case.

## Roles

Teacher Agent:

- owns the case-level objective and evidence,
- compares against canonical `openroad_dpl_flow` and best evolved artifacts,
- assigns distinct hypothesis slots to child workers,
- reads each child's result artifacts,
- reviews child code changes, diffs/patches, telemetry, logs, and metrics,
- explains why each implementation helped, failed, or merely shifted cost,
- preserves the best-so-far source, diff, binary, logs, and metrics as an
  elite artifact,
- promotes only one or two promising directions into the next round when they
  beat the strict final metric gate,
- treats weaker but interesting implementations as donors or negative evidence,
  not as automatic parents.

Child Agent:

- owns one bounded mechanism for one round,
- continues its own code lineage only when Teacher keeps it alive as a viable
  parent,
- may instead inherit the best-so-far elite artifact or borrow a mechanism from
  another child when Teacher says the student's own lineage regressed,
- starts from concrete source references, not blank brainstorming,
- edits only the evolved DPL implementation surface,
- builds and evaluates its own candidate,
- reports touched files, mechanism, metrics path, telemetry, and validity /
  complexity assessment,
- does not edit evaluator, benchmark selection, or classic `dpl`.

## Multi-Iteration Loop

1. The orchestrator refreshes the canonical baseline suite for the case:
   `openroad_dpl_flow`, `openroad_dpl_negotiation`, and `evolve_default`.
2. Teacher reads the current packet, the baseline metrics table, and the
   explicit report/log/result paths for those baseline rows.
3. Teacher starts children with non-overlapping hypotheses:
   - an optional `framework` dense-prior diagnostic route when design
     evidence shows high utilization, standard-cell density, or large
     global-to-legal HPWL disturbance, unless current full-flow HPWL evidence
     says that case archetype should prefer another route,
   - a `diamond` or Diamond-local route for local greedy closure,
     row/macro structure, or large-case local legality pressure,
   - a `default_negotiation` route for residual conflicts or overlap patterns,
   - a hybrid or improve-placement-primary route when final HPWL depends on
     after-legal recovery.
4. Each child starts from:
   - the current `tools/OpenROAD/src/dpl_evolve/src` framework,
   - `family_variants/legalm_guidance`,
   - one relevant donor such as OpenROAD DPL, NBLG, LEGALM, or
     DREAMPlace/Abacus,
   - Teacher's case-specific top-level framework guidance.
5. Each child implements one assigned route and runs the strict evaluator.  A
   route can include multiple coordinated code changes across legalization,
   improve placement, and their handoff when that is needed to test the
   mechanism.
6. Teacher reads every child's `codex_last_message`, operation summary,
   reported metrics, changed source code, and relevant logs.
7. Teacher compares every child against both the case baseline and the
   best-so-far elite artifact.  A newer iteration is not better just because it
   is newer.
   Stage-only wins are explanatory evidence; current full-flow HPWL evidence
   takes precedence when it contradicts a stage prior.
8. Teacher rejects failures and weak evidence, labels useful failures as
   donors or negative evidence, and writes concrete continuation guidance for
   the same student ids.
9. At least one child in the next iteration must continue from the best-so-far
   elite artifact.  Other children may continue their own lineage, branch from
   another child's good mechanism, or run a deliberately aggressive ablation.
10. Losing lineages are not frozen forever, but they must change strategy or
   borrow from a stronger parent instead of blindly continuing the same patch.
    From iteration 2 onward, Teacher should explicitly choose for each student:
    continue the original route, switch to another start/route family, or
    hybridize two families in a staged source-level flow.
11. Repeat until the case beats the OpenROAD DPL flow anchor on strict HPWL
   while preserving legality, runtime, and displacement guards.

## Context Transfer Rule

The loop should not rely on hidden session memory or path-only handoffs.
Compact prompts are allowed after iteration 1, but they must still inline the
small pieces of evidence that drive the next decision:

- canonical baseline rows,
- round scoreboard and best-so-far candidate,
- previous Teacher review excerpt,
- current Teacher plan excerpt for each child after Teacher finishes,
- this student's previous metrics/result trend when available.

Paths to logs, diffs, source repos, and source commit records are still required, but they are not a
substitute for an actionable summary.  If the next prompt only says "read these
paths" and contains no HPWL/runtime/displacement numbers or no Teacher feedback
text, the loop is not communicating enough.

Canonical baseline rows must come from the orchestrator baseline probe tags,
not from arbitrary student evaluations.  Student runs also use
`legalizer_mode=evolve_default`, so mode alone is not a safe baseline key.

## Teacher Insight Rule

Teacher is the information integrator.  Students should not spend every round
repeating the global comparison and strategy review.  Teacher must compress the
scoreboard, peer results, code-review findings, and best-so-far state into
short per-student insight packets:

- one short section per student,
- parent/seed to use,
- peer mechanism to borrow or avoid,
- likely code area or mechanism to try,
- success metric and validity/complexity requirement,
- what not to re-analyze.

Student prompts may include a compact peer table so each student knows what the
others tried and what happened, but this table is for execution context.  The
student should use its insight packet, then decide the concrete implementation
while editing.

## Runtime Artifacts

The orchestrator should keep round packets, prompts, lineage, manifests, and
baseline evidence in its own runtime state.  Knowledge should not name local
round paths or artifact filenames; it should only define what information must
exist and how Teacher should use it.

## Promotion Rule

A child result is only useful if it provides:

- clean build,
- strict legal result,
- HPWL/runtime/displacement comparison against baseline,
- telemetry explaining which stage produced the effect,
- an algorithmic validity and complexity assessment.

Fallback-to-default behavior is diagnostic, not success evidence.

A child result becomes a next-round parent only if the complete strict flow
beats the current best-so-far result on the active case, or if Teacher
explicitly marks it as the protected exploration branch for a different
objective such as runtime or displacement.  Stage-local proxy wins are not
promotion evidence by themselves.

The runtime gate is part of promotion.  Compare against the canonical
`openroad_dpl_flow` runtime for the same case, utilization, flow variant, and
thread count.  The multiplier is the current experiment's configured
full-flow evaluator-wall budget, such as the generated
`student_runtime_multiplier`; do not hard-code an old value into Teacher
reviews:

- `runtime <= configured_multiplier * baseline`: eligible for promotion if
  HPWL and legality pass,
- `runtime > configured_multiplier * baseline`: not promotable, even when
  final HPWL is strong,
- over-budget runtime with clear stage evidence: donor only; Teacher must
  assign a bounded rewrite rather than continue the slow implementation,
- over-budget runtime without clear stage evidence: failure, not donor.

Teacher should call out bad execution patterns explicitly when reviewing a
child:

- claimed mechanisms with zero accepted candidates or inactive telemetry,
- runtime-heavy searches whose HPWL comes only from uncontrolled random
  trial-and-error or repeated endpoint-like work, instead of scoped
  handoff/frontier targeting,
- stale binaries whose logs do not contain the new mechanism,
- continuing a stage-negative path after stage-wise evidence says the source of
  improvement is in another stage,
- spending the round on build/archive workarounds instead of using the
  supported private variant build path.

## Elite Preservation Rule

The loop must never lose the best known code path for a case:

- keep the best source snapshot, implementation diff, relinked binary,
  evaluator JSON, strict metrics, and relevant logs,
- copy or branch that artifact into at least one child workspace in the next
  iteration,
- compare all new candidates against that artifact, not only against the first
  baseline,
- if every new candidate regresses, continue from the preserved elite and
  record the regressions as negative evidence.

## Metric Interpretation

The promotion metric is the final strict evaluator result after the configured
flow.  Use `metrics.json:hpwl`, which is parsed from OpenROAD/DPL pin-based
log HPWL.  Legalizer-stage HPWL, local HPWL proxy, row-assignment coverage,
seed counts, or Stage3 local improvements are diagnostic signals only.

## Micro-Gain Rule

Tiny strict HPWL wins should be preserved as elite donors, but they are not
evidence of a meaningful algorithmic breakthrough.  Teacher must say this
explicitly when the gain is only a small local-polish improvement, and at least
one child should continue to pursue larger flow/objective/assignment/repair
changes instead of spending every round on quota or threshold tuning.

See `../case_evolution/micro_gain_trap_and_elite_donors.md` for an example
where a clean but tiny strict HPWL improvement was useful to keep but too small
to count as a strategic DPL improvement.

This matters because a legalizer-stage result may look worse while the complete
flow improves after downstream polish, and a local proxy improvement may still
hurt final strict HPWL.  Teacher should use stage metrics to explain why a
patch worked or failed, not to replace the final strict gate.
