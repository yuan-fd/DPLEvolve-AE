# Teacher Loop Prompt Templates

These files are human-editable prompt templates for
`scripts/optimize_case_with_codex.py`.

## Directory Layout

- `context/`: shared round context visible to Teacher and Students.
- `teacher/`: Teacher planning/review prompts, Teacher rules, and Teacher
  output contracts.
- `student/`: Student initial/follow-up prompts, Student rules, and small
  Student-only prompt fragments.
- `shared/`: reusable schema fragments embedded by both Teacher plan and review
  prompts.
- `packets/`: generated packet templates. These are still agent-facing, but
  Python only fills paths, ids, and measured values.

Rules:

- Keep agent-facing templates short.
- Keep shared facts in `iter_XX/context/iteration_context.md`; agent prompts
  should point to that file rather than embedding the whole context.
- Put exact paths, shell commands, run tags, and artifact locations in generated
  packet files under `iter_XX/packet/`, not in agent prompt prose.
- Workspace packets should be command/source maps, not second rulebooks. The
  first iteration may include more paths, but it should still stay concise;
  follow-up packets should be smaller and avoid restating global constraints.
- Teacher insight is appended to child prompts after Teacher runs, but only the
  assigned `### student_XX` section should be appended.
- Full first-round rules live in `teacher/rules.md`, `student/rules.md`, and
  `teacher/review_rules.md`. Follow-up prompts use the lighter
  `*_followup.md` rule files because Teacher/Student sessions already carry
  prior context; keep these follow-up files as reminders, not full policy
  restatements. Teacher plan/review output lives in `teacher/plan_output.md`
  and `teacher/review_output.md`. Shared per-student insight packet fields live
  in `shared/student_insight_packet_format.md`. Do not duplicate these blocks
  across templates.
- Teacher prompts should assign concrete per-student plans with route emphasis:
  `legalizer-emphasis`, `DPO-emphasis`, or `co-optimization-emphasis`.
  Emphasis is a priority label, not a three-way exclusive choice. Students
  should inspect and improve legalization, improve placement, and handoff when
  they can make high-quality mechanism changes, then validate the full
  legalize/improve/mirror flow. Student prompts should keep optimize mirroring
  callable for evaluation; mirror-internal edits are not the normal patch
  surface and require explicit Teacher assignment with concrete source handles.
- Teacher planning is two-stage. Iteration 1 should use the fuller context to
  build an evidence-driven case route map, using knowledge cards as targeted
  inspiration after the bottleneck is named. Follow-up iterations should use
  compact prompts and read less, but weak-gain, over-cost, or plateau evidence
  should trigger a different source-level hypothesis rather than polishing only
  the current donor.
- LEGO-lite mechanism lookup lives in `knowledge/index/skill_cards.jsonl`,
  `knowledge/index/mechanism_stack_cards.jsonl`, `knowledge/skills/`, and
  `knowledge/algorithms/`. Teacher packets should
  select relevant stage skills/cards only when they clarify the current
  evidence; Student reports should prove the adapted mechanism with source,
  metrics, and log evidence.
- Students should not read broad knowledge by default. Teacher names the
  specific card/blueprint/query when it is needed; otherwise Students use the
  Teacher packet, workspace entry points, current source, canonical metrics, and
  DPL logs first.
- Teacher may route peer-code reuse by naming a peer Student source ref and the
  mechanism to borrow. Student prompts should require a short mechanism summary
  before porting and should forbid blind diff application.
- Prompts should favor controllable algorithm complexity, bounded candidates,
  cached touched data, and direct HPWL mechanisms.
- Keep the NoPUA-style coaching signal in `skills/teacher_peer_coaching.md`:
  autonomy, evidence, initiative, and switching perspective after repeated weak
  attempts. Do not compress it into generic encouragement.
- Use `$variable_name` placeholders. Python fills them with `string.Template`.
- Keep new prompt prose in this directory. Python should only choose templates,
  compute values, and fill placeholders. If a generated packet needs new
  instructions, add or update a file under `packets/` instead of embedding the
  prose in Python.
- Do not hard-cap evidence text such as Teacher reviews. If a generated
  context becomes large, audit should warn while retaining the complete
  evidence or a complete source path.
- Run the prompt audit before launching a new prompt design:

```bash
"${DPL_EVOLVE_PYTHON:-python3}" scripts/optimize_case_with_codex.py \
  --case <case_id> \
  --children 2 \
  --iterations 2 \
  --skip-baseline-preflight \
  --dry-run \
  --audit-prompts
```

Audit output:

- `PROMPT_AUDIT.md`
- `prompt_audit.json`

Agent prompts live under `iter_XX/prompts/`. Generated machine packets live
under `iter_XX/packet/`, and generated context deltas live under
`iter_XX/context/`. Follow-up iterations should be visibly smaller than
iteration 1 because stable global rules are carried by the resumed session
rather than resent as a full prompt.
