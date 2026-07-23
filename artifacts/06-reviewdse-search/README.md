# ReviewDSE Level 1 and Level 2 search

This directory exposes the actual Teacher/Student source-edit, build, evaluate,
and review process. Planning is free; live modes require model access and incur
token usage.

```bash
bash artifacts/06-reviewdse-search/reproduce.sh --plan
bash artifacts/06-reviewdse-search/reproduce.sh --small --case aes_nangate45
bash artifacts/06-reviewdse-search/reproduce.sh --level1 --acknowledge-cost
bash artifacts/06-reviewdse-search/reproduce.sh --paper --acknowledge-cost \
  --run-prefix review_run_01
```

The public Level 1 profile is a runnable reconstruction because the original
Level 1 breadth and frozen packet were not retained. A full Level 2 paper
profile uses nine cases, four Students, and ten iterations. Reviewers are not
expected to fund that full search merely to evaluate the artifact.

- `inputs/`: machine-readable protocol and source-start description.
- `expected/`: stochastic-result interpretation.
- `output/`: generated state layout.
