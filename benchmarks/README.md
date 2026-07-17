# Benchmarks

This directory contains benchmark design manifests and download helpers.

## Structure

- `manifests/` — Design manifests listing required input files and their sources.
- `download/` — Scripts to download benchmark designs from public sources.

## Paper Cases

The paper uses 9 cases across two technology nodes:

### Nangate45 (5 cases)
- `aes_nangate45` — AES encryption core
- `ibex_nangate45` — Ibex RISC-V core
- `jpeg_nangate45` — JPEG encoder
- `ariane133_nangate45` — Ariane RISC-V core
- `bp_quad_nangate45` — BlackParrot quad-core

### ASAP7 (4 cases)
- `aes_asap7`
- `ibex_asap7`
- `jpeg_asap7`
- `swerv_wrapper_asap7`

## Input Snapshot Verification

Each benchmark's input snapshot (post-synthesis, post-global-placement ODB)
is verified by:
1. Instance count
2. Instance area
3. ODB SHA-256 checksum
4. Global HPWL (reference)

These expectations are recorded in `../configs/paper/baseline_9case.yaml`.
