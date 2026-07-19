# Token cost and artifact-evaluation scope

## What the archived logs support

The paper reports 2.15B logged and 0.10B active tokens per design. The copied
server backup contains `codex_usage_summary.json` files, but those files are
**cumulative snapshots of persistent Codex sessions**. Iteration 10 repeats the
tokens already recorded at iterations 1-9. Summing every file therefore
double-counts usage and must not be presented as an API total.

For the archived 9-case, 4-student, 15-iteration alternate campaign, the two
possible readings differ substantially:

| Accounting view | Mean logged/design | Mean active/design | Meaning |
|---|---:|---:|---|
| Sum every operation snapshot | 3.296B | 0.157B | Repeated cumulative snapshots; not a true total |
| Keep final snapshot per session | 0.432B | 0.019B | Removes repetition, but may omit restarted/failed sessions |

Neither view reproduces 2.15B/0.10B, and this is not the paper's 10-iteration
campaign. We also inspected the two 10-iteration campaign families that supply
the Table 4 HPWL selections. Their per-case values do not consistently match
the paper token column under either accounting view. Token cost therefore
remains a reported paper claim, not an independently verified artifact claim.

The AE archive needs the exact author-side aggregation script or a flat
operation-local ledger for the paper campaign. A valid ledger should state
whether retries are included and must not add cumulative session snapshots.

The aggregation can be repeated on the copied backup with:

```bash
python3 scripts/maintenance/summarize_token_usage.py /path/to/dpl_evolve_state_backup \
  --output token_usage_by_design.csv
```

The output deliberately includes both `snapshot_*` and `session_*` columns so
the ambiguity is visible rather than hidden in one total.

## Cost must not be inferred from token count alone

The previous version of this document claimed that 0.10B active tokens cost
`$1-3/design`. That estimate was not backed by an invoice, model price, or raw
usage manifest and has been removed. A reproducible cost statement needs:

1. the exact model used by each Teacher and Student operation;
2. input, cached-input, and output token counts from the same paper run;
3. the provider price and date, including the cached-input rate; and
4. the formula and any subscription credits or negotiated discounts.

Without those fields, the artifact should report tokens and wall time only.

## What the token requirement blocks

The cost is not merely an inconvenience for reviewers. It blocks independent
validation of several scientific claims:

- **Main effect size:** a reviewer cannot cheaply test whether the reported
  1.78% mean improvement is typical or a selected run.
- **Variance:** multi-seed confidence intervals multiply an already large
  experiment budget; the current launcher has no deterministic seed input.
- **Ablations:** Level-1 and Teacher/Student necessity require several full
  counterfactual searches, but those launchers are not implemented.
- **Model drift:** the launcher depends on a remotely served model name. A later
  reviewer may receive a different model or lose access to the recorded one.
- **Failure recovery:** the archived rerun contains failed operations; retries
  and partial runs affect both cost and selection bias.
- **Reusable badge:** users cannot evaluate or extend the method with ordinary
  academic compute and a bounded API budget.

## Recommended tiered evaluation

The artifact should separate four levels instead of treating a full search as
the only validation path:

| Tier | Purpose | LLM calls |
|---|---|---:|
| T0 | Replay archived metrics, patches, prompts, and usage summaries | 0 |
| T1 | Rebuild and evaluate the paper's selected source patches on 9 cases | 0 |
| T2 | One-design, 1-iteration, 1-student end-to-end agent smoke | Small |
| T3 | Full paper search: 9 cases, 4 Students, 10 review iterations | Very large |

T1 is the most important missing tier: it validates that the discovered source
mechanisms really produce the reported placement results without paying the
search cost again. T2 validates orchestration. T3 should remain optional and
must have a preflight that prints the maximum operation/token budget and asks
for explicit confirmation.

## Camera-ready requirements

Before using cost or caching as a rebuttal, export the exact paper-run evidence:

- per-operation model, elapsed time, return code, and token fields;
- per-design totals and mean/standard deviation;
- a documented cost formula using dated provider prices;
- counts of failed/retried operations; and
- results for a reduced-budget search to show the quality/cost frontier.

Until then, describe 2.15B/0.10B as a reported search budget, not as a verified
usage total, billable total, or inexpensive cost.
