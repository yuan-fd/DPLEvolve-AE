# Artifact appendix

This artifact accompanies the MLCAD 2026 paper *From Tool Invocation to
Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA*.

## Artifact summary

The archive provides four independent reviewer bundles: archived Table 4 QoR
records and selected programs, compact Table 5 composability evidence, compact
Table 6 cut-row evidence, and a fresh one-case AES Nangate45 OpenROAD flow.

| Property | Value |
|---|---|
| Platforms | Linux x86-64 |
| Languages | Bash, Python, C++, Tcl |
| Framework | OpenROAD-flow-scripts |
| Hardware | CPU only; 4+ cores and 16 GB RAM recommended for smoke |
| Evidence-only runtime | Seconds |
| Fresh smoke runtime | About 2-5 minutes after setup |
| License | BSD 3-Clause |

## Access and installation

Obtain the GitHub release or Zenodo archive, then run:

```bash
cd DPLEvolve-AE
make evidence
```

This path uses packaged records and Python's standard library. For the optional
fresh EDA run:

```bash
make bootstrap
make setup
make smoke
```

Exact source revisions are recorded in `provenance/source-commits.json`.

## Evaluation

The main reviewer command verifies the three paper-facing table bundles and
the integrity of 18 selected source programs. Expected headline values and
success messages are listed in `docs/expected-results.md`; the evidence level
for every item is listed in `docs/claims-to-artifacts.md`.

The AES flow checks its generated input digest, instance count and area, HPWL,
tool status, and strict placement legality. It is a fresh default OpenROAD
execution, not a selected ReviewDSE-program replay.

## Limitations

The nine paper-time ODB inputs were not retained because they occupied several
terabytes, so exact numerical replay of the selected programs is unavailable.
The archive also does not repeat the full Teacher/Student discovery search.
That search needs authenticated model access, the original search state, many
persistent sessions, and a very large token and EDA budget.

These boundaries are enforced in the public commands and described in every
bundle README.
