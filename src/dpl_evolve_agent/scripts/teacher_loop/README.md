# Teacher Loop Python Modules

`scripts/optimize_case_with_codex.py` is the stable CLI shim.  The implementation
is split here by responsibility:

| Module | Responsibility |
| --- | --- |
| `cli.py` | CLI arguments for the stable `optimize_case_with_codex.py` entrypoint. |
| `orchestrator.py` | Top-level prepare -> baseline -> Teacher -> Student -> review loop. |
| `constants.py` | Shared start-kind and canonical-line constants. |
| `common.py` | Dataclasses, workspace paths, prompt template rendering, and round logging. |
| `evidence.py` | Baseline/candidate metrics, scoreboard generation, peer-learning packets, and artifact validity checks. |
| `context_packets.py` | Generated common context, prior-peer briefing, prior-iteration context, Teacher routing context, and case-route insight packet implementation. |
| `packet_builders.py` | Generated current-run, workspace, review, and round-summary packet implementation. |
| `prompt_rendering.py` | Prompt-safe excerpting, prompt audit, and Teacher/Student prompt construction. |
| `workspace_scripts.py` | Start-kind source resolution, parent-source selection, runtime timeout policy, executable-script writing, and generated Student helper-script implementation. |
| `prompts.py` | Legacy compatibility import module; new orchestrator code should import through the role-specific modules above. |
| `execution.py` | `codex exec` command construction, resume checks, command execution, lineage, source commits, and elite seeding. |

Keep fixed prepare/build/evaluate/commit/diff commands in generated Student
workspace scripts under `students/student_XX/.../scripts/iter_XX/`. Packet
files under `iter_XX/packet/` should point to those iteration-specific scripts
and exact artifact paths. Keep human-editable prompt prose in
`prompt_templates/teacher_loop/`.
