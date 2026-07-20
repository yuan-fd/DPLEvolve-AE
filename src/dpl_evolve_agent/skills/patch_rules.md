# Skill: patch_rules

## Principle

Codex edits only the bounded `private_dpl_evolve_source` module unless a task
explicitly expands outside it.  Within that private `dpl_evolve` source repo,
all files are available for coherent mechanism work: legalization/detailed
placement, improve placement/DPO, handoff/frontier state, objectives,
transactions, mirroring internals, helper classes, source-internal parameters,
and telemetry.  A Teacher start branch or first-patch handle is an orientation
hint, not a whitelist.

## Preferred Surfaces

In Student rounds, `private_dpl_evolve_source` is already the `dpl_evolve`
module root. Do not append `tools/OpenROAD/src/dpl_evolve` under it. Use these
module-relative paths:

- `src/EvolveLegalizer.cpp`
- `src/EvolveLegalizer.h`
- `src/EvolveContext.h`
- `src/EvolveTelemetry.h`
- `src/StudentAlgorithm.cpp`
- LEGALM-stage files under `src/Legalm*.{h,cpp}`
- detailed-improvement and objective files under
  `src/optimization/` and `src/objective/`
- `src/Optdp.cpp`
- existing `src/Place.cpp` or `NegotiationLegalizer*` files when the selected
  mechanism needs placement or negotiation/resource-allocation support.

These are preferred entry points, not an edit-scope boundary. If a coherent
mechanism needs other files under `private_dpl_evolve_source`, modify them and
record why in the knowledge card.

Do not edit classic `tools/OpenROAD/src/dpl/`.

## Reference Inputs

Reference inputs are optional and must be opened only through absolute paths
provided by the workspace packet or Teacher plan.  `private_dpl_evolve_source`
is a source-only git repo and does not contain `family_variants/`.

If donor evidence is needed, use repo-root references such as:

- `<agent_root>/knowledge/index/skill_cards.jsonl`
- `<agent_root>/knowledge/skills/`
- `<agent_root>/scripts/repo/query_knowledge.py`
- `<agent_root>/family_variants/REFERENCE_INDEX.yaml`
- `<agent_root>/family_variants/legalm_guidance/README.md`
- `<agent_root>/family_variants/openroad_diamond/README.md`
- `<agent_root>/family_variants/openroad_negotiation_nblg/README.md`
- `<agent_root>/family_variants/openroad_negotiation_nblg/mechanism_deltas/`
  only when assigned to negotiation-history mechanisms.
- `<agent_root>/knowledge/support/dpo/source_level_mechanisms.md`

If an absolute donor path is missing, skip it and continue from Teacher-named
source symbols. Do not search for donor files relative to the private source
repo.
Evolved starts are prepared as local git branches by the workspace scripts.
Patch files under the repo are release/reproduction artifacts, not ordinary
Student inputs.
Use the generated `08_query_knowledge.sh` helper when Teacher names a LEGO-lite
skill id. Open only the matching skill note and current source handles; do not
bulk-read all skill records.
Do not broad-read external archives or ignored local backups unless the task
explicitly asks for old family-flow evidence.
Do not open `~/.codex/memories`, rollout summaries, checkpoint event logs, or
old transcripts during ordinary Student source edits. The generated Teacher
packet and repo-local knowledge files are the evidence boundary.

## DPO Mechanism Use

- Improve-placement mechanisms belong in the current private source, especially
  `src/Optdp.cpp`, `src/optimization/`, `src/objective/`, and compact handoff
  structs.
- Useful mechanism families include bounded source-edge scoring, top-K exact
  candidate reduction, transaction accept/rollback, bounded staged descent,
  scoped LSMC-style basin escape, and legalization-to-DPO handoff
  consumers.
- Implement the selected mechanism hypothesis plus directly needed
  producer/consumer support, with compact counters/logs, bounded candidate sets,
  exact affected-net scoring, and full-flow validation. LSMC is
  allowed as a scoped basin escape when perturbations are controlled by window,
  dirty-row, touched-net, displacement, gain-rate, and runtime evidence; avoid
  blind random trial-and-error when a handoff/frontier signal can target the
  same recoverable region.
- Handoff payloads should be OpenROAD-native and compact. Use
  `odb::dbInst*`, `odb::dbNet*`, `odb::dbGroup*`, `odb::dbRegion*`, dense ids,
  vectors, bitsets, and current detailed-placement mappings. Do not duplicate
  current coordinates or route hot state through files, JSON, log scraping,
  string-keyed maps, or public Tcl/SWIG ABI layout changes.

## Patch Style

- Change one coherent route hypothesis at a time.  For Teacher/Student rounds,
  that route may include multiple related code changes spanning
  legalization/detailed placement, improve placement, and their shared
  objective/handoff; avoid unrelated side quests.
- Start from Teacher-named exact symbols and nearby code before patching.
  Expand to a deeper call-chain inspection only when exact-symbol lookup fails
  or the route is explicitly a `mechanism-redesign`. Do not drift into
  unrelated donor-source sweeps.
- Use coherent, reviewable source diffs on the generated dev branch for the
  Student; the generated iteration ref records the diff base.
  Mechanism-level rewrites are encouraged when they follow Teacher guidance,
  current source diagnosis, and a concrete validation plan. In Codex,
  `apply_patch`-style editing means changing current files with explicit hunks;
  it is not `git apply` and not a start-point mechanism. Do not manually apply
  release/start patch files; prepared start branches already encode those
  seeds. A scripted mechanical edit is acceptable when it is intentional,
  path-limited, previewed before use, and followed by
  `git -C "$private_dpl_evolve_source" diff --check` plus targeted inspection
  of the changed symbols. Do not use formatter-driven rewrites or unreviewed
  broad replacements.
- Patch only current file contents from the private source tree. Before each
  nontrivial patch, confirm the module-relative file with `rg --files`, locate
  the exact function/state with targeted `rg -n`, and inspect a small nearby
  line-numbered excerpt.
- Do not patch against remembered snippets, Teacher-plan prose, donor examples,
  old checkpoints, or another student's workspace. Those are evidence, not
  current patch anchors.
- Keep hunk anchors unambiguous. Large mechanisms may span multiple files and
  functions; decompose them into coherent verified edits by subsystem or
  producer/consumer boundary so review and rollback remain possible.
- After a successful patch, verify the changed symbol with targeted `rg -n` or a
  small excerpt, then run `git -C "$private_dpl_evolve_source" diff --check`
  before building. If you already changed into the private source repo, plain
  `git diff --check` is equivalent; do not run it from the outer agent repo.
- If a hunk edit fails, do not retry the same stale hunk. Re-open the current
  local source around the intended function/state and re-apply the mechanism
  against refreshed text; keep the edit coherent rather than shrinking it into
  a cosmetic tweak.
- For donor-derived edits or Teacher-prose-derived hunks, run the generated
  source-context helper first and patch against current local source text only.
  Never reuse donor line numbers or old hunk context as the patch anchor.
- Run the generated source-preparation script before code inspection. If
  Teacher assigns a prepared start point (`framework`, `diamond`,
  `default_negotiation`, `evolved_diamond`, or `evolved_negotiation`), use
  `prepare_start_source_script --kind <kind>`
  before edits. A prepared start is only the source parent; it never limits
  which `dpl_evolve` files can be changed. Do not launch prepare and switch
  helpers in parallel. After reading the workspace packet, required skills, and
  nearby Teacher-named symbols, patch a coherent mechanism and move to
  generated build/evaluate scripts. If broader source inspection is needed,
  keep it tied to the selected HPWL mechanism and stop once the
  producer/consumer boundary is clear.
- If you already ran `prepare_source_script` and still need to switch before
  edits, run `switch_start_branch_script` serially. Do not copy seed directories
  to emulate a start-point switch.
- Student workspaces use prepared git branches, then local source edits and
  commits.
- Keep `detailed_placement_evolve` as the primary evolved placement command.
- Keep external evaluator, baseline Tcl, ORFS flow, workspace helper, and
  benchmark-selection scripts locked. Inside `private_dpl_evolve_source`, it is
  valid to change the source-level implementation of `improve_placement_evolve`,
  including internal endpoint order, pass gating, candidate scoring,
  parameterization, and legalization/improve handoff state, when runtime remains
  bounded and canonical stage metrics prove the effect.
- Hybrid legalizer work should be source-level staged algorithm composition,
  not a case/scenario selector that switches among complete legalizers. Borrow
  ideas such as global guidance, resource prices, local compaction, or exact
  DPO transactions as mechanisms and wire them together with explicit
  producer/consumer state.
- Parameter tuning belongs in source code: constants, thresholds, scoring
  weights, pass schedules, adaptive policies, or compact internal config
  structs inside `private_dpl_evolve_source`. Do not implement a white-box
  route by adding or sweeping Tcl command options, changing ORFS flow scripts,
  changing evaluator scripts, or repeatedly invoking unchanged endpoints.
- Add telemetry when a new stage or repair operator executes.
- Add lightweight counters/logs for the mechanism when they help Teacher check
  whether the logic actually executed.

## Required Patch Note

For each patch, record:

- touched files,
- stage or mechanism name,
- expected HPWL/runtime/displacement/legality effect,
- expected failure mode,
- repair or failure-handling policy inside the algorithm.
- key counters/logs that prove the mechanism ran.

## Forbidden Behaviors

- simultaneous evaluator and algorithm edits,
- moving target-plane code into the control plane,
- adding third-party source directly into target code without explicit approval,
- claiming success without comparing against the current baseline,
- using fallback-to-default behavior as evidence of algorithm quality.
