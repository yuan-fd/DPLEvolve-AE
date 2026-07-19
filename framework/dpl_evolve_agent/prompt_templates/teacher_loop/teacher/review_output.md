## Output Format

Write a compact review only:

- validity gate: build/evaluator/metrics/source/diff/legality status
- result table: final HPWL/runtime/legality plus `G_HR`, HPWL_gain%, runtime_penalty_pp
- stage attribution: where legalization, DPO, or handoff helped or erased gains
- HPWL-source diagnosis: whether final HPWL movement came from the intended mechanism; if weak, name the missing producer, handoff, consumer, acceptance, polish, runtime, or case-type link
- failure bucket: for weak/flat/over-cost results, name the blocking bucket and the source/log evidence that proves it
- stack status: for blueprint/stack routes, classify roles as full-chain, partial-chain, or non-live; name the next compatible/missing link
- complete-chain audit: for any assigned blueprint, classify full-chain/partial-chain/non-live and name the first missing link
- mechanism classification: final donor, stage donor, workbench, validated-low-ROI, non-executed, or negative evidence
- mechanism reasoning check: whether the Student explained source changes, liveness evidence, and repair/pivot thinking
- knowledge synthesis: which knowledge/algorithm idea supports or rejects the next route
- efficiency diagnosis: useful controlled runtime cost, over-cost, or under-exploration
- next routing decision: elite-expand, repair, pivot, redesign, or freeze

End with:

```text
## Next Insight Packets
$student_insight_packet_format
```

Use exact student IDs from the artifact packet only.
