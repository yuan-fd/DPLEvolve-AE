# Figures 4 and 5

This directory is the reviewer entry point for regenerating the paper plots.
The default mode redraws the retained author-run data. Fresh mode consumes a
completed Table 4 BO campaign and a completed ReviewDSE search with the same
run prefix.

```bash
bash artifacts/04-figures/reproduce.sh
bash artifacts/04-figures/reproduce.sh --fresh --run-prefix review_run_01
```

The retained Figure 4 source contains 96 observed points. The three missing
points are written to `figure4-missing-points.json`; no value is imputed.
Generated SVG/TSV files are written under
`$DPL_EVOLVE_STATE_ROOT/paper_reproduction/figures/`.

- `inputs/`: location and interpretation of the plot data.
- `expected/`: acceptance rules, not substitute observations.
- `output/`: directory contract for generated products.
