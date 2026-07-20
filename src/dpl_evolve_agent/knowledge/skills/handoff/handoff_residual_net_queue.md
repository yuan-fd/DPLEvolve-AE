# Skill Note: handoff_residual_net_queue

Canonical structured fields live in `knowledge/index/skill_cards.jsonl`.
Use `scripts/repo/query_knowledge.py --q handoff_residual_net_queue --show-full`
for source handles, metrics, and log signals.

## Extra Guidance

- Use this when legal HPWL improves but improve placement or final evaluation
  stages erase the gain; repair the producer/consumer handoff, not downstream
  optimize-mirroring code.
- Use OpenDB handles as the stable identity: `odb::dbInst*`, `odb::dbNet*`,
  `odb::dbGroup*`, and `odb::dbRegion*`.  Groups/regions are valid anchors for
  cell grouping, fence pressure, residual queues, and region-bounded DPO
  priority.
- Do not store current legal coordinates; DPO can read them from OpenDB.  Store
  only non-importable guidance: target miss, original/global target, residual
  pressure, touched-net priority, or boundary/window tags.
- Before DPO hot loops, remap handles into current detailed-placement objects
  and dense ids, cell/segment lists, or bitsets.  Do not persist transient
  `Node*`, `Edge*`, `DetailedSeg*`, `Pixel*`, or `Group*` objects across the
  stage boundary.
- Require producer and consumer counters.  Metadata with no consumer is a
  no-op.
- Keep the queue bounded and do not route hot handoff data through files,
  JSON/text logs, string-keyed maps, or public Tcl/SWIG ABI changes.
