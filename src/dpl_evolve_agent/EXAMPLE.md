# Example: One Teacher/Child Case Optimization Round

This example shows the current default way to ask agents to improve one case.
The goal is not to tune an archived family.  The goal is to evolve the
algorithm behind the stable command:

```text
detailed_placement_evolve
```

## Create A Reviewable Round

Start with a dry run so the Teacher/child prompts can be inspected before any
agent edits code:

```bash
python3 "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case jpeg_nangate45 \
  --children 4 \
  --iterations 2 \
  --dry-run
```

The script writes:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/
```

Each iteration contains:

- `packet/current_run_packet.md`
- `prompts/teacher_plan.md`
- `prompts/student_XX.md`
- `prompts/teacher_review.md`
- `manifest.json`
- `README.md`

Student lineage lives under:

```text
$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/students/
```

## Launch Agents

After checking the prompts, launch through the existing Codex recorder:

```bash
python3 "$DPL_EVOLVE_AGENT_ROOT/scripts/optimize_case_with_codex.py" \
  --case jpeg_nangate45 \
  --children 4 \
  --iterations 3 \
  --max-parallel 2 \
  --launch
```

The generated Codex commands call:

```text
scripts/run_codex_exec.py
```

That wrapper records events, usage, last messages, and stderr under ignored
runtime state.

## What Students Read

Students should start from:

- the live prepared source under `tools/OpenROAD/src/dpl_evolve/src`,
- `family_variants/legalm_guidance/`,
- one relevant donor from `family_variants/REFERENCE_INDEX.yaml`,
- the Teacher prompt for the current iteration.

They should not broad-read old archives or edit evaluator/scoring code.

## What Teacher Reviews

Teacher should inspect:

- child code changes,
- build and strict evaluator results,
- stage telemetry,
- HPWL/runtime/displacement versus canonical `openroad_dpl_flow` and current
  evolve,
- failure signatures and in-algorithm repair policies.

Only one or two promising directions should continue into the next iteration.
The next prompt should tell those students how to modify their own code
lineage, not restart unrelated ideas.

## Independent Metrics

Acceptance is always based on strict evaluator output and metrics artifacts,
not on an agent's prose claim.  Durable lessons should be committed to
`knowledge/` or the database helpers with explicit evidence scope.

Use `metrics.json:hpwl` for HPWL decisions.  It is the OpenROAD/DPL pin-based
log HPWL.  Do not promote candidates based on the legacy bbox `hpwl_proxy`.
