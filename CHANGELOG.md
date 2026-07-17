# Changelog

All notable changes to the DPLEvolve Artifact Evaluation repository.

## [1.0.0] — 2026-07-18

### Added
- Initial AE repository structure with full directory layout
- Makefile with human-facing targets: check, setup, smoke, reproduce-*
- Version lock files: provenance/source-commits.json, env/versions.lock
- Human documentation: README.md, docs/artifact-overview.md, docs/quickstart.md,
  docs/environment.md, docs/experiments.md, docs/expected-results.md,
  docs/troubleshooting.md, docs/claims-to-artifacts.md, docs/artifact-appendix.md
- Agent documentation: agent/AGENTS.md, agent/context/, agent/tasks/, agent/schemas/
- Shared scripts: scripts/internal/ with runtime_env.sh and record_provenance.sh
- Human entry scripts: scripts/human/check_environment.sh, setup.sh, smoke_test.sh,
  reproduce_baseline.sh, reproduce_main.sh, generate_tables.sh
- Agent entry scripts: scripts/agent/inspect_environment.sh, execute_experiment.sh,
  validate_run.sh, summarize_results.py
- Config files: configs/smoke/aes_nangate45.yaml, configs/schema/experiment_config.schema.json
- Benchmark manifests and third-party dependency documentation
- Provenance checksums for reference binaries and AES input ODB
- AES smoke test with input ODB SHA-256 verification
- Environment check script (shell)
- Machine-local environment generation

### Migrated
- Ported baseline runner logic from dpl_evolve_agent/baseline/
- Ported environment check from dpl_evolve_agent/scripts/ae/
- Ported smoke test logic from dpl_evolve_agent/scripts/ae/run_aes_smoke.sh
- Version pins from dpl_evolve_agent/metadata/ae_reproduction_lock.json

### Noted
- openroad_dpl_negotiation and evolve_default baselines depend on historical
  source versions not fully preserved; current-build values differ from old
  reference values
- Full LLM-powered DSE reproduction requires API credentials (not included)
- Post-deadline: ablation experiments, multi-seed runs, joint optimization
