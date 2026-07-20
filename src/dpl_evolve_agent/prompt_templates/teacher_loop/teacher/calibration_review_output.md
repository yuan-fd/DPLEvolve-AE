## Calibration Review Output Format

Write a compact calibration review only:

- calibration gate: confirm this was one iteration; number of Students launched,
  reviewable, valid, rejected-only, timeout/crash, and invalid artifact rows
- branch coverage: counts and result quality by `framework`, `diamond`, and
  `default_negotiation`
- stage coverage: counts and result quality by legalizer producer,
  handoff/frontier, DPO candidate/scoring/acceptance, transaction/reorder/local
  DP, post-consumer preservation, runtime-budget substitution, and mirror
- effective mechanisms: mechanisms with final HPWL improvement or strong
  stage-evidence worth promotion; include student id, branch, stage, HPWL,
  runtime, counters, source ref, and knowledge card path
- repairable workbenches: mechanisms that were live but blocked by one clear
  failure bucket; include the exact next repair link, not a continuation plan
- negative evidence: mechanisms that should not be repeated without a changed
  premise; name the failure bucket and proof
- mirror audit: confirm no mirror-only route was treated as primary; if any
  mirror route exists, state the upstream accepted gain it tried to preserve
- aggressive-search audit: identify which mechanisms were real aggressive
  probes versus weak retunes or guard lanes
- knowledge synthesis: list calibration evidence records to write into
  knowledge, grouped as `effective`, `repairable`, and `negative`; each entry
  must include branch, stage, mechanism, source handles, counters, metric
  evidence, and failure bucket or success reason
- next calibration pool cleanup: duplicates to remove, stage gaps still missing,
  and mechanism families to prioritize in the next one-iteration pool

Do not end with next-round Student packets in calibration mode unless the user
explicitly asks for another calibration launch. The review output should be
directly usable for updating calibration knowledge.
