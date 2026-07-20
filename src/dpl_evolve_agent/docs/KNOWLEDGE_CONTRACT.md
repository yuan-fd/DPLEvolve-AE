# Knowledge Contract

Knowledge exists to reduce search waste, not to override current protected
metrics.  Current target evidence, clean legality, final HPWL, runtime, source
diffs, and liveness counters are always stronger than stale notes.

## Default Route Knowledge

Default route knowledge may steer Teacher planning directly:

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

## On-Demand Knowledge

Open on-demand knowledge only after Teacher names the route, mechanism
question, donor, or failure bucket:

- `knowledge/skills/**`
- `knowledge/algorithms/**`
- `knowledge/support/dpo/**`
- `knowledge/support/legalization/**`
- `knowledge/support/legalm/**`
- support cards under `knowledge/support/case_evolution/`
- one assigned donor under `family_variants/`

## Reference-Only Knowledge

Reference-only material should not appear in default prompts:

- `knowledge/reference/**`
- paper PDFs and paper caches;
- historical launchers and old experiment plans.

## Evidence Discipline

Every promoted knowledge record should identify one of:

- `contract`: hard project boundary;
- `measured_observation`: result tied to a run, case, metrics path, and date;
- `working_hypothesis`: plausible mechanism explanation requiring a next test;
- `reference_donor`: source or paper mechanism to inspect and port;

Mechanism knowledge should record scope, source reference, controls/liveness,
full-flow metrics, compatibility observations, and review outcome.

## Mechanism-Stack Discipline

Mechanism-stack records are query-facing composition checklists.  They should
record:

- blueprint or route family;
- producer, handoff, consumer, and post-consumer roles;
- compatible follow-up links;
- failure buckets and liveness counters;
- first source handles for inspection.

Teacher may use stack records to assign a coherent route or compatible
follow-up, but current case metrics, source behavior, logs, and clean final HPWL
still decide whether the stack is useful. Student must translate a stack record
into source changes and counters before coding; reading the record alone is not
mechanism evidence.
