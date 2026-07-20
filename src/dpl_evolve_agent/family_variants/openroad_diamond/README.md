# OpenROAD Diamond DPL Reference

Role: measured OpenROAD DPL-flow baseline component and local-search donor.

This directory keeps a compact source snapshot from OpenROAD classic DPL:

- `source/src/Place.cpp`
- `source/src/Opendp.cpp`
- `source/src/PlacementDRC.cpp`
- `source/src/PlacementDRC.h`
- `source/include/dpl/Opendp.h`

Study this path when optimizing local candidate search, row/site legality,
placement DRC checks, and rip-up behavior.  It is a donor/reference and part of
the measured OpenROAD DPL-flow baseline, not the default implementation behind
`detailed_placement_evolve`.  Student variants may borrow mechanisms from it,
but must make that choice explicit and beat the separately measured baseline on
the active case.
