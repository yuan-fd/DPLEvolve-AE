# Problems

Each subdirectory defines one design/platform evolve problem.

A problem is the canonical control-plane handle for a case. It should answer:

- which design
- which platform
- which evaluation mode
- which baseline or objective framing we care about

## Convention

- one directory per case
- one `problem.yaml` per directory
- dense variants that reuse the same ORFS design config should set
  `recommended_flow_variant: DENSE`; launch scripts still pass the actual
  `--flow-variant` explicitly.

Examples:

- `gcd_nangate45/problem.yaml`
- `aes_asap7/problem.yaml`
- `ibex_nangate45/problem.yaml`
- `aes_dense_nangate45/problem.yaml`

## Notes

- Teacher/child case prompts are generated under
  `$DPL_EVOLVE_STATE_ROOT/<round_id>/teacher_rounds/` by
  `../scripts/optimize_case_with_codex.py`.
- Algorithm/reference context lives under `../knowledge/` and
  `../family_variants/`.
- Baseline outputs still live under the ORFS `flow/` tree
- `patch_surface` lists the current framework files that agents may modify for
  this case.  It intentionally includes LEGALM/legalization, row assignment,
  repair handoff, the `detailed_placement_evolve` interface,
  detailed-improvement internals, and shared legalizer/improve handoff so
  agents are not restricted to stale top-level scaffolding. Downstream
  optimize-mirroring remains callable and evaluated, but is not an active
  optimization surface.
  Detailed symbol-level policy lives in
  `../adapters/science_codeevolve/patch_surface.yaml`.

## Case Sets

`case_sets.json` contains only named lists of case ids, such as `default`,
`smoke`, `large_gate`, and `place_batch`. It is not a second source of case
facts. Scripts must resolve every case id back through this directory's
`problem.yaml` files.
