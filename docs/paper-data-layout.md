# Exact paper-data layout

The execution code is tracked in this repository. The large and currently
unrecovered paper-time ODB/source assets for Tables 5 and 6 must be installed
separately under `paper-data/` (or another directory selected with
`PAPER_DATA_ROOT`). The reproduction scripts refuse to substitute the compact
archived TSV/JSON summaries.

```text
paper-data/
├── table5/
│   ├── MANIFEST.sha256
│   ├── aes_dense_n45/
│   │   ├── input/{3_4_place_resized.odb,2_floorplan.sdc}
│   │   └── programs/{selected,reference}/dpl_evolve/CMakeLists.txt ...
│   ├── jpeg_dense_n45/...
│   └── swerv_dense_n45/...
└── table6/
    ├── MANIFEST.sha256
    ├── inputs/<case>/<cut-row-pattern>/{3_4_place_resized.odb,2_floorplan.sdc}
    └── programs/
        ├── evolved_negotiation_selected/dpl_evolve/...
        ├── evolved_negotiation_default_fail_probe8/dpl_evolve/...
        └── evolved_negotiation_bpquad_center_probe/dpl_evolve/...
```

The fixed Diamond and Negotiation source trees are generated under
`$DPL_EVOLVE_STATE_ROOT/seed_sources/` by `make bootstrap`; they are not
duplicated in `paper-data/`.

Each `MANIFEST.sha256` uses paths relative to `paper-data/`, for example
`table5/aes_dense_n45/input/3_4_place_resized.odb`. It must enumerate every
file in its Table 5 or Table 6 scope except the manifest itself. The checker
rejects missing, modified, extra unsigned, absolute, and parent-traversal paths.
This makes the external package part of the reproducibility contract rather
than an unversioned directory of similarly named files.

Check availability without launching EDA:

```bash
bash scripts/reproduce/reproduce_table5.sh --check-inputs
bash scripts/reproduce/reproduce_table6.sh --check-paper-data
```

After `make bootstrap`, `reproduce_table6.sh --check-inputs` additionally
checks the generated fixed Diamond and Negotiation seed source trees.

Until these assets are recovered and checksummed, the repository can execute
Table 4 from regenerated inputs and can audit the archived Table 5/6 records,
but must not claim a complete fresh reproduction of Tables 5 and 6.
