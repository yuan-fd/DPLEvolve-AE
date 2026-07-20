# Legalizer-DPO-Handoff Triage

## Core Warning

Do not assume downstream DPO can fully repair a bad legalized placement.
Detailed placement is a producer-consumer flow:

```text
global placement -> legalization producer -> handoff state -> DPO consumer -> final mirror/check
```

If legalization produces a legal but DPO-hostile state, later DPO may consume
runtime on local moves while never reaching the basin that a better legalizer
could have exposed. A stronger DPO can help, but it is not a substitute for
fixing the producer state or making the producer-consumer handoff informative.

Do not optimize legalization HPWL (`HPWLlg`) alone. Legalization repair is
valuable only when it improves the complete handoff state: final HPWL, legality,
average displacement, maximum displacement, local row/order recoverability,
handoff/frontier usefulness, and downstream DPO exact accepted gain. A patch
that lowers `HPWLlg` by pulling targets toward a proxy objective but increases
displacement tails, reduces DPO exact accepts, or worsens final HPWL is negative
evidence.

Also do not let legalization destroy the useful structure of the global
placement too early. Global placement already contains approximate net-length,
density, and region information. A legalizer that chases local legality by
large early row/order disruption can turn a recoverable global basin into a
legal but poorly recoverable local ordering. Once that structure is destroyed,
downstream DPO may not be able to reconstruct it with local swaps or short
row-window moves.

## Three Valid Improvement Routes

Teacher should keep all three routes available and choose by stage evidence.
Do not let every weak round collapse into more DPO polish, and do not let it
collapse into legalizer-only repair either. A strong candidate should normally
state a producer-consumer story:

- what legalizer behavior makes the placement DPO-recoverable;
- what DPO/improve-placement mechanism consumes or further repairs that state;
- what bounded handoff/frontier evidence connects the two.

A route may emphasize one surface, but the other two surfaces should be
explicitly changed, consumed, or guarded with source-level evidence.

### 1. Repair Legalizer Output So DPO Can Work

Use when:

- `HPWLlg` is poor, or legal HPWL improves but DPO recovery is weak.
- `HPWLg -> HPWLlg` shows a large early disruption, especially when DPO later
  consumes runtime but cannot recover final HPWL.
- Legalizer logs show large residual target miss, row pressure, conflict
  leftovers, or many cells moved into DPO-hostile local order.
- DPO accepts are nonzero but mostly repair damage created by legalization.

Mechanism directions:

- change row assignment / local ordering / residual-component repair;
- preserve global or resource guidance but stop before it destroys local
  recoverability;
- preserve the global-placement basin when possible: prefer bounded row
  assignment, local-order repair, residual-component repair, and rollback over
  broad early perturbation;
- use bounded exact local repair around high-pressure rows, residual components,
  or touched-net clusters;
- make legalizer output a state that DPO can improve, not just a locally legal
  endpoint.

Proof:

- recoverability improves without uncontrolled avg/max displacement growth;
- `HPWLimprove` and final HPWL improve, not only legal-stage HPWL;
- DPO accept counters and accepted delta remain healthy or improve after the
  legalizer change.

### 2. Strengthen DPO Consumer

Use when:

- `HPWLlg` is stable or already competitive, but `HPWLimprove`/final HPWL still
  leaves recoverable local residue.
- DPO counters show live candidates but low accept quality, narrow move class,
  or repeated local-minimum behavior.
- There is runtime headroom under the hard budget.

Mechanism directions:

- add exact-delta move classes such as row shifts, insertion moves, swaps,
  local reorder, multi-row windows, or bounded basin escape;
- improve candidate ranking with affected-net or boundary-net scoring;
- use transactional accept/rollback and deterministic conflict filtering;
- allocate runtime cost to new accepted-gain sources, not to repeating the same
  zero-accept candidate family.

Proof:

- DPO candidate, exact-probe, and accept counters are nonzero;
- `HPWLimprove` and final HPWL move in the same direction;
- runtime increase is tied to accepted gain or reduced wasted probing.

### 3. Strengthen Handoff Between Legalizer And DPO

Use when:

- legalization has useful local information, but DPO behaves as if it did not
  see it.
- stage metrics show legal-stage donor value that disappears after improve
  placement.
- producer counters are nonzero, but consumer probes/accepts are zero or weak.

Mechanism directions:

- pass bounded in-process frontier state, not logs or files;
- represent residual nets, touched rows, boundary-active cells, pressure
  regions, or recoverable local windows using OpenROAD-native object handles and
  dense ids;
- remap the handoff into current `DetailedMgr` / DPO data structures before
  candidate ranking;
- keep the payload thin enough that it guides DPO instead of becoming a new
  broad scan.

Proof:

- producer and consumer counters are both nonzero;
- handoff-ranked candidates change the accepted move distribution;
- final HPWL improves or the route is classified as a stage donor plus a clear
  consumer failure.

## Teacher Decision Rule

Before assigning another DPO-only round, Teacher should answer:

1. Is the legalizer producing a DPO-recoverable placement?
2. Is DPO's move class strong enough for the remaining residue?
3. Is useful legalizer information reaching DPO through a bounded handoff?

If the answer to 1 is no, route at least one Student to legalizer-output repair.
If the answer to 2 is no, route at least one Student to a new DPO move family.
If the answer to 3 is no, route at least one Student to handoff producer-consumer
repair. A guard route can preserve the current elite, but the remaining routes
should not all retune the same DPO basin.

For an ordinary optimization round, require every Student packet to name one
dominant mechanism and one adjacent-stage compatibility proof.  A legalizer
route should explain why the legal output remains DPO-recoverable.  A DPO route
should explain why the input state and any frontier make the search targeted.  A
handoff route should name both producer and consumer counters.

This is not a request for cosmetic edits in all files.  It is a requirement that
the candidate design explains how the stages compose and why the emphasized
mechanism can move final HPWL.

## Student Reporting Contract

Student should state which route it chose:

- `legalizer-output repair`
- `DPO-consumer strengthening`
- `handoff repair`
- `co-optimization`

The report must include stage metrics and counters that prove the chosen route
ran. If a DPO-only change gives tiny or zero gain, Student should diagnose
whether the remaining problem is actually upstream legalizer recoverability or
missing handoff, not only retune DPO thresholds.

For a legalizer-output route, the report must state whether any `HPWLlg` change
also preserved avg/max displacement and DPO accepted gain. If not, classify the
result as a proxy or stage-only failure and propose a recoverability/handoff
repair instead of more `HPWLlg` tuning.
