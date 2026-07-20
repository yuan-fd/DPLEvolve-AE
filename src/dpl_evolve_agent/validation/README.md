# Validation

This directory contains guard utilities for source-diff and packet preflight
checks.

Validation protects the control plane and evaluator from accidental edits while
allowing the active workflow to mutate private `dpl_evolve` source trees.

Current utility:

- `preflight.py`: validate a proposed source diff against a packet-declared
  patch surface and forbidden control-plane paths.

Validation is a gate, not a mechanism generator.  Student agents should fix
reported violations rather than weakening these checks during a search round.
