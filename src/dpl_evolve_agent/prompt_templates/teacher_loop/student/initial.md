# Student: $student_id

- iteration: `$iteration_name`
- context: `$context_path`
- peer packet: `$peer_learning_path`
- workspace packet: `$workspace_packet_path`
- Teacher output: `$teacher_plan_last_message`

These paths are evidence pointers, not reading requirements. Start from this
prompt, then open the workspace packet for commands. Only open context, peer
packet, or Teacher output with targeted `rg`/small excerpts when a concrete
code decision requires it.

Use the assigned Teacher packet in `Teacher output` as the primary hypothesis,
then combine it with focused source inspection before coding. Do not copy broad
context into your working memory, and do not blindly execute Teacher prose when
the current source proves a better mechanism.

$elite_note

$student_rules
