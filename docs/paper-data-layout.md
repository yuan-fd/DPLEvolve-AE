# Paper-data layout and recovery status

Large physical-design inputs live outside Git.  The reproduction scripts use
`paper-data/` by default; set `PAPER_DATA_ROOT` to use another location.  Every
external file is enumerated by a SHA-256 manifest, and unsigned extra files are
rejected.

## Table 6: retained and exportable

The nine paper-time `3_4_place_resized.odb` files were deleted with the larger
experiment workspace.  Table 6 does **not** need them.  The final Innovus
`cutRow` products survived in the original author workspace:

- one exact `cutrows.def` and `cutrows.v` for each of nine patterns;
- one `handoff.sdc` for each of the three designs;
- one complete evolved-negotiation `dpl_evolve` source tree used on all nine
  patterns.

The data-package contract is:

```text
paper-data/
└── table6/
    ├── MANIFEST.sha256
    ├── source-inventory.tsv
    ├── cases/
    │   ├── ariane133_placebatch/
    │   │   ├── handoff.sdc
    │   │   └── center_band_{8,9,10}/{cutrows.def.gz,cutrows.v.gz}
    │   ├── swerv_wrapper_dense2/
    │   │   ├── handoff.sdc
    │   │   └── center_band_{5p25,5p5,6}/{cutrows.def.gz,cutrows.v.gz}
    │   └── bp_quad_placebatch/
    │       ├── handoff.sdc
    │       └── center_band_{5,6,8}/{cutrows.def.gz,cutrows.v.gz}
    └── programs/evolved_negotiation/dpl_evolve/...
```

Maintainers create this package directly from the retained experiment:

```bash
bash scripts/maintenance/export_table6_data.sh \
  --source-root /path/to/original/Agenticflow \
  --output /tmp/dplevolve-paper-data \
  --archive /tmp/dplevolve-table6-paper-data.tar.gz
```

The exporter was exercised on the author workspace on 2026-07-22.  It produced
114 checksummed files, a 225 MiB unpacked data directory, and a 200,849,117-byte
archive with SHA-256
`c73f84c6008ddf578bce9c2708dbe1eff55b2a8e96dada95376369afe9008b63`.
The package was also consumed by the public Tcl replay: evolved-negotiation
completed Ariane `center_band_8`, and strict `check_placement` passed.

The published archive is:

```text
https://github.com/yuan-fd/DPLEvolve-AE/releases/download/paper-data-v1/dplevolve-table6-paper-data-20260722.tar.gz
```

The easiest verified installation is:

```bash
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10
```

For a short real-path check before the 27-run matrix:

```bash
make reproduce-table6 CASE=ariane133_placebatch PATTERN=center_band_8 ROLE=reviewdse THREADS=10
```

For a manual installation:

```bash
mkdir -p paper-data
tar -xzf dplevolve-table6-paper-data-20260722.tar.gz -C paper-data
PAPER_DATA_ROOT=$PWD/paper-data make reproduce-table6 THREADS=10
```

The script decompresses the exact DEF/Verilog, builds the frozen source, and
runs Diamond, Negotiation, and ReviewDSE for all nine patterns.  The original
cut-row construction used Innovus; reviewers do not need Innovus because its
exact outputs are the replay inputs.  The OpenROAD replay itself is fully open
source.

## Table 5: incomplete input recipe and six source commits missing

The paper-time dense ODBs were deleted. They are not distributed as if they
still existed. Two generation recipes survived, but the third did not:

| Row | Case/flow variant | Reconstruction |
|---|---|---|
| AES dense N45 | `aes_dense_nangate45`, `DENSE` | fixed AES floorplan |
| JPEG dense N45 | `jpeg_util90_nangate45`, `DENSE` | `CORE_UTILIZATION=90` |
| SWERV dense N45 | `swerv_wrapper_nangate45`, `DENSE_2` | **blocked:** untracked `config_dense2.mk` was deleted |

The retained SWERV cut-row handoff includes an ODB derived from the deleted
snapshot, but it was re-emitted after loading `3_place.sdc`; Table 5's evaluator
uses `2_floorplan.sdc`. It is useful provenance, not a validated exact
replacement. The standard tracked `swerv_wrapper/config.mk` is also not the
same file as the paper launch recorded in `run_flow.sh`, so the public script
refuses to use it silently.

The six complete candidate source trees selected for Table 5 are also absent
from both the original repository and its retained 39 GiB state backup. The
result TSVs, iteration reports, and commit identifiers survived, but those are
not executable source. The missing input recipe and commits are recorded in
`configs/reproduction/table5-inputs.tsv` and `table5-sources.tsv`.

If the source commits are recovered from another backup, install them as:

```text
paper-data/
└── table5/
    ├── MANIFEST.sha256
    ├── swerv_dense_n45/input/config_dense2.mk
    ├── aes_dense_n45/programs/{selected,reference}/dpl_evolve/...
    ├── jpeg_dense_n45/programs/{selected,reference}/dpl_evolve/...
    └── swerv_dense_n45/programs/{selected,reference}/dpl_evolve/...
```

Then `make prepare-table5-inputs` regenerates the three inputs with the
recovered config, and `make reproduce-table5` builds all six sources, executes
the complete downstream flow, and derives a fresh Table 5. Until that recovery
happens, both commands exit `BLOCKED`. Archived TSV values are never
substituted for missing source code or configuration.

## Manifest rule

Each `MANIFEST.sha256` records paths relative to `paper-data/`, beginning with
`table5/` or `table6/`.  Verify without launching EDA:

```bash
python3 scripts/reproduce/verify_data_manifest.py --root paper-data --scope table6
bash scripts/reproduce/reproduce_table6.sh --check-paper-data
bash scripts/reproduce/reproduce_table5.sh --check-paper-data
```

The checker rejects missing, modified, unsigned, absolute, parent-traversal,
and symlinked content.
