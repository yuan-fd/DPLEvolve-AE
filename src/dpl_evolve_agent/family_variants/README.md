# Family Variants

This directory is now a compact algorithm-reference shelf, not the active
family-tree router.

Default agent context should read only:

- `REFERENCE_INDEX.yaml`
- one assigned donor directory in this folder
- `../patches/PATCH_AUDIT.yaml` when deciding whether a patch-like file is a
  direct apply target or only a reference delta

Everything from the previous family-tree flow that is not part of those
references is intentionally excluded from default tracked context.  If a task
needs legacy evidence, it should name the archive explicitly; agents should not
search external or ignored backup material by default.

## Selection

- Use OpenROAD diamond as a local-search donor and as one component of the
  measured OpenROAD DPL-flow baseline. It is not the default evolved
  implementation.
- Use `openroad_negotiation_nblg/` for negotiation/NBLG
  repair ideas. Its `mechanism_deltas/parallel_frontier.diff` keeps selected
  threaded-frontier mechanisms without creating another family entry.
- Use `legalm_guidance/` as the LEGALM-style implementation
  reference for student agents.
- Use `dreamplace_abacus` as a row-assignment/compaction
  reference.
- Do not treat files under `mechanism_deltas/` as direct patches for the
  current framework.  They are compact mechanism examples to be ported.

New algorithm work should become a patch to the constrained framework behind
`detailed_placement_evolve`, not another default family directory.
