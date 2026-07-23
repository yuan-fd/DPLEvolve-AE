# Table 5 Source Status

The Table 5 reproduction runner is implemented, but the exact experiment
cannot currently start because the following paper-time assets were not
retained:

- the untracked SWERV Nangate45 `config_dense2.mk` configuration;
- three selected legalizer source trees; and
- three matching reference source trees.

AES and JPEG dense inputs can be regenerated. They are insufficient for the
complete three-row table because the SWERV configuration and all six source
trees are part of the experiment contract.

```bash
make check-table5-data
make reproduce-table5 THREADS=10
```

Until those assets are recovered, both commands report `BLOCKED`. The runner
does not replace the missing configuration with a standard SWERV setup and
does not use retained Table 5 numbers as fresh results.

The required rows and source identifiers are recorded in:

```text
configs/reproduction/table5-inputs.tsv
configs/reproduction/table5-sources.tsv
```
