# AGENTS.md — Agent Execution Contract

This file defines the executable contract for any coding agent (human or
machine) operating within the DPLEvolve Artifact Evaluation repository.

**This is not a tutorial. It is a set of rules, constraints, and invariants
that you MUST follow. Violations invalidate the artifact evaluation.**

---

## 1. Role and Boundaries

### You ARE permitted to:
- Read any file in this repository and its sibling repositories
- Execute scripts from `scripts/agent/` and `scripts/internal/`
- Create new timestamped experiment runs under `results/reproduced/`
- Create new flow variants under `$ORFS_ROOT/flow/results/`
- Write to `$DPL_EVOLVE_STATE_ROOT/` (state, build artifacts, checkpoints)
- Generate tables from structured results using `scripts/agent/summarize_results.py`

### You ARE NOT permitted to:
- Modify ANY file in `results/reference/`
- Modify ANY file in `provenance/` (except `provenance/current-machine.json`)
- Delete, move, or rename existing experiment outputs
- Overwrite existing flow variants or run tags
- Modify source code in `dpl_evolve_agent/` unless explicitly instructed
- Modify ORFS or OpenROAD source trees
- Install system packages or invoke `sudo`
- Export or transmit API keys, tokens, or private information
- Commit or push to any Git repository

---

## 2. Pre-Flight Checklist (Before ANY Experiment)

Before executing any experiment, you MUST verify:

- [ ] `make check` passes all critical checks
- [ ] `$YOSYS_EXE` points to the pinned Yosys binary (commit `8449dd470`)
- [ ] `$OPENROAD_EXE` points to the pinned OpenROAD binary (commit `d5ff63a`)
- [ ] The input ODB checksum matches the reference value in
  `provenance/original-artifact-checksums.txt`
- [ ] You have recorded the current machine provenance:
  `make provenance`
- [ ] The experiment run directory does not already exist
  (create a new timestamped variant; NEVER overwrite)

---

## 3. Run Execution Protocol

### Before the run:
1. Call `scripts/agent/inspect_environment.sh` — capture full environment
2. Create a unique `FLOW_VARIANT` and `RUN_TAG` with timestamp
3. Verify the target config file exists and is valid
4. Log the planned command, config, start time, and expected duration

### During the run:
1. Log every major phase start and completion
2. Capture exit codes from every subprocess
3. On non-zero exit: log the failing command, exit code, and tail of stderr
4. Do NOT retry without understanding the failure

### After the run:
1. Call `scripts/agent/validate_run.sh` on the output
2. Compare against `results/reference/` if available
3. Classify the outcome:
   - `PASS` — within tolerance for all hard gates
   - `CONDITIONAL_PASS` — hard gates pass but soft indicators differ
   - `FAIL` — any hard gate violated
   - `ENV_ERROR` — environment issue (wrong binary, missing lib, etc.)
   - `RUN_ERROR` — experiment crashed or timed out
4. Write the outcome to `results/reproduced/<run>/OUTCOME.txt`

---

## 4. Failure Classification

| Category | Example | Action |
|---|---|---|
| `ENV_MISSING_CMD` | `gcc: command not found` | Report missing dependency; do not install |
| `ENV_WRONG_VERSION` | Wrong Yosys version | Report expected vs actual; stop |
| `ENV_MISSING_LIB` | `libfoo.so: not found` | Report missing library; suggest `ldd` check |
| `RUN_SYNTH_FAIL` | Yosys synthesis error | Check input RTL; log full error |
| `RUN_PLACE_FAIL` | Placement crash | Check ODB; log OpenROAD stderr |
| `RUN_TIMEOUT` | Exceeded time budget | Log elapsed time; do not auto-extend |
| `RUN_OOM` | Out of memory | Log peak memory; suggest smaller case |
| `VAL_CHECKSUM` | ODB checksum mismatch | Check which Yosys was used |
| `VAL_HPWL_DRIFT` | HPWL outside tolerance | Log actual vs expected; check input first |
| `VAL_MISSING_OUTPUT` | metrics.json not found | Check if the run completed; check disk |

---

## 5. Safety Constraints (ABSOLUTE)

### Budget
- **NEVER** increase the experiment budget (cases, iterations, rounds)
  beyond what is specified in the config file
- **NEVER** auto-extend timeouts
- **NEVER** retry a failed run more than once without understanding the failure

### Data Integrity
- **NEVER** fabricate, estimate, or interpolate missing metrics
- **NEVER** copy reference results and present them as reproduced
- **NEVER** modify reference results to match reproduced output
- **NEVER** cherry-pick successful runs; report ALL attempts

### Provenance
- **ALWAYS** record the exact command, config, commit, and binary hashes
  for every run
- **ALWAYS** preserve complete logs; never truncate or filter stderr
- **ALWAYS** timestamp every run

### Secrets
- **NEVER** write API keys to any file, log, or output
- **NEVER** include tokens or credentials in error messages
- **NEVER** export `ANTHROPIC_API_KEY` or similar to provenance records

---

## 6. Path Conventions

All scripts use these environment variables. Do NOT hardcode absolute paths.

| Variable | Typical Value | Purpose |
|---|---|---|
| `$AE_ROOT` | `.../DPLEvolve-AE` | This repository |
| `$DPL_EVOLVE_AGENT_ROOT` | `.../dpl_evolve_agent` | Core framework |
| `$ORFS_ROOT` | `.../OpenROAD-flow-scripts` | ORFS workspace |
| `$DPL_EVOLVE_STATE_ROOT` | `.../dpl_evolve_state` | Build and run artifacts |
| `$DPL_EVOLVE_PYTHON` | `.../bin/python` | Project Python |
| `$YOSYS_EXE` | `.../yosys/8449dd470/bin/yosys` | Pinned Yosys |
| `$OPENROAD_EXE` | `.../openroad_core/d5ff63a/install/OpenROAD/bin/openroad` | Pinned OpenROAD |

---

## 7. Context Files

More detailed guidance in:
- `agent/context/project-map.md` — File map and dependency graph
- `agent/context/experiment-semantics.md` — What each experiment means
- `agent/context/invariants.md` — Non-negotiable invariants

Task-specific instructions in:
- `agent/tasks/reproduce-baseline.md`
- `agent/tasks/reproduce-main-results.md`
- `agent/tasks/validate-artifact.md`

Machine-readable schemas in:
- `agent/schemas/run-manifest.schema.json`
- `agent/schemas/result.schema.json`
