# Reviewer quick start

All commands run from the repository root. For a full clean-clone simulation,
see [reviewer-walkthrough.md](reviewer-walkthrough.md).

## Open the Web Demo

```bash
bash web-demo/start.sh
```

Open `http://127.0.0.1:8080`. For a remote server, create a local SSH tunnel:

```bash
ssh -N -L 8080:127.0.0.1:8080 USER@SERVER
```

## Prepare and run one real case

```bash
make doctor
make reviewer-prepare THREADS=8
make reviewer-aes-result THREADS=8
```

This generates new OpenROAD metrics and replays one source program reported in
Table 4. It is the recommended minimum fresh review.

## Complete no-LLM campaigns

```bash
# Table 4: nine defaults, 3,600 BO trials, and two nine-program replay tracks
make reproduce-table4 THREADS=10

# Table 6: install data, then execute 27 cut-row jobs
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10

# Figures from retained author-run plotting data
make reproduce-figures FIGURE_SOURCE=retained
```

Or execute the currently available aggregate after fetching Table 6 data:

```bash
make reproduce-available-results THREADS=10
```

Table 5 is deliberately excluded because its SWERV configuration and six
source trees are missing.

## Optional model-backed method check

```bash
make plan-level1
make plan-dse-paper
make run-dse-small CASE=aes_nangate45 STUDENTS=1 ITERATIONS=1 THREADS=8
```

The plan commands are free. The small run uses real model calls. A complete
nine-case search additionally requires `ACKNOWLEDGE_LLM_COST=yes` and a very
large budget.

## Supporting checks

```bash
make test            # repository regression tests
```
