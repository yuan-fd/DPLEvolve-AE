# LEGALM-Guidance Source Reference

This directory preserves a measured `detailed_placement_evolve`
LEGALM-style guidance implementation as a source-code reference for student
agents.

It is a reference donor, not a separate active family or alternate entrypoint.
The evolved command remains `detailed_placement_evolve`; future agents should
study this code, then port or replace bounded mechanisms in the live
`tools/OpenROAD/src/dpl_evolve/src` implementation.

## Contents

- `source/LegalmGuidance.cpp`: Stage 1 relaxed/static projection plus Stage 2
  ALM/BGD guidance, triplefold partitioning, and local-optimum escape.
- `source/LegalmRowAssignment.cpp`: bounded row assignment for guided targets.
- `source/LegalmFullLegalization.cpp`: self-contained LEGALM-style placement
  and Stage 3 no-overflow refinement.
- `source/EvolveNegotiationRepair.cpp`: internal repair utility code kept as a
  source reference; the default command does not expose a separate negotiation
  flag.
- `source/StudentAlgorithm.cpp`: top-level stage composition.
- `source/Optdp.cpp`: bounded frontier consumer inside improve placement.
- `source/CMakeLists.txt`: active dpl_evolve source list required by the
  relink build.
- `source/Opendp.h`: top-level DPL-Evolve state and method declarations.
- `source/Opendp.cpp`: command entrypoint hook into the student algorithm.
- `source/PlacementDRC.cpp` and `source/PlacementDRC.h`: candidate-local DRC
  support used by the LEGALM technology penalty path.
- `source/DifferentialGuidance.cpp`: guided initial-location storage helpers.
- `source/LegalmCommon.h`: shared LEGALM paper parameters, CPU caps, footprint
  structures, and the common bounded BGD candidate driver used by Stage 2 and
  Stage 3.
- `source/EvolveLegalizer.cpp`: top-level evolve legalizer orchestration.
- `source/EvolveLegalizer.h`: public evolve legalizer interface.
- `source/EvolveContext.h`: shared context wrapper.
- `source/EvolveTelemetry.h`: lightweight stage telemetry structures.

## How Students Should Use This

- Treat this as a concrete implementation baseline for Stage A guidance,
  ALM/BGD-style overflow reduction, Stage3 no-overflow refinement, and per-stage
  telemetry.
- Do not assume every constant or stage choice is final. The algorithmic flow
  is intentionally evolvable.
- Compare any new idea against the strict OpenROAD DPL flow and active evolve
  metrics for the active case.
- If a new implementation diverges, explain which mechanism was preserved,
  replaced, or removed.

## Known Calibration

This snapshot is useful because it has compiled and run on large strict-flow
validation. It is not proof of optimality.

Recent evidence shows the LEGALM pipeline can be near one-second scale on a
medium-density large standard-cell case after incremental occupancy updates and
partition-scoped demand scoring.  Full strict-flow runtime can still be
dominated by downstream detailed improvement and mirroring, so future agents
should report legalizer-stage timing separately from full-flow timing.

Students should therefore view this as a strong implementation reference, not
as a locked design.
