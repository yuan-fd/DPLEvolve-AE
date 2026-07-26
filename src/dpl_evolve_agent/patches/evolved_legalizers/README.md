# Evolved Legalizer Patches

This directory preserves route-specific donor patches that can be materialized
as prepared start branches:

- `negotiation`: negotiation-primary NBLG/resource-allocation-oriented line.
- `diamond`: Diamond/local-greedy-oriented line.
- `source_topk_diamond`: the validated Diamond exact-sourceTopK producer,
  cumulative hot-segment handoff, and selected-reorder consumer that produced
  a 4.882% clean Ariane133 HPWL reduction in an independent historical run and
  again in the current validation environment.

Use the stable line names and patch paths below as the portable interface for
these donor sources.

The clean OpenROAD target is the `openroad_base_commit` in
`../../metadata/anchors.json`, currently
`d14d526a6f8ce5388e2a8dc30da88a0189df2f46`.

## Lineage Table

| line | stable start kind | apply to clean OpenROAD | apply to base+framework |
| --- | --- | --- | --- |
| `negotiation` | `evolved_negotiation` | `openroad_dpl_evolve_negotiation_iter30_from_clean.patch` | `openroad_dpl_evolve_negotiation_iter30_framework_delta.patch` |
| `diamond` | `evolved_diamond` | `openroad_dpl_evolve_diamond_iter30_from_clean.patch` | `openroad_dpl_evolve_diamond_iter30_framework_delta.patch` |
| `source_topk_diamond` | `source_topk_diamond` | — | `validated_source_topk_diamond.patch` applied to the prepared `diamond` seed |

The table is the patch base contract.  If a patch is used outside that target
state, conflicts or misleading partial application are expected.

## Patch Types

- `openroad_dpl_evolve_<line>_iter30_from_clean.patch`: standalone patch for a
  clean OpenROAD checkout at the recorded anchor.  It includes the base
  `dpl_evolve` integration, the framework patch, and the selected evolved
  legalizer implementation.  Use this only when starting from the clean
  OpenROAD anchor.
- `openroad_dpl_evolve_<line>_iter30_framework_delta.patch`: compact review
  patch from the base+framework state to the selected evolved legalizer.  Apply
  it only after `openroad_dpl_evolve_base.patch` and
  `openroad_dpl_evolve_framework.patch`.  This is the patch type
  `prepare_workspace.sh` uses to materialize evolved start-kind seeds.

## Agent-Facing Meaning

These files are active donor/start-point artifacts.  The `negotiation` and
`diamond` lines are evolved legalizer starts with measured wins on some case
archetypes.  They are not universal defaults.  When a Teacher assigns a route,
it should decide whether the Student should start from:

- a clean route seed such as `diamond` or `framework`,
- the matching materialized evolved seed, such as `evolved_diamond`,
  or `evolved_negotiation`, or
- another start kind while borrowing only a specific evolved mechanism.

`evolved_negotiation` is intentionally negotiation-primary.  It may use
LEGALM-style guidance only as a seed/frontier, but it must not return success
from a full LEGALM legalization before running negotiation.  Use it for
conflict-heavy, row-scarce, or cut-row-like legalization hypotheses and verify
the route with negotiation-primary log counters.  It is a donor, not a
universal HPWL winner: validation against OpenROAD negotiation showed useful
runtime/quality tradeoffs on conflict-heavy cases, while small/local cases can
prefer the native negotiation baseline.

For normal Teacher/Student rounds, do not ask Students to manually apply these
OpenROAD-root patch files inside their private source tree.  Run
`scripts/workspace/prepare_workspace.sh` to materialize the corresponding seed sources
under `.dpl_evolve_state/seed_sources/`, then launch with
`--start-kind evolved_diamond` or `--start-kind evolved_negotiation`.

## Usage

Apply one standalone line to clean OpenROAD:

```bash
git -C /path/to/OpenROAD checkout d14d526a6f8ce5388e2a8dc30da88a0189df2f46
git -C /path/to/OpenROAD apply /path/to/dpl_evolve_agent/patches/evolved_legalizers/openroad_dpl_evolve_negotiation_iter30_from_clean.patch
```

Review or port only the evolved delta on top of the prepared framework:

```bash
git -C /path/to/OpenROAD apply /path/to/dpl_evolve_agent/patches/openroad_dpl_evolve_base.patch
git -C /path/to/OpenROAD apply /path/to/dpl_evolve_agent/patches/openroad_dpl_evolve_framework.patch
git -C /path/to/OpenROAD apply /path/to/dpl_evolve_agent/patches/evolved_legalizers/openroad_dpl_evolve_negotiation_iter30_framework_delta.patch
```

`generation_manifest.tsv` records stable start kinds and patch paths.
`validation.tsv` records `git apply --check` results for both patch types.
`negotiation_primary_validation.tsv` records the repaired negotiation donor's
three-case full-flow comparison against OpenROAD negotiation, OpenROAD Diamond,
and a direct `runLegalmFullLegalization()` ablation.  The repaired line is
useful because it demonstrates a route-pure pattern: differential guidance can
stop early as a seed/frontier producer, then negotiation remains the primary
successful legalizer.  Treat the guidance objective, stop point, and downstream
consumer as optimization variables for future Teacher/Student rounds.
