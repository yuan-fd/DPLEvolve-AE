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

After `make prepare-paper-inputs`, replay one selected source on a regenerated
paper input with:

```bash
bash artifacts/01-table4-qor/selected-programs/run.sh \
  --case aes_nangate45 --objective hpwl --threads 8
```

The launcher verifies the selected source digest, requires the requested ODB,
builds a private OpenROAD variant, runs the full protected trajectory, and
compares the new HPWL with the archived selected result. AES Nangate45 also
checks the retained paper-time ODB digest and the machine-readable rebuilt-HPWL
tolerance in `manifest.json`. The author-time linked winner binaries and full
compiler fingerprints were not retained, so this is numerical reproduction,
not bit-for-bit binary replay. The other eight ODBs are generated from pinned
revisions but lack retained paper-time digests; their absolute HPWL drift is
reported as diagnostic information rather than an identity proof.

The stable all-case interface is:

```bash
make replay-reviewdse TRACK=hpwl
make replay-reviewdse TRACK=ghr
```

Developer-machine prefixes in non-functional message and seed-manifest path
annotations were normalized to `<ORIGINAL_WORKSPACE>`. The normalization is
recorded in `manifest.json`; C++ source files were not changed.
