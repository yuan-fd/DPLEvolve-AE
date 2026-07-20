# Skill: source_git_workflow

Use this when a Student edits its private `dpl_evolve` source workspace.

## Source Workspace

- The private source is `private_dpl_evolve_source` from the workspace packet.
- The private source path is the `dpl_evolve` module root. Source files are
  directly under `src/`, `src/optimization/`, and `src/objective/`; do not
  prepend `tools/OpenROAD/src/dpl_evolve/` inside the private repo.
- It is a local git repository.
- Work on the branch named `source_branch`, normally `dev/<student_id>`.
  The generated `iteration/<run_tag>` ref records the current iteration start
  commit.
- Do not create copied source backup trees. Git refs record the lineage:
  checkpoint refs, `kept/<run_tag>/...`, `rejected/<run_tag>/...`, and the final
  stable candidate ref.

## Required Flow

1. Open the workspace packet.
2. If Teacher assigns a prepared start point, run
   `prepare_start_source_script --kind <kind>` before edits. Otherwise run
   `prepare_source_script` once.
3. Confirm the generated script reports the expected branch and prepared
   `start/...` branches. Use `source_status_script` for a compact status check.
4. If Teacher asks you to reuse another Student's mechanism, run
   `fetch_peer_source_script --peer-repo <source_repo> --peer-ref <source_ref>
   --label <short-name>`. This fetches a peer ref and writes diff artifacts
   without modifying your working tree. Inspect the diff and port only the
   mechanism you understand onto your current dev branch.
5. For each mechanism workbench, use the trial state machine:
   - `trial_source_script begin --label <short-mechanism>`
   - when Teacher asks to continue a repairable rejected/kept line, use
     `trial_source_script begin --label <repair-name> --from-ref <ref>`
   - edit only files under `private_dpl_evolve_source`
   - build and evaluate with the generated helpers
   - `trial_source_script reject --reason <why>` for a rejected mechanism
   - `trial_source_script keep --reason <why>` for a retained mechanism
6. `reject` commits and pins the rejected code, snapshots diff/metrics into
   `source_trials.jsonl`, and leaves that code checked out on the dev branch.
   A rejected mechanism is not necessarily abandoned; it can become a repair
   workbench through `begin --from-ref` when Teacher classifies the issue as
   implementation or handoff repairable. To change direction, explicitly reset
   the dev branch with `switch_start_branch_script --ref <ref>` or
   `--kind <prepared-start-kind>`.
7. Before final answer, make the source tree match the source state you want
   Teacher to review and run `finalize_source_script`. Finalize pins the stable
   candidate ref, writes `source_commit.json`, writes `implementation.diff`
   from the iteration-start commit to current dev HEAD, and creates the
   knowledge-card skeleton if missing.

Generated workspace helpers that mutate the private source repo are serialized
by a file lock and must not be run in one parallel tool call:
`prepare_source_script`, `prepare_start_source_script`,
`switch_start_branch_script`, `build_variant_script`,
`evaluate_candidate_script`, `trial_source_script`, and
`finalize_source_script`. Parallel file reads and `source_status_script` are
fine after the source branch is prepared. `fetch_peer_source_script` mutates
local git refs and should be run serially, but it does not change the working
tree.

Run any manual `git diff --check` as `git -C "$private_dpl_evolve_source" diff
--check` or after `cd "$private_dpl_evolve_source"`. Generated build/evaluate
scripts may be launched from their script directory, but source git hygiene is
always scoped to the private source repo.

The orchestrator records the final commit after the Student exits. If the
Student leaves uncommitted changes, the orchestrator may create a fallback
commit, but that is a safety net, not the intended workflow.

The final recorded commit, `implementation_diff`, metrics row, and knowledge
card must describe the same code state. If the final state is rejected evidence
instead of a promotable result, say so explicitly; the code is still preserved
for Teacher review and possible continuation.

## Report

Report:

- source branch,
- final commit hash,
- stable candidate source ref printed by `finalize_source_script`,
- `kept/...` or `rejected/...` refs when relevant,
- dirty status after finalize,
- implementation diff path,
- any reason the Student could not finalize.
