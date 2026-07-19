# Repo Redesign Blueprint

Scope: `dpl_evolve_agent` only.

This document captures the repo-only redesign plan for `dpl_evolve_agent`.
The outer workspace, paper sources, ORFS checkout, local backups, and runtime
artifacts are dependencies or local context, not part of this repo redesign.
It is a planning record; the current user-facing source of truth is
`README.md`, `docs/RUNTIME_CONTRACT.md`, `docs/KNOWLEDGE_CONTRACT.md`, and the
directory READMEs.

## Prefix Policy

Use environment variables for directory prefixes in scripts, generated packets,
and docs:

- `$DPL_EVOLVE_AGENT_ROOT`: tracked control repo.
- `$ORFS_ROOT`: external OpenROAD-flow-scripts workspace.
- `$DPL_EVOLVE_STATE_ROOT`: ignored runtime state root.

Hardcoded absolute paths and machine-local prefixes must stay out of tracked
files.  Relative paths are acceptable only inside wrappers that are locating
their own script before `runtime_env.sh` initializes the canonical roots.

## Method Alignment

The paper describes ReviewDSE as a two-level protected white-box DSE flow:

- Level 1 builds frozen method evidence and source-start branches from
  calibration designs.
- Level 2 performs target-case Teacher/Student source exploration using the
  frozen Level 1 material plus target-local metrics, logs, diffs, peer evidence,
  and Teacher review.
- The protected evaluator fixes benchmark inputs, command sequence, legality,
  metric parsing, reference comparison, runtime gates, and evaluator scripts.

The current repo has strong Level 2 machinery, but Level 1 is not yet a first
class directory or contract.  Current calibration code is mostly a target-round
start-kind probe (`calibrate_start_seeds.sh`) rather than the paper-level
method/source-start construction layer.

## Current Repo Surface

Tracked publishing surface is concentrated in these directories:

- `scripts/`: active control-plane helpers, Teacher loop, Codex exec recorder,
  evaluator runners, BO runner, candidate matrix tools, and reports.
- `prompt_templates/`: Teacher/Student prompt fragments.
- `knowledge/`: policies, route maps, mechanism support cards, skill cards,
  algorithm cards, and references.
- `family_variants/`: compact donor/source-reference shelf.
- `patches/`: prepare patches and evolved legalizer patch catalog.
- `baseline/`: protected evaluator Tcl/Python wrappers and canonical lines.
- `problems/`: case registry.
- `configs/`: reusable runner, BO, and mutation settings.
- `experiments/`: fixed campaign launchers and workbenches, not generic tools.
- `skills/`: repo-local operational notes for humans and Student workers.
- `validation/`: preflight guard for allowed source diff scope.

Ignored local state is outside the tracked surface:

- `$DPL_EVOLVE_STATE_ROOT`: runtime rounds, seed snapshots, OpenROAD core
  builds, candidate matrices, operations, logs, and packets.
- `.venv_raytune/`: local BO environment.
- `__pycache__/`, `*.pyc`, `env.sh`, and local checkpoint stores.

Known hygiene issues should be tracked in `docs/RELEASE_READINESS_REVIEW.md`
and enforced by `scripts/repo/audit_repo_hygiene.py`; do not leave ad hoc
findings in default runtime docs after they are fixed.

## Current Reading Chain

The main Level 2 runtime chain is:

1. `scripts/optimize_case_with_codex.py`
2. `scripts/teacher_loop/orchestrator.py`
3. `runtime_paths.py`
4. `scripts/repo/case_registry.py`
5. baseline preflight through `scripts/teacher_loop/evidence.py`
6. optional start-kind probe through `scripts/calibration/calibrate_start_seeds.sh`
7. packet generation through `scripts/teacher_loop/context_packets.py` and
   `scripts/teacher_loop/packet_builders.py`
8. Teacher plan prompt from `prompt_templates/teacher_loop/teacher/`
9. Student workspace packet and generated helper scripts
10. Student source edit, relink, evaluator run, metrics report, source commit
11. Teacher review prompt and target-local evidence update

The chain now has role-specific implementation modules for context packets,
packet builders, prompt-safe rendering/audit, prompt construction, and
workspace policy. The legacy compatibility module
`scripts/teacher_loop/prompts.py` now only re-exports compatibility names.

## Knowledge Classification

### Default Knowledge

Default knowledge is allowed to steer Teacher planning directly.

- `README.md`
- `AGENTS.md`
- `knowledge/policies/evidence_policy.md`
- `knowledge/contracts/metric_contract.md`
- `knowledge/contracts/teacher_child_case_evolution.md`
- generated `case_feature_route_insight_packet.md`
- `knowledge/routing/case_feature_to_mechanism_route_map.md`
- `knowledge/routing/mechanism_reconstruction_roadmap.md`
- `knowledge/index/skill_cards.jsonl` and
  `knowledge/index/mechanism_stack_cards.jsonl` through
  `scripts/repo/query_knowledge.py`
- `family_variants/REFERENCE_INDEX.yaml`
- `patches/PATCH_AUDIT.yaml`

### On-Demand Knowledge

On-demand knowledge should be opened only after Teacher names the route,
mechanism question, or failure bucket.

- `knowledge/skills/**`
- `knowledge/algorithms/**`
- `knowledge/support/dpo/**`
- `knowledge/support/legalization/**`
- `knowledge/support/legalm/**`
- `knowledge/support/case_evolution/*support*`
- exactly one assigned donor under `family_variants/`
- patch files under `patches/` only through their audit classification

### Reference-Only Knowledge

Reference-only material should not appear in default prompts.

- `knowledge/reference/**`
- paper PDFs and cached paper indices
- historical experiment launchers

### Weak Or Low-Utility Default Context

These are not useless, but they should not be read by default because they do
not decide normal Teacher routes.

- `memory/` and `database/` until the current Teacher loop actually consumes
  them as the authoritative memory backend.
- `learning/` until log-to-knowledge promotion is wired to reviewed evidence
  schemas.
- broad `knowledge/reference/` scans.
- fixed campaign scripts in `experiments/launchers/`.
- support insight files that repeat route logic already in the two core
  case-evolution files.

## Target Directory Design

The target design should keep compatibility shims while introducing clearer
ownership.

```text
$DPL_EVOLVE_AGENT_ROOT/
  docs/
    REPO_REDESIGN_BLUEPRINT.md
    RUNTIME_CONTRACT.md
    KNOWLEDGE_CONTRACT.md
  calibration/
    README.md
    plans/
    case_sets/
    evidence/
    source_starts/
    schemas/
    reports/
  scripts/
    runtime/
    workspace/
    evaluator/
    calibration/
    orchestration/
    analysis/
    bo/
  prompt_templates/
    teacher_loop/
  knowledge/
    policies/
    routing/
    skills/
    algorithms/
    support/
    reference/
    index/
  family_variants/
  patches/
  baseline/
  problems/
  configs/
  experiments/
```

Top-level script paths are no longer used as migration wrappers. New launch
commands should call canonical role-namespace paths directly.

## Calibration Directory Contract

`calibration/` should represent paper Level 1, not just target-round seed
probing.

Tracked calibration content:

- `calibration/README.md`: Level 1 purpose, protected boundaries, and how
  target search may consume frozen evidence.
- `calibration/case_sets/`: calibration-only case definitions and disjointness
  notes. These are diagnostic probes, not target tasks.
- `calibration/plans/`: reusable calibration matrix plans.
- `calibration/evidence/`: reviewed mechanism evidence records. These should
  use structured files, preferably JSONL plus concise markdown summaries.
- `calibration/source_starts/`: source-start branch specs and provenance, not
  generated source trees.
- `calibration/schemas/`: schemas for evidence records, source-start manifests,
  liveness fields, review outcomes, and disjointness checks.
- `calibration/reports/`: frozen human-readable summaries that Teacher may
  cite during Level 2 initialization.

Generated target-local calibration outputs remain under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
$DPL_EVOLVE_STATE_ROOT/<round_id>/start_seed_calibration/
```

Paper-level diagnostic archives that are not one target round may use
`$DPL_EVOLVE_STATE_ROOT/calibrations/<calibration_id>/`.

The current `start_seed_calibration` directory should be renamed in docs as a
"target start-kind probe" to avoid confusing it with paper Level 1 calibration.
The CLI flag may stay as `--calibrate-start-seeds` for compatibility.

## Runtime Read Order

### Repo Agent

1. `README.md`
2. `AGENTS.md`
3. `docs/REPO_REDESIGN_BLUEPRINT.md`
4. `scripts/README.md`
5. `knowledge/README.md`
6. `patches/PATCH_AUDIT.yaml`
7. only the files needed by the requested change

### Level 1 Calibration

1. `calibration/README.md`
2. selected `calibration/plans/*`
3. selected `calibration/case_sets/*`
4. protected evaluator contract from `baseline/` and `configs/`
5. assigned donor references from `family_variants/REFERENCE_INDEX.yaml`
6. produced metrics/logs under state root
7. reviewed records written to `calibration/evidence/`
8. source-start provenance written to `calibration/source_starts/`

### Level 2 Teacher

1. current generated `design_characteristics.md`
2. current generated `baseline_artifacts.md`
3. current generated target start-kind probe packet, if requested
4. current generated `case_feature_route_insight_packet.md`
5. prior Teacher review and peer-learning packet
6. elite source reference, if any
7. two core route files only when the generated packet is insufficient
8. knowledge index query for selected mechanism or blueprint-stack sharpening
9. one donor or support card only after assigning the route

### Level 2 Student

1. generated Student workspace packet
2. Teacher's assigned plan and route section
3. generated helper scripts in the private workspace
4. current private `dpl_evolve` source and nearby call sites
5. canonical candidate metrics and DPL logs
6. named skill/card/donor only when assigned or needed for a failure diagnosis
7. final source commit, diff, metrics summary, and `knowledge_card.md`

## Agent Behavior Contract

### Teacher

- Route from current case evidence first, not broad knowledge search.
- Never optimize `HPWLlg` alone.  Final HPWL, legality, displacement, DPO
  recoverability, runtime, and liveness decide promotion.
- Treat prepared starts as basins, not answers.
- Assign each Student a complete route statement: start basin, producer,
  handoff, consumer, counters, proof criteria, and first repair rule.
- Classify non-winning candidates as donor, repair lead, negative evidence, or
  rejection instead of losing the mechanism lesson.
- Do not update global Level 1 evidence during Level 2 target search.

### Student

- Edit only the private `dpl_evolve` source workspace.
- Use generated scripts for prepare, build, evaluate, report, trial, and
  finalize.
- Do not edit evaluator scripts, ORFS flow scripts, prompt templates, or repo
  control files during an evolve run.
- Prove mechanism liveness with counters/logs when the mechanism is new.
- Commit and pin the evaluated source ref before final reporting.
- Use one bounded self-diagnosis and repair when the route fires but is weak.
- Write the next handoff in `knowledge_card.md` for Teacher review.

### Repo Agent

- Keep runtime outputs ignored and out of commits.
- Keep local absolute paths out of tracked files.
- Keep top-level `scripts/` limited to stable shims and sourced runtime env.
- Run hygiene, syntax, and prompt/packet dry-run checks before claiming a
  framework refactor is safe.

## Refactor Plan

### Phase 0: Hygiene Fixes

- Add `patches/openroad_dpl_evolve_default_negotiation_seed.patch` to
  `PATCH_AUDIT.yaml`.
- Add `docs/` contracts.
- Make the distinction between Level 1 calibration and target start-kind probe
  explicit in README, AGENTS, and scripts docs.

### Phase 1: Calibration First-Class Directory

- Add `calibration/README.md`.
- Add evidence and source-start schema stubs.
- Move the start-kind probe under `scripts/calibration/`.
- Rename documentation from "start seed calibration" to "target start-kind
  probe" where the behavior is target-local.

### Phase 2: Runtime Module Split

- Split `scripts/teacher_loop/prompts.py` into:
  - `packet_builders.py`
  - `prompt_rendering.py`
  - `workspace_scripts.py`
  - `context_packets.py`
- Keep `orchestrator.py` as sequencing only.
- Current status: role-specific modules exist and orchestrator imports through
  them. `prompt_rendering.py` physically owns prompt-safe text excerpting and
  prompt-audit rules plus Teacher/Student prompt construction.
  `packet_builders.py` physically owns current-run, Student workspace,
  Teacher review, and round README packet generation.
- Current status: `context_packets.py` physically owns common context, prior
  peer briefing, prior-iteration context, Teacher routing context, and
  case-feature route-insight packet generation.
- Current status: `workspace_scripts.py` physically owns start-kind source
  resolution, parent-source selection, runtime timeout policy, and executable
  script writing plus generated Student helper-script construction.
- Current status: legacy `prompts.py` is now a compatibility import module
  rather than an implementation module.
- Current status: shared constants such as `START_KINDS` and `CANONICAL_LINES`
  live in `scripts/teacher_loop/constants.py`.

### Phase 3: Script Namespace Cleanup

- Group implementation scripts by role under `scripts/runtime/`,
  `scripts/workspace/`, `scripts/evaluator/`, `scripts/calibration/`,
  `scripts/orchestration/`, `scripts/analysis/`, and `scripts/bo/`.
- Remove old top-level wrappers after docs and launchers use canonical paths.
- Update `scripts/README.md` to show canonical paths only.
- Current status: BO implementation scripts live under `scripts/bo/`.
- Current status: workspace/build helpers moved to `scripts/workspace/`,
  evaluator/metrics helpers moved to `scripts/evaluator/`, cross-case replay
  helpers moved to `scripts/matrix/`, human-facing report/export helpers moved
  to `scripts/analysis/`, and target-local probe scripts live under
  `scripts/calibration/`.
- Current status: orchestration observability helpers live under
  `scripts/orchestration/`, repo-contract helpers live under `scripts/repo/`,
  and old top-level wrappers have been removed.

### Phase 4: Knowledge Taxonomy

- Move policy files into `knowledge/policies/`.
- Move direct route files into `knowledge/routing/`.
- Keep `knowledge/skills/` and `knowledge/index/`.
- Move algorithm cards into `knowledge/algorithms/`.
- Move support insights into `knowledge/support/`.
- Keep paper PDFs and donor references under `knowledge/reference/`.
- Regenerate and validate `knowledge/index/skill_cards.jsonl` and
  `knowledge/index/mechanism_stack_cards.jsonl`.
- Current status: Phase 4 taxonomy has been applied. Policy, routing,
  contract, algorithm, and support material now lives under
  `knowledge/policies/`, `knowledge/routing/`, `knowledge/contracts/`,
  `knowledge/algorithms/`, and `knowledge/support/`.

### Phase 5: Calibration-Aware Teacher Context

- Teach packet generation to read frozen Level 1 evidence manifests from
  `calibration/evidence/`.
- Add explicit "frozen global evidence" and "target-local evidence" sections.
- Prevent Level 2 from writing to global calibration evidence.

## Validation Gates

Minimum checks after each refactor phase:

```bash
"$DPL_EVOLVE_PYTHON" scripts/repo/query_knowledge.py --validate
"$DPL_EVOLVE_PYTHON" scripts/repo/audit_repo_hygiene.py
"$DPL_EVOLVE_PYTHON" -m compileall runtime_paths.py scripts baseline validation database memory learning
bash -n scripts/*.sh baseline/*.sh experiments/launchers/*.sh experiments/workbenches/*.sh
"$DPL_EVOLVE_PYTHON" scripts/optimize_case_with_codex.py --case jpeg_nangate45 --iterations 1 --children 1 --dry-run --audit-prompts
```

If script paths are moved, update `docs/MIGRATION_MAP.md` and run the repo
hygiene check so stale top-level scripts cannot reappear.
