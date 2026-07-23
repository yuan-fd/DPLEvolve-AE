# Level 1 Evidence Index

## Author-time status

The author-time three-case Level 1 packet and exact Student breadth were not
retained. The main paper specifies JPEG N45 UTIL=90, AES N45 UTIL=70, and SWERV
N45 UTIL=60, but does not state that breadth. No author-time evidence id is
listed here, and the public artifact does not claim exact search-process replay.

## Fresh public reconstruction

`make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes` runs all three calibration
cases using the explicitly labeled public profile (default: 50 Students per
case, one iteration). It freezes only actual Teacher final reviews, Student
operation provenance, and complete source-start hashes into:

- `$DPL_EVOLVE_STATE_ROOT/calibrations/paper_level1/frozen/level1_evidence.md`
- `$DPL_EVOLVE_STATE_ROOT/calibrations/paper_level1/frozen/level1_evidence.json`

Level 2 verifies the packet/manifest hash and protocol before consuming it.
Fresh evidence ids are derived from their immutable round ids as
`<round_id>:iter_01:teacher_review`; they are not pre-populated in this tracked
index before the paid reconstruction is run.

## Reconstructible source starts

- `framework`: base plus constrained framework patch.
- `diamond`: clean evolved-DPL base before the framework patch.
- `default_negotiation`: framework plus the default-negotiation seed patch.

Their exact fresh tree hashes are written by the freezer. Git bookkeeping is
excluded from the digest so identical source content hashes portably.
