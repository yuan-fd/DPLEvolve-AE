# Artifact Evaluation Entrypoints

This namespace contains the stable Artifact Evaluation wrappers:

- `check_environment.sh`: read-only verification of revisions, binaries, and
  Python dependencies.
- `setup_user_environment.sh`: idempotent user-level Python, Yosys, and
  OpenROAD setup. It does not use `sudo`.
- `run_aes_smoke.sh`: check or reproduce the pinned AES/Nangate45 native
  OpenROAD detailed-placement baseline.
- `validate_aes_smoke.py`: machine-readable validation used by the smoke
  wrapper and tests.

These wrappers delegate experiment execution to `baseline/` and workspace
construction to `scripts/workspace/`. They do not implement a second flow.
