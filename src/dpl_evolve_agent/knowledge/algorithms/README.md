# Algorithms

This directory stores reusable algorithm knowledge for detailed-placement
evolution.  It is organized by stage so Teacher and Student can quickly choose
the right mechanism without reading a long mixed note.

## Directory Map

- `legalization/`: legalizer mechanisms and path-pure legal-resource solvers.
- `dpo_refinement/`: improve-placement, post-legalization refinement, and
  legality-preserving move search.
- `context/`: background cards that explain existing engines or external
  references.  These are not direct implementation recipes.
- `legalization_paper_pseudocode.md`: compatibility index for legalization
  cards.
- `dpo_paper_pseudocode.md`: compatibility index for DPO/refinement cards.

## Card Contract

Each mechanism card should state:

- source status: `checked-source-text`, `source-handle-only`,
  `derived-from-openroad/context`, or `implementation-hypothesis`;
- source handles: IDs from `knowledge/reference/papers/paper_sources.yaml`;
- stage boundary: legalizer, handoff, improve placement, or final polish;
- best path: DIAMOND, LEGALM, NEGOTIATION, or stage-agnostic use;
- pseudocode: enough structure to implement the mechanism;
- liveness logs: counters that prove the mechanism ran;
- failure patterns: when the mechanism is likely to hurt HPWL, legality, or
  runtime.

## Hygiene Rules

- Do not store local filesystem paths, experiment directories, commit hashes,
  benchmark-specific result rows, or dated run summaries in algorithm cards.
- Translate measurements into design-feature guidance, for example dense-row
  pressure, fragmented rows, macro-edge stress, or DPO recoverability.
- Do not put pure global placement or ML placement-from-scratch algorithms here.
  Cards must map to legalization, handoff, improve placement, or bounded
  detailed-placement refinement.
- A mechanism can inspire another path, but it must not be used as a hidden
  cross-path fallback.
