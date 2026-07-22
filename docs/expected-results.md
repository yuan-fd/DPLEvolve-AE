# Expected fresh results and interpretation

The values under `artifacts/*/expected/` are paper transcriptions used as
reference targets. Fresh reproduction commands calculate their outputs from
new EDA runs; they do not copy those reference files into the result.

## Table 4

After all nine default, BO, HPWL-selected, and runtime-aware runs complete,
`make summarize-table4` writes a ten-row TSV (nine cases plus mean) under
`$DPL_EVOLVE_STATE_ROOT/paper_reproduction/table4/`.

The paper's mean results relative to default are:

| Method | Mean final HPWL change | Mean runtime ratio |
|---|---:|---:|
| BO-DSE | -0.38% | 1.17x |
| ReviewDSE-HPWL | -1.78% | 1.34x |
| ReviewDSE-GHR | -1.68% | 1.11x |

The selected-source runner requires the ODB and companion SDC, a successful build, `PASS` evaluator
status, complete `H_g/H_lg/H_ip/H_f`, and strict legality. Input ODB hashes are
recorded for each regenerated run. AES Nangate45 has a paper-time input hash;
the other eight currently have revision provenance but no paper-time checksum.

Small floating-point/runtime differences can occur across compilers and hosts.
A mismatch should be reported with the new metrics, input hash, tool revision,
and host configuration; the reference file must not be edited to make it pass.

## Table 5

The fresh output contains three rows. A reproduced counterexample has both:

- selected `H_lg` lower than the reference `H_lg`;
- selected final `H_f` higher than the reference `H_f`.

The paper reports final-HPWL degradations of +1.48%, +20.96%, and +0.02% for
AES dense N45, JPEG dense N45, and SWERV dense N45, respectively. Missing exact
inputs are reported as `BLOCKED`; an archive-audit pass is not a replacement.

## Table 6

The fresh output contains 27 rows: three programs on each of nine patterns.
The expected status counts are:

| Program | Pass | Fail | Timeout |
|---|---:|---:|---:|
| Diamond | 1 | 8 | 0 |
| Negotiation | 1 | 4 | 4 |
| ReviewDSE repair | 9 | 0 | 0 |

`pass` means both OpenROAD and `check_placement` pass. `timeout` means the
detailed-placement flow reaches the 7200-second cap. Table 6 focuses on legality
recovery; BPQUAD has no legal fixed-source reference for a QoR comparison.

## Search process

A small Level 2 run should create a private Student source tree, a private
OpenROAD binary, protected evaluator metrics, a Student knowledge card, and a
Teacher review. The full profile uses one GPT-5.5 xhigh Teacher, four GPT-5.4
xhigh Students, ten iterations per target, and the 2x runtime gate.

Search is stochastic. Reproduction means executing the disclosed method and
reporting its newly observed trajectory, not requiring an identical sequence of
LLM proposals. Exact reconstruction of the author process additionally depends
on the unrecovered author-time Level 1 breadth and frozen evidence.

## Diagnostic-only AES run

`make toolchain-smoke` expects 14,676 instances and approximately
`188569.2 -> 176845.1` micron HPWL with strict legality on its pinned case. This
validates one toolchain path only and is not evidence for Tables 4-6.
