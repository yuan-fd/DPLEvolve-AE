# External paper data

Large physical-design inputs are stored outside Git. The default location is
`paper-data/`; set `PAPER_DATA_ROOT` to use another filesystem. Every published
file is covered by a SHA-256 manifest so corrupted or incomplete downloads are
rejected before EDA execution.

Hashes are used here to validate the distributed package. They are not a
requirement that every regenerated Table 4 ODB be bit-identical to an
author-time file.

## Table 6: available

The original Table 6 ODB workspaces were deleted, but the experiment does not
need them. The final cut-row handoff products survived:

- nine exact `cutrows.def` and `cutrows.v` pairs;
- one `handoff.sdc` per design; and
- one complete evolved-negotiation `dpl_evolve` source tree.

Install and validate the published package:

```bash
make fetch-table6-data
make check-table6-data
```

Published archive:

```text
https://github.com/yuan-fd/DPLEvolve-AE/releases/download/paper-data-v1/dplevolve-table6-paper-data-20260722.tar.gz
```

Archive SHA-256:

```text
c73f84c6008ddf578bce9c2708dbe1eff55b2a8e96dada95376369afe9008b63
```

The unpacked layout is:

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

Run one row and then the complete matrix:

```bash
make reproduce-table6 CASE=ariane133_placebatch \
  PATTERN=center_band_8 ROLE=reviewdse THREADS=10
make reproduce-table6 THREADS=10
```

Reviewers do not need Innovus. The exact Innovus `cutRow` products are inputs;
all replay execution is open-source OpenROAD.

While the GitHub repository is private, release downloads may require
`gh auth login`. The fetch script tries anonymous `curl` first and then the
authenticated GitHub CLI. A public repository needs no authentication.

## Table 5: unavailable recovery assets

The Table 5 dense ODBs were deleted. Two generation recipes survived:

| Row | Flow variant | Input status |
|---|---|---|
| AES dense Nangate45 | `DENSE` | Regenerable |
| JPEG UTIL=90 Nangate45 | `DENSE` | Regenerable |
| SWERV dense Nangate45 | `DENSE_2` | **Blocked:** `config_dense2.mk` missing |

The six complete selected/reference source trees are also missing. Retained
result TSVs and source identities are not executable source code.

The SWERV cut-row handoff ODB is not substituted: it was re-emitted after
loading a different SDC stage from the Table 5 evaluator. The standard tracked
SWERV config is also not silently substituted for the deleted DENSE_2 config.

If another backup is found, install it as:

```text
paper-data/
└── table5/
    ├── MANIFEST.sha256
    ├── swerv_dense_n45/input/config_dense2.mk
    ├── aes_dense_n45/programs/{selected,reference}/dpl_evolve/...
    ├── jpeg_dense_n45/programs/{selected,reference}/dpl_evolve/...
    └── swerv_dense_n45/programs/{selected,reference}/dpl_evolve/...
```

Then the existing `make prepare-table5-inputs` and `make reproduce-table5`
commands become executable without changing code. Until then they return
`BLOCKED` before EDA work.

## Manual Table 6 installation

```bash
mkdir -p paper-data
tar -xzf dplevolve-table6-paper-data-20260722.tar.gz -C paper-data
PAPER_DATA_ROOT=$PWD/paper-data make check-table6-data
PAPER_DATA_ROOT=$PWD/paper-data make reproduce-table6 THREADS=10
```

The manifest checker rejects missing, modified, unsigned, absolute,
parent-traversal, and symlinked content.
