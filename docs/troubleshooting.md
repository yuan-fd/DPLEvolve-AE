# Troubleshooting

Common problems and their solutions.

---

## Environment Check Failures

### "module: command not found"

**Symptom**: `make setup` fails with module-related errors.

**Solution**: Your server may not use Environment Modules. Install build
dependencies manually:

```bash
# RHEL/Rocky/CentOS
sudo dnf install gcc gcc-c++ cmake make bison flex python3

# Ubuntu/Debian
sudo apt install gcc g++ cmake make bison flex python3
```

Or set `DPL_EVOLVE_SKIP_MODULES=1` before running setup.

---

### "Could not load gcc/default module"

**Symptom**: Server has modules but GCC module name differs.

**Solution**: Find the correct module name:
```bash
module avail gcc
```

Then set it before running setup:
```bash
module load gcc/12.3.0  # or whatever is available
make setup
```

---

### "Bison version too old (need >= 3.6)"

**Symptom**: Yosys build fails with bison-related errors.

**Solution**: The server's `openroad` module provides Bison 3.8.2:
```bash
module load openroad
```

Or install manually:
```bash
# RHEL
sudo dnf install bison
# Ubuntu
sudo apt install bison
```

---

## Build Failures

### Yosys build fails

**Symptom**: `make setup` fails during Yosys build.

**Solutions**:
1. Check Bison and Flex versions:
   ```bash
   bison --version  # Need >= 3.6
   flex --version    # Need >= 2.6
   ```
2. Try with fewer parallel jobs:
   ```bash
   make setup JOBS=4
   ```
3. Check disk space:
   ```bash
   df -h .
   ```

---

### OpenROAD build fails

**Symptom**: `make setup` fails during OpenROAD build.

**Solutions**:
1. Check that all OpenROAD dependencies are available
2. Try building with `--skip-openroad-build` and use system OpenROAD if
   available (but note this may produce different results)
3. Check for CMake errors in the build log

---

## Smoke Test Failures

### Wrong input ODB checksum

**Symptom**: Smoke test fails with "input ODB SHA-256 mismatch".

**Root cause**: Wrong Yosys version was used for synthesis.

**Solution**:
1. Verify which Yosys is on PATH: `which yosys`
2. The correct Yosys is at: `$DPL_EVOLVE_STATE_ROOT/yosys/8449dd470/bin/yosys`
3. Run `make setup` to ensure the correct Yosys is built and set in the environment
4. Re-run `make smoke`

---

### Instance count mismatch

**Symptom**: 15,764 instances instead of 14,676.

**Root cause**: Yosys 0.63 was used instead of Yosys 0.64.

**Solution**: As above — use the pinned Yosys version.

---

### Final HPWL differs by more than tolerance

**Symptom**: Final HPWL is ~192,546 instead of ~176,845.

**Root cause**: Different netlist due to wrong Yosys version.

**Solution**: As above — use the pinned Yosys version.

---

### "Refusing to overwrite existing smoke run"

**Symptom**: `make smoke --run` refuses to run.

**Solution**: Use a different flow variant name:
```bash
./scripts/human/smoke_test.sh --run --flow-variant my_custom_variant
```

Or use `--rebuild` instead of `--run`.

---

### "metrics.json not found"

**Symptom**: Validation fails because metrics.json is missing.

**Solutions**:
1. Check that the baseline run actually completed: look for error messages
   in the OpenROAD log
2. Check disk space
3. Check that OpenROAD binary is working:
   ```bash
   $OPENROAD_EXE -help
   ```

---

## LLM/DSE Issues

### "API key not found"

**Symptom**: Full DSE reproduction fails with authentication errors.

**Solution**: Set your API key:
```bash
export ANTHROPIC_API_KEY=sk-ant-...
```
Or configure in `dpl_evolve_agent/env.sh`.

---

### Token cost concerns

If you're concerned about the token cost (~2.15B tokens/design):

1. Run on fewer cases: modify `configs/paper/evolve_search.yaml`
2. Reduce search breadth: edit the experiment plan
3. Run a smoke-level DSE first: use `configs/paper/evolve_smoke.yaml` if available

---

## Still Stuck?

1. Check the Phase 2 audit report: `../audit/phase2_aes_baseline_validation.md`
2. Run `make provenance` to capture your machine state
3. File an issue with the provenance report attached

---

## Quick Diagnostic Checklist

```bash
# 1. Basic system
uname -a && cat /etc/os-release | head -3

# 2. Toolchain
gcc --version | head -1
cmake --version | head -1
bison --version | head -1
flex --version | head -1

# 3. Python
$DPL_EVOLVE_PYTHON --version
$DPL_EVOLVE_PYTHON -c "import yaml; print('PyYAML:', yaml.__version__)"

# 4. Yosys
$YOSYS_EXE -V 2>&1 | head -1
sha256sum $YOSYS_EXE

# 5. OpenROAD
$OPENROAD_EXE -version 2>&1 | head -1
ldd $OPENROAD_EXE | grep "not found" || echo "All libraries resolved"

# 6. Disk
df -h .

# 7. Repo commits
git -C $DPL_EVOLVE_AGENT_ROOT rev-parse HEAD
git -C $ORFS_ROOT rev-parse HEAD
git -C $ORFS_ROOT/tools/yosys rev-parse HEAD
```
