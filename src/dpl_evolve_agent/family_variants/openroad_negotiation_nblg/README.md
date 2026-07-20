# OpenROAD Negotiation / NBLG Reference

Role: overflow repair, stuck-cell recovery, history-cost donor, and
negotiation-style final legalization stage.

This directory keeps a compact source snapshot from OpenROAD classic DPL:

- `source/src/NegotiationLegalizer.cpp`
- `source/src/NegotiationLegalizer.h`
- `source/src/NegotiationLegalizerPass.cpp`
- `source/src/Opendp.tcl`
- `mechanism_deltas/parallel_frontier.diff`

The shared OpenROAD DPL entrypoint and `Opendp` interface are intentionally not
duplicated here; read `../openroad_diamond/` for that common baseline path.

Use this reference for bounded repair ideas, not as the default quality
baseline.  Prior runs showed that negotiation can be useful as a repair
operator, but it is not automatically better than diamond search on normal
strict detailed-placement cases.

`mechanism_deltas/parallel_frontier.diff` records selected improvements from
earlier negotiation experiments: safe threaded history-cost updates, parallel
sort-key precompute, two-pass top-K candidate frontier design, and candidate
telemetry.  It is not a direct apply target for the current framework; port
only the useful mechanism into `EvolveLegalizer` or private student code when
negotiation repair work is explicitly assigned.
