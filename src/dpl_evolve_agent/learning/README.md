# Learning

This directory contains optional utilities that convert local run records into
compact observations, knowledge-card candidates, and token ROI reports.

It is not the default knowledge source for Teacher/Student routing.  Runtime
learning outputs are generated under `DPL_EVOLVE_STATE_ROOT`; human-reviewed
knowledge that should guide future agents belongs under the tracked
`knowledge/` tree.

Current utilities:

- `log2knowledge.py`: convert selected RunDB records into observation/card
  candidates.
- `report_token_roi.py`: summarize token cost and evidence return from RunDB.

Use these after a run when curating evidence.  Do not let automated learning
rewrite tracked knowledge without review.
