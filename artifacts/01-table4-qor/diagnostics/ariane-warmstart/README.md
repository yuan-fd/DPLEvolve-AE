# Ariane133 warm-start diagnostic

This directory retains the six exact source trees behind the diagnostic
footnote associated with Figure 4.  Four candidates missed the complete
Diamond-sourceTopK handoff, while two consecutive candidates came from the
Level-1-guided route.  The footnote is explicitly diagnostic context, not a
controlled ablation.

`make reproduce-ariane-diagnostic` rebuilds every source tree, runs the same
protected full detailed-placement evaluator on Ariane133 Nangate45, and
recomputes both group means from fresh metrics.  It does not call an LLM.

The source identities, tree digests, and author-run metrics are recorded in
`configs/reproduction/ariane-diagnostic.tsv`.  The incoming paper-time ODB was
deleted; the public command therefore uses the pinned-flow reconstruction and
reports tolerance-based scientific agreement rather than bit-for-bit identity.
