## Calibration Output Format

Write only:

```text
Calibration objective: single-iteration mechanism validation sweep; children=$children; no continuation routing.
Design read: ...
Branch coverage plan: <how framework, diamond, and default_negotiation are distributed; name any excluded start and why>
Stage coverage plan: <legalizer producer, handoff/frontier, DPO candidate/scoring/acceptance, transaction/reorder/local DP, post-consumer preservation, runtime-budget substitution>
Mechanism pool: <compact list of mechanism families being tested; state why they are distinct>
Mirror policy: <normally "mirror is not a primary route"; list any rare mirror-preservation assignment if used>
Aggressive search stance: <bounded but non-guard exploration policy; caps/cache/stop logic>
Knowledge used: <mechanism cards/blueprints used as inspiration menu; or none>
## Insight Packets
$student_insight_packet_format
```

Use exactly one `### student_XX` packet for every Student ID in the roster.
Each Student packet must describe exactly one primary mechanism hypothesis.
Do not assign multi-step continuation chains, guard lanes, or duplicate variants.

Calibration-specific requirements inside each `### student_XX` packet:

- `route` must be a unique mechanism label, not a broad stage name.
- `route action` should normally be `switch`, `mechanism-redesign`, `hybridize`,
  or `repair`; avoid `elite-expand` and guard-style continuation.
- `start/parent` must name exactly one prepared branch:
  `framework`, `diamond`, or `default_negotiation`.
- `expected HPWL source` must name the mechanism's primary final-HPWL source,
  not mirror alone.
- `stage-wise proof target` must include the liveness counters or log lines that
  prove the mechanism executed.
- `runtime/complexity` should be `explore` or `aggressive` with caps, cache, or
  stop conditions.
- `record` must require a compact `knowledge_card` suitable for calibration
  synthesis: branch, stage, mechanism, changed functions, counters, final/stage
  metrics, legality, runtime, and failure bucket.
