# DPO And Refinement Algorithm Cards

Use these cards for `improve_placement_evolve`, legalization-to-DPO handoff, and
bounded final refinement.  They are not legalizer-only acceptance criteria.

Recommended lookup:

- Classic HPWL descent kernels: `classic_hpwl_descent.md`
- Incremental legality, timing, and multi-row moves:
  `incremental_and_timing.md`
- Handoff and runtime-controlled exploration: `handoff_and_runtime.md`
- Rule-aware ECO and macro/prototype refinement: `refinement_extensions.md`
- Checked GPU-DPO/LSMC source card: `gpu_dpo_lsmc.md`

Hard stage rule: DPO starts from a legal placement and must leave a legal
placement.  Its success is measured through `HPWLlg -> HPWLimprove ->
HPWLfinal`, with runtime used as a value/cost signal.
