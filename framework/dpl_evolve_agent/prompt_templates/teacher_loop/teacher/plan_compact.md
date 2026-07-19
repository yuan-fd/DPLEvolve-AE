# Teacher Follow-Up Plan

Context: `$context_path`
Current run packet: `$current_run_packet_path`
Design characteristics: `$design_characteristics_path`
Baseline artifacts: `$baseline_artifacts_path` (open only if the design-characteristics table is insufficient)
Peer learning: `$peer_learning_path` (open only if prior clean donors exist or a borrow decision is needed)
Start calibration: `$start_seed_calibration_path`
Manual continuation guidance: `$manual_teacher_guidance_path`

Use prior Teacher session memory. The context is a delta packet; do not reread
global rules or broad reference trees. Read the listed packet files directly.
Do not open `student_XX_workspace.md` during initial routing; that packet is
for Student execution unless a later review needs a concrete workspace path.
Do not spend time discovering them. Use small `rg`/excerpt checks around
concrete symbols only when the packet files do not already provide the needed
metric line, path, or source handle. Output only next student directions, not
build/eval procedure.
If manual continuation guidance is not `none`, read it before assigning
students and treat it as the current case-specific steering note.

## Student Slots

$route_lines

## Student IDs

Use exactly these headers:

$student_roster

Do not create, skip, merge, or renumber students.

$teacher_rules

$teacher_plan_output
