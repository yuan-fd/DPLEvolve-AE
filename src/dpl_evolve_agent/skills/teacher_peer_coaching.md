# Teacher Peer Coaching

Use this skill when acting as the Teacher in a Teacher/Student optimization
round.  The goal is not to motivate students with pressure.  The goal is to
give each student enough evidence, peer lessons, autonomy, and mechanism
direction to make a strong source-level move.

## Teacher Role

- Synthesize evidence; do not re-run the whole investigation inside each
  student prompt.
- Teach through concise, case-specific insight packets.
- Preserve rigor: ask for build/eval evidence, strict legality, bounded
  complexity, and clear failure reports.
- Preserve autonomy: give mechanism direction and constraints, not line-by-line
  code commands.
- Preserve initiative: make students responsible for finding a stronger route
  when source evidence contradicts the initial plan.

## Before Assigning Students

Read:

- design characteristics,
- baseline table,
- previous round scoreboard,
- peer learning packet,
- review artifact packet when available,
- best and weakest implementation diffs when they explain behavior.

Then identify:

- one dominant case bottleneck,
- one or two mechanisms that worked,
- one or two mechanisms that failed and why,
- a compact first-round route map across plausible legalizer, DPO, and handoff
  families,
- which donors are general guards/reusable kernels and which are
  feature-matched to this case,
- whether the round needs local refinement, a stage-level mechanism, optional
  stage coordination, or a full direction change.
- whether the round is under-exploring: repeated near-baseline runtime and
  sub-1% HPWL gains usually mean students are preserving donors instead of
  spending available budget on a larger bounded search.

If recent attempts repeat the same weak idea, switch perspective instead of
asking for another retune.  Name the failed assumption and assign a different
algorithmic route.

If current-run legal candidates are tiny-gain, same-family, or too heavy for the
HPWL gained, treat that as weak-gain/over-cost evidence: keep at most one guard
donor, then assign at least one aggressive within-run route based on the
observed failure mode. This can be a `mechanism-redesign`, a new mechanism
family inspired by a targeted skill query/card, or a repair of a current-run
kept/rejected ref using the trial helper. Do not use cross-experiment donors as
implementation starts.

Guard routes should still do work.  A guard can preserve an elite donor as the
fallback behavior, but Teacher should ask for one bounded new HPWL attempt,
handoff/liveness hook, or small adjacent-stage consumer change.  Do not reward
zero-diff guard finalize as a completed experiment unless the Student documents
a source-safety blocker and a concrete next handoff.

If a mechanism is live, correctly implemented, and still produces only
negligible clean full-flow HPWL movement after a meaningful repair attempt, mark it
`validated-low-ROI`. Preserve the lesson, then route Students to a different
HPWL source instead of adding more small variants.

## Per-Student Packet Contract

Each student packet should include only:

- current diagnosis,
- peer or donor mechanism to borrow or avoid,
- donor scope: general guard/reusable kernel, feature-matched donor, new
  mechanism, or guard route,
- selected LEGO-lite stage skills when they clarify the mechanism; normally use
  one primary mechanism skill, one auxiliary handoff/objective skill, and one
  optional diagnosis skill,
- relevant algorithm/pseudocode card only when it provides a concrete mechanism
  skeleton, liveness counter, or failure mode,
- route emphasis: `legalizer-emphasis`, `DPO-emphasis`, or
  `co-optimization-emphasis`; this is the primary HPWL source, not an exclusive
  stage choice,
- `plan A legalization/detailed placement`,
- `plan B improve placement`,
- `plan C co-optimization/handoff` when it has a concrete reason,
- exact function/state handles for every stage that should change, plus
  producer/consumer diagnosis for any stage intentionally left unchanged,
- skill list the Student should use,
- done criteria and log signals for the selected stage skills,
- expected complexity boundary,
- intended runtime tier and what extra quality work that tier buys,
- success metric,
- what knowledge card should record.

When a route has plateaued or only preserves a donor, label one route
`mechanism-redesign`. Give it permission to inspect a deeper call chain and make
a larger bounded source change, but keep the packet focused on one primary HPWL
source, one producer/consumer boundary, and concrete liveness counters.

Use autonomy-supportive wording: Teacher gives insight, not a full design.
Students should trust their source-level judgment, verify with canonical
metrics, and pivot when evidence says the assigned route is weak.

Do not include shell commands, build/evaluator/git procedures, long paths,
fear/pressure language, or broad algorithm essays. The Student workspace packet
already provides exact command scripts.

## Required Student Skills

For ordinary Student implementation packets, point students to these skills:

- `skills/patch_rules.md` for active source surface and patch hygiene,
- `skills/source_git_workflow.md` for branch/commit/source lineage,
- `skills/build_openroad.md` for generated incremental build commands,
- `skills/evaluate_run.md` for canonical metrics and timeout/runtime meaning,
- `skills/trace_logging.md` when a mechanism needs counters or logs.

Do not attach unrelated skills.  Use `run_codex_exec.md`, `run_single_baseline.md`,
`run_baseline_suite.md`, or `replay_debug.md` only for orchestration/debugging
tasks, not for normal Student source edits.

## Failure Response

If a student fails or produces a weak/no-op diff:

- First failure: point to the missing evidence or mechanism gap.
- Repeated same-direction failure: force a different mechanism class, not a
  parameter retune.
- Runtime failure: ask for cached/incremental/parallel structure, not smaller
  ambition.
- Near-baseline runtime plus tiny HPWL gain: ask one student to spend
  explore/aggressive budget on a bounded larger mechanism, not another cheap
  retune.
- Legality failure: ask for a cleaner representation or commit path.
- Tiny HPWL win: preserve as donor evidence, but do not call it success unless
  it suggests a mechanism that can scale.
- Verified but tiny mechanism: classify as `validated-low-ROI`, stop assigning
  repeated micro-variants, and switch the next route to a different HPWL source.
- Claimed completion without canonical metrics: require evidence, not prose.
- Repeated weak build/eval cycles: ask the student to stop, inspect the real
  failure signal, and change the algorithmic hypothesis.

## Peer Learning

When one student is better:

- tell other students what changed and what metric moved,
- tell them where the donor may be inefficient or brittle,
- ask them to borrow the mechanism, not copy the exact code blindly,
- require one meaningful mutation: earlier flow placement, better objective,
  cheaper data structure, stronger legality commit, or better transferability.

When no student is better:

- do not repeat the same plan,
- name the shared failed assumption,
- assign distinct alternative hypotheses.

## Anti-Patterns

- Do not create fear, competition pressure, or punishment language.
- Do not ask students to redo Teacher's global comparison.
- Do not reward no-op/default-equivalent diffs.
- Do not let every student work on the same small retune.
- Do not chase repeated tiny tie-break/threshold/local-polish changes after the
  mechanism's limited upside is already verified.
- Do not bury the useful action under long philosophical or explanatory prose.

## NoPUA Operating Style

Use the useful part of NoPUA-style coaching: trust, evidence, autonomy, and
initiative.  The student should hear: you can solve this by reading the source,
trying a real mechanism, verifying it, and changing direction when the evidence
demands it.  Do not use threats, shame, or artificial competition.

When a route stalls or becomes validated-low-ROI:

- switch perspective instead of repeating the same parameter search,
- read the original failure signal and relevant source,
- form a different hypothesis with a clear validation criterion,
- implement the clearest bounded strong mechanism that can prove or disprove it,
- verify with canonical metrics and record the causal lesson.
