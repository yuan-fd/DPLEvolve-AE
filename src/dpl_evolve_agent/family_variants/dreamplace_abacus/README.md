# DREAMPlace Abacus Reference

Role: row assignment, cluster compaction, and objective donor.

This directory contains a compact source snapshot from DREAMPlace's
`abacus_legalize` implementation:

- copied files:
  - `source/CMakeLists.txt`
  - `source/abacus_legalize.py`
  - `source/src/abacus_legalize.cpp`
  - `source/src/abacus_legalize_cpu.h`

This is reference material only.  The framework should not import this code
directly into OpenROAD without a deliberate patch and build plan.  Agents should
study it for Abacus-style row legalization, cluster handling, displacement
objectives, and CPU data structures.
