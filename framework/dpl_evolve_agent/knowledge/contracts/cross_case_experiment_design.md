# Cross-Case Experiment Design

## Agent Use

- Role: workflow or metric contract. Use to keep experiments and reporting consistent, not to choose an algorithmic route.
- Route selection still starts from the core case-type blueprint map and current metrics.


Single-case Teacher/Student evolution is useful for finding mechanisms, but it
is not enough to prove that a DPL change is valuable.  Use an outer experiment
matrix to separate three questions:

1. Does the mechanism beat the canonical `openroad_dpl_flow` baseline on the
   discovery line?
2. Does it still help when placement density or whitespace changes?
3. Does it transfer to designs with different population, density, technology,
   and HPWL sensitivity?

## Preferred Pattern

1. Define an explicit experiment plan outside default knowledge.
2. Generate or reuse placement inputs for every matrix row.
3. Refresh `openroad_dpl_flow`, `openroad_dpl_negotiation`, and
   `evolve_default` baselines for every row.
4. Run Teacher/Student loops on one discovery line to produce candidate source
   source states.
5. Freeze each promising student source commit.
6. Run the same frozen source unchanged on every matrix row.
7. Compare canonical metrics, source diffs, and each student's mechanism report.

## Matrix Shape

Use rows that cover:

- the discovery design at low, medium, and high utilization,
- small smoke designs,
- medium wirelength-sensitive designs,
- large row-rich designs,
- at least one alternate technology or library setting.

The exact case ids belong in the experiment plan, not in this insight card.
Round ids should describe design class and utilization class rather than
hard-coding a benchmark name when possible.

## Promotion Rule

Do not call tiny HPWL gains a strategic win.  Preserve them as donor evidence
when they reveal a mechanism, but use the outer matrix to decide whether the
mechanism is worth turning into a stronger patch.

The key control is fixed-source validation.  If a student improves only after
rewriting for each target, that is search evidence.  If the same source commit
improves multiple density classes and design families without edits, that is
algorithm knowledge.

## Expected Scale

A transfer matrix should be large enough to distinguish single-design
overfitting from robust mechanisms.  Choose enough frozen source variants and
feature-diverse rows to expose utilization, scale, technology, macro/fence, and
HPWL-sensitivity differences.
