# Selected ReviewDSE programs

This component stores the nine HPWL-selected and nine GHR-selected source trees
used by the Table 4 bundle.

To check source integrity without running the Table 4 arithmetic:

```bash
bash artifacts/01-table4-qor/selected-programs/run.sh --sources-only
```

The verifier computes a deterministic tree digest from relative file paths and
file contents, then compares it with `manifest.json`. This is an integrity
check, not a compilation or numerical replay.

An optional one-case replay command is available when the exact paper input is
installed in the sibling ORFS workspace:

```bash
bash artifacts/01-table4-qor/selected-programs/run.sh \
  --case aes_nangate45 --objective hpwl --threads 8
```

The paper-time ODB inputs are currently missing, so the launcher refuses an
exact replay rather than silently substituting regenerated inputs. A dry run
checks launcher construction but is not experimental evidence.

Developer-machine prefixes in non-functional message and seed-manifest path
annotations were normalized to `<ORIGINAL_WORKSPACE>`. The normalization is
recorded in `manifest.json`; C++ source files were not changed.
