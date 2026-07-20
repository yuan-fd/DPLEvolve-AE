# Mechanism Robustness Support

## Agent Use

- Role: inspiration/support only. This card must not directly select a case route or override the core blueprint map.
- First map the case through `case_feature_to_mechanism_route_map.md`; then use this card only to refine a selected mechanism, risk, or failure diagnosis.
- If this card repeats a core idea, treat the core blueprint as authoritative and keep this card as background evidence.

## Evidence Type

Mechanism robustness evidence.  This card keeps only reusable lessons from
feature-diverse mechanism validation.  It is not a case-name lookup table and
not a source-history selector.

Teacher should use the core route map first.  Use this card only after a
blueprint has been selected and the question is whether the selected mechanism
looks robust, expensive, or likely over-specialized.

## Reusable Lessons

1. Strong final-HPWL donors are usually complete chains:
   - a producer creates a compact frontier, target, residual, source-edge, or
     row-window signal;
   - the signal is mapped into native detailed-placement objects;
   - DPO or local exact repair consumes it with rollback-safe HPWL scoring;
   - later reorder, endpoint, pair, mirror, or guard logic only preserves
     accepted upstream moves.
2. A legalizer-stage win is not enough.  If `HPWLlg` improves but final HPWL
   does not, treat the route as a stage donor or DPO-consumer failure.  Check
   avg/max displacement, payload liveness, exact accepts, and accepted delta.
3. Some mechanisms apply broadly because they are source-local and exact:
   source-edge top-K DPO, selected-segment exact reorder, affected-net scoring,
   component/window replay, and canonical replay/restore.
4. Some mechanisms are valid but costly: scoped basin escape, broad component
   replay, endpoint branch scoring, and large exact transaction search.  Use
   them when lighter exact consumers are live and plateaued; add cache, caps,
   and gain-rate stops rather than disabling the quality mechanism.
5. Mechanism failures are also reusable:
   - passive handoff metadata with zero consumer accepts;
   - mirror-only or guard-only work before upstream exact moves are live;
   - threshold/counter changes that do not alter candidate generation, scoring,
     transaction granularity, or preservation;
   - producer tuning that reduces legal HPWL but creates a DPO-hostile basin.

## Feature-Level Support

- Low/mid-util or slack-rich cases: prefer Diamond/source-edge exact DPO and
  row-local exact closure before heavy basin escape.  If low-util heuristics
  bypass exact DPO, repair the dispatch gate.
- Dense or high-pressure cases: a recoverable legalizer producer is useful only
  when it passes target/source/frontier state to an exact consumer.  Do not
  promote legalizer-only target polishing.
- Wrapper, hierarchy, macro, or row-fragmented cases: residual/window producers
  and source-edge exact consumers are both plausible.  Pick by evidence: real
  residual payload favors row-window/component replay; endpoint geometry favors
  exact source-edge plus selected-segment/post consumers.
- Negotiation-compatible cases: negotiation is valuable when it produces a
  recoverable conflict/resource basin.  The quality source should still be
  exact global/source-edge, hot-segment reorder, or post-reorder exact/pair/
  endpoint consumers.
- Same-family plateau: keep one elite continuation, but assign other Students
  to a different producer, handoff payload, DPO move family, objective, or
  transaction shape.  More counters in the same dead route are not evidence.

## Student Use

When using this support card, the Student report should say:

- which producer, handoff, and consumer links were implemented;
- which counters prove each link is live;
- whether final HPWL movement came from legalization, DPO, post-DPO, or
  preservation;
- if final HPWL is weak, which link is missing and what the next repair should
  be.
