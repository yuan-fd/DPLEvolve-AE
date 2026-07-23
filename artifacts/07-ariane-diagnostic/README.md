# Ariane warm-start diagnostic

This diagnostic rebuilds and executes six retained source trees: four runs
that missed the intended handoff mechanism and two Level-1-guided runs. It is
supporting diagnostic evidence, not a controlled paper ablation.

```bash
bash artifacts/07-ariane-diagnostic/reproduce.sh --threads 10
```

The runner generates a fresh Ariane input with the pinned flow, executes all
six complete programs, and derives the two group means from new metrics.

- `inputs/`: source location and input reconstruction notes.
- `expected/`: numerical acceptance window.
- `output/`: generated product location.
