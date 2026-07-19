# Skill: build_openroad

Use this when you are responsible for building binaries. In Teacher/Student
rounds, Students should normally run the generated build script listed in the
workspace packet instead of reconstructing build steps.

## Preconditions

- `DPL_EVOLVE_AGENT_ROOT`, `ORFS_ROOT`, and `DPL_EVOLVE_STATE_ROOT` are set.
- `ORFS_ROOT` has already been prepared.
- External tool dependencies are already available. Do not run
  `./build_openroad.sh --local` inside the evolve loop.

## Preferred Student Build Path

In Teacher/Student rounds, use:

- `build_variant_script` from the workspace packet
- `fresh_build_script` only if the normal generated script fails or leaves the
  private binary missing

Do not open CMake link files, object files, or archives unless the generated
script error proves that lower-level inspection is necessary.
Do not spend implementation time learning the build system; the generated
script is the interface for Student source workers.

## Manual Fallback For Maintainers

Build common core once:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/configure_openroad_core.sh"
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/build_openroad_core.sh" --threads 10
```

Build a private variant binary. In normal Student execution this is already
wrapped by the workspace packet scripts:

```bash
"$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/configure_openroad_variant_relink.sh" \
  --variant-root "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>" \
  --dpl-src /abs/path/to/<agent_id>/dpl_evolve
source "${DPL_EVOLVE_STATE_ROOT}/variants/<agent_id>/variant_env.sh"
python3 "$DPL_EVOLVE_AGENT_ROOT/scripts/workspace/build_openroad_variant_relink.py" --threads 10
```

Expected outputs:

- `${OPENROAD_BINARY}`
- `${OPENROAD_INSTALL_ROOT}/bin/sta` as a symlink when common-core `sta` exists

## Evidence To Report

- build status
- binary path
- elapsed build time
- first compiler error block if failed

## Do Not

- do not rebuild Yosys or the whole ORFS stack
- do not share one variant binary across different source variants
- do not assume root `tools/install/OpenROAD/bin/openroad` is correct for a
  family/variant run
- do not inspect CMake link files, object files, archives, or handcraft relink
  commands unless the generated helper itself fails and the error requires it
