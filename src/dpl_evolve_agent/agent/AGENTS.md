# Artifact Agent Contract

This directory is the Agent-facing control plane. Human execution guidance is
kept in `docs/AE_ENVIRONMENT_AND_SMOKE.md`.

## Non-Negotiable Boundaries

- Treat the historical experiment backup as read-only evidence.
- Do not modify protected evaluator code, baseline semantics, reference
  results, or `metadata/ae_reproduction_lock.json` to make a check pass.
- Do not launch Teacher, Student, Codex workers, or any paid model call without
  an explicit budget authorization for that run.
- Do not install system packages, invoke `sudo`, upload artifacts, or publish a
  release.
- Do not delete or overwrite existing flow variants, logs, or metrics.
- Keep credentials in environment variables and never print them.
- Record source revisions, input checksum, command, threads, exit status, and
  output paths for every fresh run.

## Entrypoint Rule

Use `scripts/ae/` wrappers for AE environment and smoke work. Do not reproduce
their shell commands manually. Human and Agent entrypoints may differ in
documentation, but both must call the same protected implementation under
`baseline/` and `scripts/workspace/`.

## Result Rule

A command finishing is not evidence of success. Success requires the validator
to exit 0. Report failed and missing checks as failures; never replace missing
artifacts with inferred numbers.
