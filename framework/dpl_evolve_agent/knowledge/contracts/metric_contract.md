# Metric Contract For Evolution

## Agent Use

- Role: workflow or metric contract. Use to keep experiments and reporting consistent, not to choose an algorithmic route.
- Route selection still starts from the core case-type blueprint map and current metrics.


## Evidence Type

`contract`

## Canonical HPWL

Use `metrics.json:hpwl` for promotion and public reporting.  It is parsed from
OpenROAD/DPL pin-based log reports, especially `[INFO DPL-0022] HPWL after`
after the configured full flow.

Do not use `hpwl_proxy` for promotion.  It is a legacy cell-bbox proxy kept to
debug placement movement and old artifacts.  It can differ substantially from
pin-based HPWL and can reverse conclusions.

## Comparable Experiment Classes

Keep these classes separate in tables:

- `raw input`: the incoming `3_4_place_resized.odb`; useful as a reference, not
  a legalized output.
- `full OpenROAD flow`: `detailed_placement`, `improve_placement`,
  `optimize_mirroring`.
- `full evolve flow`: `detailed_placement_evolve`,
  `improve_placement_evolve`, `optimize_mirroring_evolve`.
- `external handoff`: import an external DEF/ODB and optionally run a limited
  OpenROAD pass such as `optimize_mirroring`; useful for diagnosis, not a
  direct replacement for a full legalizer flow.

## Reporting Requirements

Every result table should include:

- HPWL after, from `metrics.json:hpwl.after_micron`,
- HPWL source, usually `openroad_dpl_log`,
- legality status,
- average displacement,
- maximum displacement,
- runtime when the run script records it,
- flow class.

Teacher and Student agents should use legalizer-stage telemetry to explain a
mechanism, but final decisions must use the complete strict flow metric.
