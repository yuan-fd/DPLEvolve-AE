# Model-token scope

## Reviewer boundary

No model API is needed for:

- Table 4 default and BO baselines;
- replaying the 18 selected ReviewDSE source programs;
- Table 6 cut-row experiments;
- retained Figure 4/5 reconstruction; or
- the Ariane diagnostic.

Model access is required only to generate new source proposals through Level 1
or Level 2 ReviewDSE.

## Reported paper profile

The Level 2 paper profile uses one GPT-5.5 xhigh Teacher, four GPT-5.4 xhigh
Students, ten iterations, and nine targets. The paper reports a mean of 2.15B
logged and 0.10B active tokens per target, or approximately 19.35B logged and
0.90B active tokens over nine targets.

These are reported paper figures, not a guaranteed billable total. Retained
usage files include cumulative snapshots of persistent sessions, so blindly
summing every snapshot double-counts tokens. A trustworthy monetary cost would
also require dated provider prices, cached-input rates, retries, credits, and
the exact author-side ledger.

## Evaluation tiers

| Tier | Command | Purpose | Model use |
|---|---|---|---:|
| T1 | `make reproduce-table4`, `make reproduce-table6` | fresh reported-result execution | None |
| T2 | `make run-dse-small` | bounded real Teacher/Student method check | Small |
| T3 | `make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes` | complete Level 1 + Level 2 search | Very large |

T0 is not paper reproduction. T1 is the recommended scientific review path.
T2 demonstrates that the generative method operates end-to-end. T3 is runnable
for an authorized author/reviewer but is not a reasonable mandatory AE cost.

## Safety controls

- `make plan-level1` and `make plan-dse-paper` print launches without API calls.
- Full Level 1/2 commands require explicit `ACKNOWLEDGE_LLM_COST=yes`.
- The Web Demo does not expose a one-click paid full-search action.
- API credentials are external and must never be committed.

Search is stochastic. A fresh campaign should report its newly observed
trajectory and operation ledger rather than claim the same proposal sequence or
exact token count as the author run.
