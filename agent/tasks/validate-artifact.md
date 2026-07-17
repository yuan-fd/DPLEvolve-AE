# Task: Validate Artifact Completeness

## Goal
Verify that the artifact repository is complete, self-consistent, and ready
for submission.

---

## Checklist

### Documentation Completeness
- [ ] README.md exists and is reviewer-focused
- [ ] All docs/ files present
- [ ] Claims-to-artifacts mapping complete
- [ ] Artifact appendix ready for paper
- [ ] LICENSE, CITATION.cff, SECURITY.md present

### Script Completeness
- [ ] All make targets work
- [ ] All scripts/human/ scripts executable
- [ ] All scripts/agent/ scripts executable
- [ ] All scripts/internal/ scripts executable
- [ ] No hardcoded absolute paths in tracked files

### Configuration Completeness
- [ ] Smoke test config present and valid
- [ ] Paper experiment configs present
- [ ] Config schema defined

### Provenance Completeness
- [ ] source-commits.json has all required entries
- [ ] original-artifact-checksums.txt has all reference checksums
- [ ] versions.lock is machine-readable

### Security Review
- [ ] No API keys in any tracked file
- [ ] No passwords or tokens in any file
- [ ] No internal hostnames or IPs (except in provenance examples)
- [ ] No private email addresses (except paper author contacts)
- [ ] .gitignore excludes secrets, results, and build artifacts

### Reproducibility
- [ ] `make check` runs successfully
- [ ] `make setup` builds everything (or reuses existing)
- [ ] `make smoke` passes with exact HPWL match
- [ ] All paths are relative or use environment variables

---

## Auto-Validation Command

```bash
# Run all validation checks
bash scripts/agent/validate_run.sh --check-all
```

---

## Output

Write validation results to:
```
results/reproduced/validation/validation_report.txt
```

Format:
```
=== DPLEvolve AE Validation Report ===
Date: <timestamp>
Machine: <hostname>

Documentation: PASS/FAIL
Scripts:       PASS/FAIL
Configuration: PASS/FAIL
Provenance:    PASS/FAIL
Security:      PASS/FAIL
Smoke test:    PASS/FAIL

Overall:       READY / NEEDS FIXES
```
