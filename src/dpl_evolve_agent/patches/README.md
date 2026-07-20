# Prepare Patches

This directory contains the prepare-time patches used to bootstrap a clean
workspace.

Patch status is tracked in:

- `PATCH_AUDIT.yaml`

- `openroad_dpl_evolve_base.patch`
  Applies the clean `dpl_evolve` base, the minimal `ord` hooks needed to
  register the evolve command surface (`detailed_placement_evolve`,
  `improve_placement_evolve`, and `optimize_mirroring_evolve`), and the
  `DPL_EVOLVE_SRC_DIR` build hook to a clean OpenROAD checkout. It
  intentionally does not install a target-side `src/dpl_evolve/README.md`;
  use this control repo's README,
  `AGENTS.md`, and `knowledge/` as the documentation source of truth. During
  `prepare_workspace.sh`, this state is snapshotted as the `diamond`
  start-kind seed before the framework patch is applied.
- `openroad_dpl_evolve_framework.patch`
  Applies the constrained top-level `dpl_evolve` framework on top of the clean
  base. The primary evolved placement command remains `detailed_placement_evolve`; the patch
  introduces framework telemetry, the student-owned algorithm hook, a
  LEGALM-style producer/frontier path, bounded DPO handoff support, and
  Abacus-enabled negotiation repair as a downstream repair stage. Child agents implement
  case-specific behavior by modifying this private `dpl_evolve` source after
  the orchestrator has collected baseline evidence. During
  `prepare_workspace.sh`, this state is snapshotted as the `framework`
  start-kind seed.
- `orfs_make_overlay.patch`
  Applies the `flow/Makefile` runner hook needed by the control plane so the
  strict baseline wrappers can pass `RUN_OPENROAD_ARGS` and `RUN_SCRIPT_ARGS`
  through the shared ORFS `run` target.
- `evolved_legalizers/`
  Contains route-specific evolved legalizer donor variants.  The
  `*_from_clean.patch` files apply directly to the recorded
  clean OpenROAD anchor and include the base, framework, and one evolved
  legalizer line.  The `*_framework_delta.patch` files apply after
  `openroad_dpl_evolve_base.patch` and `openroad_dpl_evolve_framework.patch`
  and are intended for review or porting.  These patches are also active
  Teacher/Student donor evidence: `prepare_workspace.sh` materializes
  `evolved_diamond` and `evolved_negotiation` seed sources from the framework
  deltas when the patch files are present.
  `evolved_negotiation` is negotiation-primary by contract.  It may use
  differential guidance as a seed/frontier, but the successful primary
  legalization route must execute negotiation/resource allocation.

The exact supported base commits are recorded in:

- `metadata/anchors.json`
