# Model-token scope

## Reviewer boundary

Teacher and Student Agent configuration is mandatory for the artifact. The
following retained-program and fixed-result validations may complete without
issuing new model requests:

- Table 4 default and BO baselines;
- replaying the 18 selected ReviewDSE source programs;
- Table 6 cut-row experiments;
- retained Figure 4/5 reconstruction; or
- the Ariane diagnostic.

They are validation stages, not a model-free alternative to ReviewDSE. Model
access is exercised by `make check-demo-models` and is required to generate new
source proposals through Level 1 or Level 2.

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
| T1 | `make reproduce-table4`, `make reproduce-table6` | fixed-result validation with configured Agents | Requests not normally issued |
| T2 | `make run-dse-small` | bounded real Teacher/Student method check | Small |
| T3 | `make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes` | complete Level 1 + Level 2 search | Very large |

T0 is not paper reproduction. T1 validates the reported numerical results, but
does not replace the model-backed method check. T2 demonstrates that the
generative method operates end-to-end. T3 is runnable for an authorized
author/reviewer but is not a reasonable mandatory AE cost.

## Safety controls

- `make plan-level1` and `make plan-dse-paper` inspect configured launches
  without dispatching them; they do not define an alternative method path.
- Full Level 1/2 commands require explicit `ACKNOWLEDGE_LLM_COST=yes`.
- The Web Demo does not expose a one-click paid full-search action.
- API credentials are external and must never be committed.

Search is stochastic. A fresh campaign should report its newly observed
trajectory and operation ledger rather than claim the same proposal sequence or
exact token count as the author run.
