# AGENTS.md

This file is the repo-level execution contract for coding agents maintaining
`dpl_evolve_agent`. It is not the runtime Teacher prompt and it is not the
runtime Student prompt.

Human setup and common commands live in `README.md` and `scripts/README.md`.
Runtime Teacher/Student behavior is generated from `prompt_templates/`.

## Agent Roles And Prompt Locations

### Repo Agent

The repo Agent is the agent editing this repository. Its job is to keep the
framework publishable:

- Maintain scripts, prompt templates, knowledge cards, patch catalogs, docs,
  and validation helpers in this repo.
- Keep tracked files self-contained and machine-portable. Do not hardcode
  local absolute paths in tracked scripts, prompts, knowledge, or patches.
  Use `DPL_EVOLVE_AGENT_ROOT`, `ORFS_ROOT`, and `DPL_EVOLVE_STATE_ROOT` as the
  directory prefixes for tracked docs, scripts, and generated packets.
- Keep runtime-generated outputs under `DPL_EVOLVE_STATE_ROOT`; do not commit
  state-root directories, ORFS run outputs, or private variant workspaces.
- Validate changed Python and shell scripts before launching experiments.
- Keep prompt text in `prompt_templates/` when possible. Python may assemble,
  substitute, and index prompt fragments, but should not grow hidden long-form
  Teacher/Student instructions.

Repo Agent rules are defined here:

```text
AGENTS.md
```

### Evolve Teacher Agent

The Teacher Agent is created by the evolve loop. Its job is to review evidence
and assign mechanism-level routes to Students.

Teacher source templates live here:

```text
prompt_templates/teacher_loop/teacher/
prompt_templates/teacher_loop/context/
prompt_templates/teacher_loop/shared/
```

Generated Teacher prompts for one round live here:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/iter_XX/prompts/teacher_plan.md
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/iter_XX/prompts/teacher_review.md
```

The Teacher must lead with stage-wise metrics, logs, source evidence, Student
cards, and prior negative evidence. Skill/knowledge queries are inspiration and
risk checks, not the primary route source. It should assign source-level
mechanisms, not blind patch commands. It should make the route emphasis explicit:
`legalizer-emphasis`, `DPO-emphasis`, or `co-optimization-emphasis`; this is a
priority label, not a restriction to one stage.

Teacher must not treat `HPWLlg` as an isolated objective. Legalization-side
routes are useful only when they improve the complete handoff state: final HPWL,
legality, avg/max displacement, DPO recoverability, and downstream exact
accepted gain. `HPWLlg` movement that worsens those signals is negative
evidence.

Teacher should not route Students into broad knowledge-base reading by default.
When knowledge is needed, Teacher names the specific card, blueprint, or query
and the mechanism question it answers. Teacher may also assign a Student to
inspect another Student's source ref when peer code is useful; in that case,
name the peer ref, mechanism to borrow, and expected adaptation.

### Evolve Student Agent

The Student Agent is created by the evolve loop. Its job is to implement one
Teacher-assigned route in its private `dpl_evolve` source workspace, evaluate
it with the canonical full flow, and report exact evidence.

Student source templates live here:

```text
prompt_templates/teacher_loop/student/
prompt_templates/teacher_loop/packets/
```

Generated Student prompts and workspace packets live here:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/iter_XX/prompts/student_XX.md
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/iter_XX/packet/student_XX_workspace.md
```

Student private source workspaces live here:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/students/student_XX/workspace/variant/dpl_evolve
```

Students should plan from Teacher guidance plus current source diagnosis,
patch the private source, build/evaluate, inspect metrics/logs, revise when the
mechanism is live but weak, use the generated trial helper to keep/reject
workbench branches safely, finalize the best clean evaluated source, and write
the final report. Students should not edit external evaluator scripts, ORFS flow
scripts, or repository-level prompt templates during an evolve run.

Students should not read the broad knowledge base, papers, or donor trees by
default. They should use the Teacher packet, workspace entry points, current
source, canonical metrics, and DPL logs first. They open knowledge only when
Teacher names a card/blueprint, the assigned mechanism is unclear, or their own
log diagnosis shows the route needs a pivot. If Teacher assigns peer-code reuse,
Students inspect the peer ref with the generated helper, summarize the mechanism
briefly, and port the idea deliberately rather than auto-applying diffs.

Students should not stop for ordinary implementation uncertainty. They should
inspect nearby source patterns, call sites, or assigned donor refs, choose a
coherent implementation of the assigned mechanism, evaluate it, and record
assumptions. Each iteration allows one bounded self-diagnosis plus strengthened
repair before finalization; if the route still needs follow-up, the Student
writes it under
`## Next Teacher Handoff` in:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/students/student_XX/iter_XX/artifacts/knowledge_card.md
```

Teacher review reads that section with `source_trials.jsonl` and decides
whether the next round should continue the workbench, switch family, or
redesign producer/consumer composition.

Student self-diagnosis must use generated metrics summaries plus actual DPL
log/counter evidence when needed to prove whether the mechanism fired, why
candidates were accepted or rejected, what stopped the pass, and whether the
accepted delta or stage movement matches the intended HPWL source.
For legalizer-side changes, Students must diagnose `HPWLlg` together with
avg/max displacement, handoff/frontier liveness, DPO exact accepts,
accepted_delta, and final HPWL; `HPWLlg` improvement alone is not success.

## Runtime Layout Contract

`DPL_EVOLVE_STATE_ROOT` remains the global state root. Do not change its
meaning. New Teacher-round artifacts are grouped by round id:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
$DPL_EVOLVE_STATE_ROOT/<round_id>/checkpoints/operations/
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrices/
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrix_batches/
$DPL_EVOLVE_STATE_ROOT/<round_id>/start_seed_calibration/
```

`start_seed_calibration/` is the legacy runtime directory for the target-local
start-kind probe produced by `--calibrate-start-seeds`. Paper-level Level 1
calibration contracts live under `calibration/`. Mechanism calibration launched
with `--calibration-mode` is still a Teacher/Student round and writes raw
artifacts under `$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/`.
Generated paper-level diagnostic archives that are not one target round may
belong under `$DPL_EVOLVE_STATE_ROOT/calibrations/<calibration_id>/`.

During repo discovery, ignore global state roots unless the user or the current
packet names a specific round path. Runtime state is evidence, not repo
structure.

Codex session storage remains the normal Codex store, usually `~/.codex` unless
`CODEX_HOME` is explicitly set. The evolve framework records session identity
and thread ids in the round workspace, but it does not move the Codex session
store into `DPL_EVOLVE_STATE_ROOT`.

ORFS evaluator outputs stay in ORFS:

```text
flow/reports/<platform>/<design>/<FLOW_VARIANT>/dpl_evolve_baseline/<run_tag>/metrics.json
flow/logs/<platform>/<design>/<FLOW_VARIANT>/dpl_evolve_baseline/<run_tag>/
```

## Current Source Direction

The active project direction is a constrained detailed-placement evolution
framework. The evolved flow mirrors the OpenROAD detailed-placement flow:

```text
detailed_placement_evolve
improve_placement_evolve
optimize_mirroring_evolve
```

The canonical patch surface is the evolved detailed-placement source, not the
classic OpenROAD DPL implementation:

```text
tools/OpenROAD/src/dpl_evolve/
```

Do not edit classic `tools/OpenROAD/src/dpl/` unless a task explicitly changes
the project direction.

The current base framework patch is:

```text
patches/openroad_dpl_evolve_framework.patch
```

Evolved start seeds are cataloged under:

```text
patches/evolved_legalizers/
```

Teacher/Student use prepared start branches in generated workspaces. Patch
catalogs are release/reproduction artifacts used by repo-maintenance scripts to
materialize those starts; they are evidence, not manual Student commands.

## Source And Algorithm Scope

Valid source-level optimization work includes:

- legalization/detailed-placement candidate generation and ordering,
- global/differential guidance,
- row/segment/cluster assignment,
- negotiation/conflict repair,
- legality recovery,
- exact or cached HPWL objective scoring,
- transaction accept/rollback policy,
- bounded local polish,
- DPO candidate generation and acceptance,
- legalizer-to-DPO handoff state,
- lightweight counters and telemetry that prove mechanism liveness.

Parameter tuning is allowed when implemented inside the `dpl_evolve` source as
constants, thresholds, scoring weights, pass schedules, adaptive policies, or
compact internal config structs. Do not turn evolve into Tcl/flow-script knob
sweeps.

Hybrid algorithms should compose mechanisms inside one source flow. Do not
hybridize by adding case-name selectors among complete legalizers. Broader
search, repeated subpasses, or randomized perturbations are acceptable only when
they are source-level mechanisms with scope limits, counters, early-stop or
gain-rate evidence, and full-flow validation.

For legalizer-to-DPO handoff, prefer OpenROAD-native compact state such as
`odb::dbInst*`, `odb::dbNet*`, `odb::dbGroup*`, `odb::dbRegion*`, dense ids,
vectors, bitsets, and row/segment indices. Do not duplicate current coordinates
or pass hot algorithm state through JSON/text logs/string-keyed maps.

## Evidence And Metrics

Use evaluator-produced `metrics.json` as truth. The canonical HPWL is the
OpenROAD/DPL pin-based HPWL reported through `metrics.json:hpwl` and
`hpwl_stages`. `hpwl_proxy` is a debug-only cell-bbox proxy and must not drive
promotion, Teacher planning, public tables, or claims.

The headline optimization target remains legal final HPWL versus the OpenROAD
DPL default-flow Diamond baseline. Runtime is analyzed as value/cost evidence
and controlled by the configured timeout budget; it is not the primary
optimization objective.

Every evaluated Student source must be committed and pinned to a stable
candidate git ref generated by the workspace helper. Later iteration workspaces
may be reseeded, so elite continuation and matrix export must inherit from the
stable ref rather than a mutable workspace branch alone.

Hard failures include:

- build failure,
- evaluator failure,
- legality failure,
- missing or malformed metrics,
- stale source/binary/metrics mismatch,
- uncontrolled displacement or runtime regression.

## Build And Evaluation Rules

Build common OpenROAD once, then relink private variants:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/configure_openroad_core.sh"
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/build_openroad_core.sh" --threads 10
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/create_variant_start.sh" \
  --variant-root "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>" \
  --dpl-src /abs/path/to/variant/dpl_evolve
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/configure_openroad_variant_relink.sh" \
  --variant-root "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>" \
  --dpl-src /abs/path/to/variant/dpl_evolve
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/build_openroad_variant_relink.py" \
  --variant-root "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>" \
  --dpl-src /abs/path/to/variant/dpl_evolve \
  --threads 10
```

Variant/evolved runs must pass an explicit private binary. Do not assume the
root `tools/install/OpenROAD/bin/openroad` is the candidate binary.

Use generated workspace helper scripts during evolve runs. Do not reconstruct
build/relink/evaluator commands by hand unless a helper fails and the failure
itself must be diagnosed.

## Repo Hygiene

- Use `rg`/`rg --files` for discovery.
- Use coherent, reviewable diffs for tracked source edits. Mechanism-level
  rewrites are expected when Teacher guidance and source diagnosis justify
  them; keep the intent, touched surfaces, and validation evidence explicit.
  In Codex sessions, `apply_patch` is only an editing mechanism for current
  files; it is not an instruction to manually apply release/start patch files.
- Do not rewrite large files with heredocs or ad-hoc Python when a patch is
  sufficient.
- Do not revert unrelated user or experiment changes.
- Keep generated files ignored and outside commits unless the user explicitly
  asks to preserve an artifact as documentation.
- Before committing framework changes, run at least Python compile checks,
  shell syntax checks, and a smoke or dry-run that exercises prompt generation,
  workspace layout, and command assembly.
