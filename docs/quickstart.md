# Reviewer quick start

All commands run from the repository root.

## Prepare a real experiment

```bash
make doctor
make bootstrap
make build-tools THREADS=16
make prepare-paper-inputs CASE=aes_nangate45 THREADS=8
make validate-evaluator CASE=aes_nangate45 THREADS=8
```

The last two commands create a placement ODB and execute the protected
detailed-placement evaluator. They are the shortest fresh EDA path connected
to the paper's measurement contract.

## Reproduce one Table 4 target

```bash
make reproduce-default CASE=aes_nangate45 THREADS=8
make setup-bo
make reproduce-bo CASE=aes_nangate45 THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS=8
make replay-reviewdse CASE=aes_nangate45 TRACK=ghr THREADS=8
```

Remove `CASE=...` or run `make reproduce-table4` for all nine targets. BO then
executes 3,600 trials, so plan capacity before starting it.

## Exercise the actual search loop

```bash
make plan-level1
make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes LEVEL1_CHILDREN=50
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1
make plan-dse-paper
```

The small run uses real model calls and source edits. `plan-dse-paper` prints
the full nine-case protocol without spending tokens. A complete search requires
`ACKNOWLEDGE_LLM_COST=yes`; see the root README for reported token budgets.

## Table 5/6 inputs

```bash
make paper-data-check
```

Fresh Table 5/6 replay is allowed only after the checksummed exact ODB/SDC pairs and source programs
in [paper-data-layout.md](paper-data-layout.md) are installed. Missing data is
reported as `BLOCKED` and is never replaced by the archived summaries.

With all exact data installed, the complete no-LLM result path is:

```bash
make reproduce-paper-results THREADS=10
```

It runs fresh Table 4/5/6 experiments. The separate full discovery rerun is
`make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes`.

## Optional archive audit

```bash
make audit-archive
```

This seconds-long command checks packaged records and source-tree hashes. It
does not invoke EDA and does not count as reproducing an experiment.
