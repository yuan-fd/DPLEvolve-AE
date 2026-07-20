# Skill: replay_debug

## Purpose
Debug legalizer behavior using small or replayable cases before re-running full design baselines.

## Principle
When a patch causes:
- build success but runtime crash
- legality regression
- extreme displacement
- obvious overflow pathology

Codex should prefer:
1. replay case
2. strict legalizer single-run
3. full baseline suite

## Expected replay sources
- extracted local windows / plates
- design-specific small manifests
- explicit replay archives named by the task

## Required replay outputs
- failing surface
- local metric anomaly
- whether failure is initialization / cost / search / refinement related
