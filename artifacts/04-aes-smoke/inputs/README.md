# Smoke input

The binary ODB is intentionally regenerated rather than stored here. The
source RTL, platform files, and flow scripts come from the pinned sibling
OpenROAD-flow-scripts workspace. `run.sh --run` executes synthesis, floorplan,
global placement, and resize through stage `3_4_place_resized`, then compares
the resulting ODB SHA-256 with the value in
`../expected/ae_reproduction_lock.json`.

This reconstruction is exact for AES Nangate45 in the audited environment.
The corresponding paper-time inputs for the other eight Table 4 cases were not
retained.
