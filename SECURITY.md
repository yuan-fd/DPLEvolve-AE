# Security Policy

## Reporting Security Issues

The DPLEvolve artifact evaluation repository is research code. If you discover
a security vulnerability, please report it responsibly.

**Do not report security vulnerabilities through public GitHub issues.**

Use GitHub's private vulnerability-reporting feature for the public repository.
The included reviewed PDF is anonymous and does not provide a safe contact
address. Maintainers must add the artifact contact before public release.

## API Key Safety

This repository never stores API keys, tokens, or credentials.

### For Artifact Evaluators

- The supported reviewer path does not need an API key.
- Never commit API keys to this repository
- Never include API keys in experiment logs or provenance records
- The `.gitignore` excludes `.env`, `*.key`, `*.token`, and `credentials/`

## Input Validation

The experiment scripts validate:
- ODB input checksums before running placement
- Binary hashes to detect tampering
- Repository commits for provenance integrity

## Build Safety

- All builds run under user privileges (no sudo)
- Build outputs go to user-writable project directories only
- No system files are modified
- No packages are installed system-wide
