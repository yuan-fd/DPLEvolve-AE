# OpenROAD DPL Context Card

## What Codex should remember

OpenROAD DPL currently exposes **two legalizer engines at runtime**:

1. **Diamond search** (default)
2. **NegotiationLegalizer** (`-use_negotiation`)

Both are complete legalizer engines when their routes execute.  They may also
act as donors for DPO handoff or local repair, but a plan should not confuse
that secondary role with the primary legalizer role.  If a route claims Diamond
or negotiation changed the placement, require route logs and nonzero
moved/candidate/conflict counters.

Within the negotiation engine, the public docs describe a logical flow:

- Abacus pass (optional / skippable)
- Negotiation pass
- Post-optimisation

At the flow level, ORFS `detail_place.tcl` is **not a pure legalizer stage**. It may include:

- `balance_row_usage`
- `set_placement_padding`
- `detailed_placement`
- `improve_placement`
- `optimize_mirroring`
- `check_placement`
- `estimate_parasitics -placement`

Therefore:
- Legalizer-stage claims should be labeled separately from full-flow claims.
- Current repo promotion decisions use the complete configured flow, including
  detailed placement, improve placement, and mirroring.
- HPWL promotion uses `metrics.json:hpwl`, parsed from OpenROAD/DPL pin-based
  log reports.  Local or bbox HPWL proxies are diagnostics only.
- Differential or LEGALM-style target fields are not legal placements by
  themselves.  Treat them as heuristic producers unless followed by explicit
  legal assignment/repair and clean legality, or unless DPO consumes their
  frontier with nonzero counters.

## High-value surfaces

### 1. `NegotiationLegalizer.cpp`
Useful place to evolve repair mechanisms:
- cost
- search window
- negotiation schedule
- disabled or incomplete postopt
- escape / refinement logic

### 2. `Opendp.cpp`
Useful place to evolve dispatch and framework wiring:
- engine dispatch policy
- top-level setup around detailed placement
- future initialization hooks

### 3. `Opendp.tcl`
Useful place to evolve only if a C++ knob genuinely needs a Tcl surface:
- expose a carefully chosen knob
- keep Tcl <-> C++ synchronization correct

## What not to confuse

- `detailed_placement` = legalizer core entry
- `improve_placement` = DPO / different optimization subsystem
- `optimize_mirroring` = HPWL polishing after legalizer
