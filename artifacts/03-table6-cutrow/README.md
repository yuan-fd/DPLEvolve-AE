# Table 6: hard cut-row legality recovery

This experiment executes Diamond, Negotiation, and one frozen ReviewDSE repair
program on nine retained cut-row patterns.

## Fresh reproduction

```bash
make fetch-table6-data
make check-table6-data
make reproduce-table6 THREADS=10
# or fetch and run through this directory:
bash artifacts/03-table6-cutrow/reproduce.sh --fetch --threads 10
```

The complete matrix contains 27 OpenROAD jobs. Every fresh execution uses a
7200-second cap, canonical log HPWL, displacement metrics, and strict
`check_placement` legality.

Start with one row:

```bash
bash artifacts/03-table6-cutrow/reproduce.sh \
  --case ariane133_placebatch --pattern center_band_8 \
  --role reviewdse --threads 10
```

The exact DEF/Verilog/SDC inputs and evolved source are distributed in the
published Table 6 data package. Paper-time ODBs and Innovus are not required.

## Directory contents

- `inputs/fixed_routes.tsv`: fixed-source reference outcomes for comparison.
- `inputs/reviewdse.tsv`: ReviewDSE reference outcomes for comparison.
- `inputs/provenance.json`: retained-record provenance.
- `expected/table6.json`: paper qualitative pattern.
- `output/`: output-contract placeholder; fresh runs live under the state root.
