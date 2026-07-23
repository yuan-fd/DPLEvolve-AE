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

The eight cases without a retained paper-time ODB hash use regenerated pinned
inputs. Their fresh runs must be complete and legal, and their relative HPWL
and runtime results must fall within the documented scientific tolerances;
they are not advertised as bit-for-bit replays.

## Rebuild paper figures and Ariane diagnostic

```bash
make reproduce-figures FIGURE_SOURCE=retained
make check-ariane-diagnostic-sources
make reproduce-ariane-diagnostic THREADS=10
```

The first command redraws checksummed author-run logs. After a full fresh DSE
run, use the same stable prefix for launch and rendering:

```bash
make run-dse-paper ACKNOWLEDGE_LLM_COST=yes DSE_RUN_PREFIX=review_run_01
make reproduce-figures FIGURE_SOURCE=fresh DSE_RUN_PREFIX=review_run_01
```

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

## Table 5/6 data

Table 6 uses a separately published, checksummed 200 MB archive containing
retained cut-row DEF/V/SDC inputs and one evolved source:

```bash
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10
```

Table 5's ODBs were deleted. AES/JPEG generation recipes survive, but the
untracked SWERV `config_dense2.mk` and all six exact source commits are
currently missing. `make prepare-table5-inputs` and `make reproduce-table5`
therefore report `BLOCKED` until another backup is found. See
[paper-data-layout.md](paper-data-layout.md); no standard config or archived
TSV is substituted.

`make paper-data-check` reports both statuses together and therefore exits
nonzero while the Table 5 config/source recovery remains incomplete.

If the Table 5 config and sources are later recovered, the complete no-LLM result path is:

```bash
make reproduce-paper-results THREADS=10
```

It runs fresh Table 4/5/6 experiments. Today Table 4 and Table 6 are runnable
independently, while Table 5 is a disclosed gap. The separate full discovery rerun is
`make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes`.

The current all-available path excludes Table 5 explicitly:

```bash
make reproduce-available-results THREADS=10
```

## Optional archive audit

```bash
make audit-archive
```

This seconds-long command checks packaged records and source-tree hashes. It
does not invoke EDA and does not count as reproducing an experiment.
