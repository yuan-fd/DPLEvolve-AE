# DREAMPlace Abacus Context Card

## Role in this project
DREAMPlace Abacus is not the target plane. It is:

1. a **row-assignment and compaction donor**
2. an **external reference** when an experiment explicitly asks for it

## Recommended use
- keep it outside the target plane
- call it through the existing bridge
- borrow bounded row/cluster assignment ideas when the active `dpl_evolve`
  framework needs a better legal capacity model

## Do not
- transplant its Python/PyTorch stack into `src/dpl_evolve/` in early phases
- use it as a reason to broaden patch surfaces in OpenROAD
- treat it as part of the default canonical baseline suite.  The default suite is
  `openroad_dpl_flow`, `openroad_dpl_negotiation`, and `evolve_default`;
  DREAMPlace/Abacus remains an external donor/reference only.
