# Pull Request

## Description
<!-- Describe your changes in detail -->

## Type of change
- [ ] Bug fix
- [ ] New feature / experiment
- [ ] Documentation update
- [ ] Configuration change
- [ ] Test improvement

## Validation
- [ ] `bash tests/artifact/test_ae_structure.sh` passes
- [ ] `bash tests/integration/test_smoke_pipeline.sh` passes
- [ ] `python3 scripts/shared/validate_config.py --all` passes
- [ ] `make check` runs (if environment is set up)

## Checklist
- [ ] No hardcoded absolute paths in tracked files
- [ ] No API keys, tokens, or credentials in any file
- [ ] All new scripts are executable (`chmod +x`)
- [ ] Documentation updated if needed
