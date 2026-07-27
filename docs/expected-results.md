# Fresh outputs and acceptance

Expected files are comparison targets. Fresh commands produce new metrics and
summaries from EDA execution and never copy an expected value into an observed
field.

## Protected evaluator

A complete fresh candidate records:

- incoming global-placement HPWL, `H_g`;
- post-legalization HPWL, `H_lg`;
- post-DPO HPWL, `H_ip`;
- final post-DPL HPWL, `H_f`;
- strict placement legality;
- average and maximum displacement;
- runtime, process status, and mechanism-liveness signals.

ReviewDSE eligibility additionally requires source/build/evaluation consistency
and an exact `runtime_ratio <= 2.0` gate.

## Table 4

`make summarize-table4` writes nine case rows plus a mean row to:

```text
$DPL_EVOLVE_STATE_ROOT/paper_reproduction/table4/table4-fresh.tsv
```

Paper means relative to default are:

| Method | Mean final HPWL change | Mean runtime ratio |
|---|---:|---:|
| BO-DSE | -0.38% | 1.17x |
| ReviewDSE-HPWL | -1.78% | 1.34x |
| ReviewDSE-GHR | -1.68% | 1.11x |

Default acceptance windows are 0.06 percentage point for each reported HPWL
delta and 0.20 for runtime ratio. AES Nangate45 also checks rebuilt absolute
HPWL within 0.5%. These windows judge numerical reproduction across hosts and
builds; they are not claims of a bit-identical linked binary.

Missing paper-time hashes for other inputs do not block execution. A run must
still be complete, legal, and within the relative-result tolerance.

## Figures 4 and 5

Retained Figure 4 produces 96 observed rows. Missing are SWERV ASAP7 iterations
9/10 and SWERV Nangate45 iteration 10. The renderer writes these gaps to
`figure4-missing-points.json` without imputation. A complete fresh campaign may
produce all 99 case/iteration points.

Figure 5 requires 400 BO points for AES Nangate45 and 400 for Ariane133
Nangate45, plus available ReviewDSE candidates. Its horizontal coordinate is
same-case runtime ratio, and Pareto membership is recomputed.

## Table 5

`make reproduce-table5` first verifies the checksummed LEGALM, Diamond, and
Negotiation snapshots, regenerates only the three Table 5 inputs, and then
builds and executes the six selected/reference roles. The local
`CORE_UTILIZATION` values are 70, 90, and 60 for AES, JPEG, and SWERV; global
ORFS configurations and Table 4 inputs are unchanged.

The selected/reference mappings are LEGALM/Diamond for AES,
Negotiation/Negotiation for JPEG, and Diamond/Negotiation for SWERV. Each fresh
comparison must satisfy `delta_H_lg < 0` and `delta_H_f > 0`: the selected
legalizer improves post-legalization HPWL but worsens final HPWL after the
complete flow. The paper reports `H_lg`/`H_f` changes of -0.76%/+1.48%,
-14.96%/+20.96%, and -0.12%/+0.02%, respectively. Fresh stage metrics are
written to `table5_*/table5-fresh.tsv`; retained values are comparison targets
only and are never substituted for observations.

## Table 6

The complete output has 27 rows: three programs on nine patterns. The retained
qualitative pattern is:

| Program | Pass | Fail | Timeout |
|---|---:|---:|---:|
| Diamond | 1 | 8 | 0 |
| Negotiation | 1 | 4 | 4 |
| ReviewDSE repair | 9 | 0 | 0 |

`pass` requires both OpenROAD success and clean `check_placement`. Every fresh
execution uses a 7200-second cap. The retained Ariane center-10 Negotiation row
is an earlier 600-second timeout and remains historical provenance; fresh
output records the new outcome under the public 7200-second protocol.

For legal fixed-source cases, the summary also reports HPWL comparison and
runtime speedup. BPQUAD has no legal fixed-source QoR reference.

## Search process

A small ReviewDSE run should create a private Student source tree, private
OpenROAD binary, protected metrics, knowledge card, Teacher review, and
provenance records. The paper Level 2 profile uses one GPT-5.5 xhigh Teacher,
four GPT-5.4 xhigh Students, ten iterations per target, and the 2x runtime gate.

Search is stochastic. Reproduction means executing the disclosed process and
reporting the new trajectory; it does not require identical LLM proposals. The
public Level 1 profile is a reconstruction because the author-time breadth and
packet were not retained.

## Supporting diagnostic

The AES toolchain smoke validates one default path only. It is useful for
debugging installation, but it is not evidence for Tables 4–6.
