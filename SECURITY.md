# Security Policy

## Reporting Security Issues

The DPLEvolve artifact evaluation repository is research code. If you discover
a security vulnerability, please report it responsibly.

**Do not report security vulnerabilities through public GitHub issues.**

Contact the paper authors directly (see paper for contact information).

## API Key Safety

This repository NEVER stores API keys, tokens, or credentials.

### For Artifact Evaluators

- Set API keys via environment variables: `export ANTHROPIC_API_KEY=...`
- Never commit API keys to this repository
- Never include API keys in experiment logs or provenance records
- The `.gitignore` excludes `.env`, `*.key`, `*.token`, and `credentials/`

### For Agent Automation

- Agents MUST NOT write API keys to any tracked file
- Agents MUST NOT include credentials in log output
- Agents MUST read API keys from environment variables only

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
