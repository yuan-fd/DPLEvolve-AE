# Repository architecture

The public repository is organized by reviewer task, not by implementation
type. Each supported result is a self-contained bundle under `artifacts/`.

```text
artifacts/
  01-table4-qor/
  02-table5-composability/
  03-table6-cutrow/
  04-aes-smoke/
src/dpl_evolve_agent/
scripts/
  human/
  agent/
  shared/
  maintenance/
docs/
agent/
schemas/
```

## Artifact contract

Every top-level artifact bundle provides:

```text
README.md   human explanation and evidence boundary
run.sh      independent entry point
inputs/     archived inputs or input-generation instructions
expected/   checked paper values or reproduction lock
output/     ignored generated reports
```

Bundle-specific code and configuration remain inside the bundle. Shared code
is limited to environment and utility functions needed by more than one
execution path.

## Human and machine interfaces

Human users enter through `README.md`, `make`, `scripts/human/`, and the
artifact READMEs. These files use explanatory prose and stable commands.

Automation agents enter through `agent/` and `scripts/agent/`. The dispatcher
accepts a fixed artifact ID and writes a JSON run manifest. Agent instructions
do not appear in the reviewer quick start.

## Framework boundary

`src/dpl_evolve_agent/` contains the research implementation used by the
fresh smoke flow and optional source replay. Evidence-only verification does
not import or initialize the framework. This keeps archived checks fast and
independent of OpenROAD, model APIs, and the original discovery workspace.

## Release boundary

`extras/unsupported/` is a local preservation area for incomplete launchers,
legacy layouts, and unsupported experiments. It is ignored by Git and
explicitly excluded from Zenodo archives. Nothing in public documentation or
CI depends on it.
