# Expected results

## Archived evidence

`make evidence` should finish with:

```text
[PASS] All packaged paper-evidence bundles passed
```

The headline values are:

| Bundle | Expected result |
|---|---|
| Table 4 BO-DSE | 0.3813% mean HPWL reduction |
| Table 4 ReviewDSE-HPWL | 1.7840% mean HPWL reduction, 1.3367x runtime |
| Table 4 ReviewDSE-GHR | 1.6761% mean HPWL reduction, 1.1103x runtime |
| Selected programs | 18 of 18 source-tree digests match |
| Table 5 | 3 of 3 stage-local counterexamples match |
| Table 6 | 9 of 9 archived rows match |

Exact values and comparison rules are stored with each bundle in `expected/`.
Generated JSON and CSV reports appear in the corresponding `output/`
directory and may be removed with `make clean`.

## AES smoke

A successful fresh run ends with `[OK] AES smoke test PASSED`. The reference
headline values are:

| Metric | Expected |
|---|---:|
| Instances | 14,676 |
| Global HPWL | 188,569.2 microns |
| Final HPWL | 176,845.1 microns |
| Placement legality | Clean |

The reproduction lock is authoritative for exact tolerances and file hashes.
Small runtime differences are expected across machines; metric values outside
the recorded tolerances are not silently accepted.

## Interpreting failures

- A digest mismatch means packaged evidence or a selected source tree changed.
- A paper-claim mismatch means recomputed arithmetic differs from the checked
  transcription.
- A smoke input-hash mismatch means the prepared source or synthesis path does
  not match the pinned environment.
- A legality failure is a failed smoke run even if HPWL is close.
