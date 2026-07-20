# OpenROAD-Native Legalizer-To-DPO Handoff

## Agent Use

- Role: DPO mechanism support. Open after Teacher or current logs identify an improve-placement/DPO bottleneck.
- Use it to design source-local consumers, counters, and repair checks; do not use it as a case-type route selector.


## Evidence Type

Source-structure guidance for implementing legalizer-to-DPO handoff inside
`dpl_evolve`.  This is not a separate algorithm and not a replacement for
stage-wise evaluation.

## Core Rule

Use OpenROAD-native in-process state:

- stable OpenDB handles: `odb::dbInst*`, `odb::dbNet*`, `odb::dbGroup*`, and
  `odb::dbRegion*`;
- compact vectors, bitsets, timestamp arrays, dense ids, or fixed-cap queues;
- DPO-side mappings rebuilt after import into current detailed-placement
  objects, cell lists, segment lists, or bitsets.

Do not store current legalized coordinates; DPO can read them from OpenDB.  Use
handoff only for guidance not recovered from import: target miss,
original/global target, residual pressure, touched-net priority, or
boundary/window tags.

`dbGroup`/`dbRegion` should be used when they already express instance/cell
groups, fence regions, macro-adjacent pressure, or region-bounded DPO priority.

## Minimal Producer/Consumer Contract

Legalization may produce bounded records for moved cells, touched nets, residual
row/segment pressure, or group/region pressure.  Improve placement must consume
those records in candidate ordering, exact scoring, acceptance, rollback, or
local window selection.

Required counters:

- records produced and capped,
- records consumed,
- candidate attempts from handoff,
- accepted/rejected transactions,
- handoff build/consume runtime.

For frontier-aware basin consumers, also track:

- frontier-ranked cells,
- frontier-selected cells,
- frontier-probed exact moves,
- frontier-accepted moves,
- non-frontier accepted moves for comparison.

## What Not To Do

- Do not persist transient DPO objects such as `Node*`, `Edge*`,
  `DetailedSeg*`, `Pixel*`, or `Group*` across the stage boundary.
- Do not make DPO parse logs, JSON, text files, or string-keyed hot maps.
- Do not add public Tcl/SWIG ABI fields for ordinary private algorithm state.
- Do not add metadata unless a real DPO consumer uses it.
