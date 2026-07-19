# BO Search Spaces

These YAML files define black-box parameter spaces for
`scripts/bo/bo_tune_case.py`.

The BO runner requires Ray Tune and does not edit C++/Tcl source.  It maps
parameter values to Tcl command arguments and injects them through the existing
strict baseline harness.  Each space declares its own `env_prefix`: OpenROAD
native DPL spaces use `OPENROAD_DPL_*`, and evolve-specific spaces use
`DPL_EVOLVE_*`.

Supported parameter fields:

- `name`: stable ledger column name.
- `stage`: one of `detail`, `improve`, `improve_global_swap`, `optimize`, or
  `meta`.
- `flag`: Tcl flag to emit.  Use an empty string for `meta` switches.
- `type`: `int`, `float`, `flag`, or `categorical`.
- `min` / `max`: numeric range for `int` and `float`.
- `choices`: value list for `categorical`.
- `default`: value used by trial 0.
- `active_if`: optional map from another parameter to allowed values; inactive
  parameters are kept in the ledger but not emitted into Tcl args.

Use `omit` as a categorical value when a default OpenROAD behavior should be
preserved by not emitting a flag at all.

Active spaces:

- `openroad_dpl_native.yaml`: OpenROAD native `detailed_placement`,
  `improve_placement`, and `optimize_mirroring` public Tcl knobs only. It uses
  `OPENROAD_DPL_*` injection and includes Diamond, negotiation, negotiation
  with Abacus, and source-default-centered global-swap ranges.
- `evolve_default_dpo_small.yaml`: evolve-default LEGALM/DPO public Tcl knobs.
