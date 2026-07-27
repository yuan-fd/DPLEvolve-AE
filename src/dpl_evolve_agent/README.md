# dpl_evolve_agent

`dpl_evolve_agent` is the control repo for evolving OpenROAD detailed-placement
algorithms on one placement case at a time.

The repo does not replace OpenROAD-flow-scripts.  It prepares an external,
clean `OpenROAD-flow-scripts` workspace, builds a shared OpenROAD core, gives
each Student agent a private `dpl_evolve` source tree, evaluates candidates
against strict baselines, and keeps all generated runtime state outside tracked
source files.

## What You Need

This is a white-box OpenROAD experiment harness, not a standalone Python-only
package.  Before launching real experiments, the machine must be able to build
and run OpenROAD-flow-scripts.

Required local tools:

- `bash`, `git`, `rsync`, `realpath`, `flock`, `awk`, `sed`, and `find`.
- A Python 3 interpreter with PyYAML importable as `yaml`. Runtime scripts
  resolve it into `DPL_EVOLVE_PYTHON`.
- OpenROAD build tools: `cmake`, `make`, a C++ compiler, and the normal
  OpenROAD/ORFS third-party libraries for your platform.
- A clean `OpenROAD-flow-scripts` checkout with `tools/OpenROAD` available.
- An installed Yosys usable by ORFS, either from ORFS setup or via `YOSYS_EXE`.
- The `codex` CLI configured for launched Teacher/Student runs.

Dry-run prompt audits do not call Codex and do not run OpenROAD.  Any command
with `--launch`, baseline execution, placement snapshot generation,
calibration, or DSE replay requires the real ORFS/OpenROAD environment.

The control repo uses git heavily.  ORFS and OpenROAD must be git checkouts, and
each Student source is an independent local git repo so candidate revisions can
be committed, exported, and replayed exactly.

Recommended layout:

```text
work/
  OpenROAD-flow-scripts/
  dpl_evolve_agent/
```

All commands below assume this repo is `dpl_evolve_agent`.

## Repo Map

- `scripts/`: canonical runtime, workspace, evaluator, matrix, calibration,
  orchestration, and repo-check helpers.
- `baseline/`: strict three-line baseline harness and metrics collection.
- `problems/`: single source of truth for case ids, designs, platforms, and
  design configs.
- `configs/experiment_plans/`: TSV DSE replay plans.
- `knowledge/`: active routing, skill, support, algorithm, and reference
  knowledge.
- `calibration/`: optional Level 1 calibration contracts, reviewed evidence,
  source-start provenance, and report layout.
- `family_variants/`: source/reference donors, not default baselines.
- `adapters/`, `learning/`, `metadata/`, `validation/`: thin support layers;
  each directory has its own README.
- `experiments/`: human campaign launchers and workbenches, not the default
  path for new runs.

Ignored runtime state belongs under `DPL_EVOLVE_STATE_ROOT`, preferably outside
this repo.  Treat existing local state directories as evidence for named rounds,
not as part of the control-plane structure.

## Quick Flow

The reproducible path is:

1. configure `DPL_EVOLVE_AGENT_ROOT`, `ORFS_ROOT`, and
   `DPL_EVOLVE_STATE_ROOT`;
2. prepare a clean ORFS/OpenROAD workspace;
3. build the shared OpenROAD core once;
4. create a placement snapshot for the discovery case;
5. optionally run calibration;
6. run one Teacher/Student discovery round on one case;
7. replay the committed Student sources across a TSV DSE plan.

Use `configs/experiment_plans/smoke_dse.tsv` for a one-row replay smoke, then
switch to `configs/experiment_plans/full_cross_case_core_util.tsv` for the full
cross-case/core-utilization DSE matrix.

## 0. Verify Local Dependencies

Run these before touching ORFS:

```bash
command -v bash git rsync realpath flock python3 cmake make codex
python3 - <<'PY'
import yaml
print("PyYAML OK")
PY
codex --version
```

If the Python check fails but another Python can import PyYAML, configure
`DPL_EVOLVE_PYTHON` in `env.sh` in the next step.

Then verify that ORFS itself can build on this machine.  Follow the dependency
installation flow from your OpenROAD-flow-scripts checkout, then return here.
This repo assumes those system packages are already present; it does not install
OpenROAD/ORFS compiler dependencies.

If `codex` uses a non-default session store, export it before launched runs:

```bash
export CODEX_HOME="/abs/path/to/writable/codex-home"
```

`scripts/run_codex_exec.py` records the selected Codex session store in each
Teacher/Student operation artifact.

## 1. Configure Runtime Paths

Create a local `env.sh`:

```bash
cd /path/to/dpl_evolve_agent
cp env.sh.example env.sh
$EDITOR env.sh
source env.sh
source "$DPL_EVOLVE_AGENT_ROOT/scripts/runtime_env.sh"
dpl_init_runtime "interactive"
```

At minimum:

```bash
export DPL_EVOLVE_AGENT_ROOT="/abs/path/to/dpl_evolve_agent"
export ORFS_ROOT="/abs/path/to/OpenROAD-flow-scripts"
export DPL_EVOLVE_STATE_ROOT="$(realpath -m "${DPL_EVOLVE_AGENT_ROOT}/../dpl_evolve_state")"
```

`env.sh` is ignored by git.  Keep machine-local paths there, not in tracked
files.  The runtime fallback and example use a sibling state root; you may
choose another external ignored path, but avoid placing large generated rounds
inside the control repo.

If the default Python cannot import `yaml`, add:

```bash
export DPL_EVOLVE_PYTHON="/path/to/python3"
export DPL_EVOLVE_PYTHON_CANDIDATES="/usr/bin/python3:/path/to/another/python3"
```

For every new shell, run the same runtime initialization before direct Python
commands:

```bash
source "$DPL_EVOLVE_AGENT_ROOT/env.sh"
source "$DPL_EVOLVE_AGENT_ROOT/scripts/runtime_env.sh"
dpl_init_runtime "interactive"
```

If a clean ORFS clone has a local OpenROAD submodule URL problem, point prepare
at a valid OpenROAD source:

```bash
export DPL_EVOLVE_OPENROAD_URL="/abs/path/to/OpenROAD"
```

## 2. Check The Control Repo

Run the lightweight repo gate:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/repo/check_release_readiness.sh" \
  --skip-teacher-dry-run
```

This static gate does not prove that ORFS can build or that Codex can launch;
it checks the control repo itself.

When `ORFS_ROOT` is ready and you want prompt-generation checks too:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/repo/check_release_readiness.sh"
```

## 3. Prepare A Clean ORFS Workspace

Start from a clean `OpenROAD-flow-scripts` checkout.  If there are local ORFS or
OpenROAD edits you care about, save them before this step.

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/prepare_workspace.sh" \
  --workspace-root "$ORFS_ROOT" \
  --force
```

`--force` resets ORFS/OpenROAD to the supported anchors, applies the
`dpl_evolve` overlay, and refreshes prepared source starts under:

```text
$DPL_EVOLVE_STATE_ROOT/seed_sources/
```

The active start branches are:

```text
framework
diamond
default_negotiation
```

## 4. Build The Shared OpenROAD Core

Build once after prepare:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/configure_openroad_core.sh"

"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/build_openroad_core.sh" \
  --threads 10
```

Later Student variants relink only their private `dpl_evolve` sources against
this core.  For later launches, pass `--skip-core-build` if this core is still
valid.

Load the shared binary for ORFS snapshot/baseline commands:

```bash
CORE_ENV="$(find "$DPL_EVOLVE_STATE_ROOT/openroad_core" -mindepth 2 \
  -maxdepth 2 -name core_env.sh | sort | tail -n 1)"
source "$CORE_ENV"
export OPENROAD_EXE="$OPENROAD_BINARY"
```

## 5. Create A Placement Snapshot

Each case run needs:

```text
$ORFS_ROOT/flow/results/<platform>/<design>/<FLOW_VARIANT>/3_4_place_resized.odb
```

Create one snapshot for a single case:

```bash
export FLOW_VARIANT="snapshot_aes_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/evaluator/run_place_batch.sh" \
  --case aes_nangate45 \
  --max-tasks 1 \
  --num-cores 8 \
  --flow-variant "$FLOW_VARIANT"
```

If `make check-yosys` fails in a clean ORFS clone, point ORFS at an installed
Yosys:

```bash
export YOSYS_EXE="/abs/path/to/yosys"
```

If the snapshot already exists, just reuse its `FLOW_VARIANT`.

Useful built-in case ids live in `problems/`, for example:

```text
aes_nangate45
jpeg_nangate45
ibex_nangate45
gcd_nangate45
aes_asap7
jpeg_asap7
```

## 6. Optional: Run Mechanism Calibration

Calibration is optional.  Use it when you want Teacher to assign many distinct
single-mechanism probes before a normal evolve run.

Calibration is one iteration only.  Scale breadth with `--children`, not with
`--iterations`.

Small dry-run check:

```bash
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id calib_dryrun_aes_$(date +%Y%m%d_%H%M%S) \
  --iterations 1 \
  --children 5 \
  --max-parallel 2 \
  --calibration-mode \
  --skip-baseline-preflight \
  --skip-core-build \
  --dry-run \
  --audit-prompts
```

Full 50-child calibration pool:

```bash
export ROUND_ID="calib_aes_50x10_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id "$ROUND_ID" \
  --start-kind framework \
  --iterations 1 \
  --children 50 \
  --max-parallel 10 \
  --threads 8 \
  --student-runtime-multiplier 1.0 \
  --calibration-mode \
  --calibrate-start-seeds \
  --skip-core-build \
  --audit-prompts \
  --launch
```

In calibration mode, Teacher is prompted to:

- assign one independent mechanism per Student;
- spread work across `framework`, `diamond`, and `default_negotiation`;
- spread mechanisms across legalizer, handoff/frontier, DPO, transaction,
  reorder/local DP, post-consumer preservation, and runtime-budget stages;
- avoid guard lanes;
- avoid mirror as the primary improvement path;
- review results as effective, repairable, or negative mechanism evidence.

Calibration artifacts stay under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
```

The Teacher review and Student `knowledge_card.md` files are the source for
later human-reviewed knowledge updates.

## 7. Run A Single-Case Evolve Round

First generate prompts without launching agents:

```bash
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id evolve_dryrun_aes_$(date +%Y%m%d_%H%M%S) \
  --start-kind framework \
  --iterations 1 \
  --children 2 \
  --max-parallel 2 \
  --skip-baseline-preflight \
  --skip-core-build \
  --dry-run \
  --audit-prompts
```

Then launch a smoke run with one Teacher and two Students:

```bash
export ROUND_ID="smoke_aes_1t2s_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id "$ROUND_ID" \
  --start-kind framework \
  --iterations 1 \
  --children 2 \
  --max-parallel 2 \
  --threads 8 \
  --student-runtime-multiplier 1.0 \
  --teacher-reasoning-effort low \
  --student-reasoning-effort low \
  --skip-core-build \
  --audit-prompts \
  --launch
```

Real launched rounds must keep baseline preflight enabled.  The launcher runs
the three canonical comparison lines before Teacher planning:

```text
openroad_dpl_flow
openroad_dpl_negotiation
evolve_default
```

For a larger single-case search:

```bash
export ROUND_ID="evolve_aes_4x2_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id "$ROUND_ID" \
  --start-kind framework \
  --iterations 2 \
  --children 4 \
  --max-parallel 2 \
  --threads 10 \
  --teacher-reasoning-effort high \
  --student-reasoning-effort high \
  --skip-core-build \
  --audit-prompts \
  --launch
```

## 8. Optional: Run Multi-Case Evolve

Use this only after every requested case already has a placement snapshot for
the chosen `FLOW_VARIANT`.

Create snapshots for the standard evolve set:

```bash
export FLOW_VARIANT="place_batch_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/evaluator/run_place_batch.sh" \
  --case-set evolve_9case \
  --max-tasks 3 \
  --num-cores 8 \
  --flow-variant "$FLOW_VARIANT"
```

Validate the campaign commands without launching Codex workers:

```bash
"$DPL_EVOLVE_AGENT_ROOT/experiments/launchers/run_evolve_9case_place_batch.sh" \
  --case-set evolve_9case \
  --flow-variant "$FLOW_VARIANT" \
  --run-prefix evolve_9case_smoke_$(date +%Y%m%d_%H%M%S) \
  --children 1 \
  --iterations 1 \
  --max-parallel 1 \
  --max-concurrent-cases 1 \
  --threads 8 \
  --skip-core-build \
  --dry-run
```

Launch the same campaign by removing `--dry-run`.  The launcher reuses complete
canonical baseline suites when present and fills missing baseline rows through
the Teacher loop; do not add any option that skips the three baseline lines for
a first real run.

For paper-scale campaigns, raise `--children`, `--iterations`, and
`--max-concurrent-cases` only after the single-case smoke path is clean.

## AE Environment Smoke Gate

Before running a DSE campaign on a new server, use the pinned AES environment
and native-baseline gate documented in `docs/AE_ENVIRONMENT_AND_SMOKE.md`:

```bash
./scripts/ae/check_environment.sh
./scripts/ae/run_aes_smoke.sh --check-only
```

The artifact still requires configured Teacher and Student Agents. This gate
only checks the pinned environment; a fresh EDA run is an explicit
`run_aes_smoke.sh --run` operation, and neither step replaces ReviewDSE.

## 9. Run DSE Replay Across A Plan

DSE replay has two phases:

1. a discovery round produces committed Student source revisions on one case;
2. matrix replay builds each fixed source once and evaluates that exact binary
   on every enabled plan row.

Choose a plan:

```bash
# One-row smoke plan.
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/smoke_dse.tsv"

# Smaller cross-case transfer plan.
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/flow_knowledge_core_util.tsv"

# Full DSE matrix.
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/full_cross_case_core_util.tsv"
```

First replay one candidate without skipping baselines:

```bash
export MATRIX_PREFIX="${ROUND_ID}_smoke_matrix_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh" \
  --round-id "$ROUND_ID" \
  --plan "$PLAN" \
  --matrix-prefix "$MATRIX_PREFIX" \
  --threads 8 \
  --matrix-row-parallel 1 \
  --student student_01 \
  --iteration iter_01 \
  --limit 1
```

This command first runs the plan baseline matrix.  For each enabled row it
creates or reuses `3_4_place_resized.odb`, runs the three canonical baselines,
then builds and evaluates the selected fixed Student source.

After the smoke pass, run the full round against the full plan:

```bash
export PLAN="$DPL_EVOLVE_AGENT_ROOT/configs/experiment_plans/full_cross_case_core_util.tsv"
export MATRIX_PREFIX="${ROUND_ID}_full_matrix_$(date +%Y%m%d_%H%M%S)"

"$DPL_EVOLVE_AGENT_ROOT/scripts/matrix/run_round_candidate_matrices.sh" \
  --round-id "$ROUND_ID" \
  --plan "$PLAN" \
  --matrix-prefix "$MATRIX_PREFIX" \
  --threads 10 \
  --matrix-row-parallel 6
```

Rerun the same command with the same `--matrix-prefix` to resume.  Complete
candidate matrices are skipped automatically.  Use `--skip-baseline-matrix`
only after the baseline matrix for that plan has already completed.

Key DSE outputs:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrix_batches/<matrix_prefix>/candidates.tsv
$DPL_EVOLVE_STATE_ROOT/<round_id>/candidate_matrices/<matrix_id>/results.tsv
```

`results.tsv` contains final HPWL, HPWL delta, stage HPWL, displacement, runtime,
and metrics paths for every candidate/plan-row pair.

## 10. Watch A Run

Terminal watcher:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/orchestration/watch_teacher_round.py" \
  "$ROUND_ID" \
  --watch
```

Local dashboard:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/orchestration/watch_teacher_round.py" \
  "$ROUND_ID" \
  --serve --host 127.0.0.1 --port 8765
```

Quick status report:

```bash
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/analysis/report_experiment_status.py" \
  --round-id "$ROUND_ID" \
  --detail-round "$ROUND_ID" \
  --children 2
```

## 11. Read Results

Round state:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
```

Important files:

```text
round.log
events.jsonl
PROMPT_AUDIT.md
iter_01/context/iteration_context.md
iter_01/prompts/
iter_01/packet/
students/student_XX/iter_01/artifacts/candidate_metrics_summary.md
students/student_XX/iter_01/artifacts/implementation.diff
students/student_XX/iter_01/artifacts/knowledge_card.md
```

Student private source:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/students/student_XX/workspace/variant/dpl_evolve
```

Baseline and candidate metrics:

```text
$ORFS_ROOT/flow/reports/<platform>/<design>/<FLOW_VARIANT>/dpl_evolve_baseline/
```

Use final `metrics.json:hpwl` for promotion decisions.  Legal-stage HPWL and
raw counters are diagnostic evidence, not the final ranking target.

## 12. Common Commands

Run only the canonical baselines:

```bash
"$DPL_EVOLVE_AGENT_ROOT/baseline/run_baseline_suite.sh" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --threads 8 \
  --tag-prefix baseline_aes_$(date +%Y%m%d_%H%M%S)
```

Validate prompt generation for calibration at the full 50-child size without
launching agents:

```bash
"$DPL_EVOLVE_PYTHON" "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case aes_nangate45 \
  --flow-variant "$FLOW_VARIANT" \
  --round-id calib_prompt_50_dryrun_$(date +%Y%m%d_%H%M%S) \
  --iterations 1 \
  --children 50 \
  --max-parallel 10 \
  --calibration-mode \
  --skip-baseline-preflight \
  --skip-core-build \
  --dry-run \
  --audit-prompts
```

Run repository checks before sharing changes:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/repo/check_release_readiness.sh" \
  --skip-teacher-dry-run

git diff --check
```

## More Documentation

- `calibration/README.md`: optional calibration contract and evidence layout.
- `scripts/README.md`: concise script inventory and namespace ownership.
- `docs/RUNTIME_CONTRACT.md`: runtime roots and generated state contract.
- `adapters/`, `learning/`, `metadata/`, and `validation/`: each has a
  directory README describing when humans or agents should use it.
- `docs/KNOWLEDGE_CONTRACT.md`: knowledge loading and evidence rules.
- `calibration/README.md`: tracked calibration evidence contract.
- `prompt_templates/teacher_loop/README.md`: prompt-template maintenance rules.
- `problems/README.md`: case registry format.
- `knowledge/policies/evidence_policy.md`: how to classify evidence.
