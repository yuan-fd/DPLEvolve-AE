# DPLEvolve Artifact Evaluation — Makefile
# Human-facing entry points for the artifact evaluation workflow.
#
# Quick start:
#   make check          # Validate the environment
#   make setup          # Build all dependencies
#   make smoke          # Run the minimal AES smoke test
#   make reproduce-main # Reproduce the main paper results
#
# Each paper table/figure has a dedicated target:
#   make table-1        # Baseline comparison table
#   make table-2        # Main HPWL results
#   make table-3        # Constraint repair results

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c

# ── Paths ──────────────────────────────────────────────────────────────────
AE_ROOT          := $(CURDIR)
SCRIPTS_DIR      := $(AE_ROOT)/scripts
INTERNAL_DIR     := $(SCRIPTS_DIR)/internal
HUMAN_DIR        := $(SCRIPTS_DIR)/human
AGENT_DIR        := $(SCRIPTS_DIR)/agent
CONFIGS_DIR      := $(AE_ROOT)/configs
RESULTS_DIR      := $(AE_ROOT)/results
PROVENANCE_DIR   := $(AE_ROOT)/provenance
ENV_DIR          := $(AE_ROOT)/env

# These must be set in the environment or env.local.sh
DPL_EVOLVE_AGENT_ROOT ?= $(CURDIR)/../dpl_evolve_agent
ORFS_ROOT           ?= $(CURDIR)/../OpenROAD-flow-scripts
DPL_EVOLVE_STATE_ROOT ?= $(CURDIR)/../dpl_evolve_state
DPL_EVOLVE_PYTHON   ?= python3

# Machine-local overrides: create env.local.sh (optional, not tracked)
# The scripts internally source the agent's env.sh and state's environment.sh.
# No bash include in Makefile — GNU Make can only include makefile syntax.
-include $(AE_ROOT)/env.local.sh

export DPL_EVOLVE_AGENT_ROOT
export ORFS_ROOT
export DPL_EVOLVE_STATE_ROOT
export DPL_EVOLVE_PYTHON
export AE_ROOT

# ── Phony targets ──────────────────────────────────────────────────────────
.PHONY: all check setup smoke reproduce-baseline reproduce-main clean distclean
.PHONY: table-1 table-2 table-3 figure-3
.PHONY: help provenance

.DEFAULT_GOAL := help

# ── Help ────────────────────────────────────────────────────────────────────
help:
	@echo "DPLEvolve Artifact Evaluation"
	@echo ""
	@echo "Quick start:"
	@echo "  make check           Validate environment"
	@echo "  make setup           Build all dependencies"
	@echo "  make smoke           Run minimal AES smoke test"
	@echo "  make reproduce-main  Reproduce main paper results"
	@echo ""
	@echo "Individual results:"
	@echo "  make table-1         Baseline comparison"
	@echo "  make table-2         Main HPWL results"
	@echo "  make table-3         Constraint repair results"
	@echo ""
	@echo "Maintenance:"
	@echo "  make provenance      Generate provenance record"
	@echo "  make clean           Remove reproduced results"
	@echo "  make distclean       Remove all build artifacts"
	@echo ""
	@echo "Full documentation: docs/"

# ── Environment Check ──────────────────────────────────────────────────────
check:
	@echo "=== DPLEvolve AE: Environment Check ==="
	@bash $(HUMAN_DIR)/check_environment.sh

# ── Setup ───────────────────────────────────────────────────────────────────
setup:
	@echo "=== DPLEvolve AE: Environment Setup ==="
	@bash $(HUMAN_DIR)/setup.sh

# ── Smoke Test ──────────────────────────────────────────────────────────────
smoke:
	@echo "=== DPLEvolve AE: AES Smoke Test ==="
	@bash $(HUMAN_DIR)/smoke_test.sh --run --threads 8

smoke-check:
	@echo "=== DPLEvolve AE: AES Smoke Test (check-only) ==="
	@bash $(HUMAN_DIR)/smoke_test.sh --check-only

# ── Baseline Reproduction ───────────────────────────────────────────────────
reproduce-baseline:
	@echo "=== DPLEvolve AE: Baseline Reproduction ==="
	@bash $(HUMAN_DIR)/reproduce_baseline.sh

# ── Main Results Reproduction ──────────────────────────────────────────────
reproduce-main:
	@echo "=== DPLEvolve AE: Main Results Reproduction ==="
	@bash $(HUMAN_DIR)/reproduce_main.sh

# ── Table/Figure Targets ───────────────────────────────────────────────────
table-1:
	@echo "=== DPLEvolve AE: Table 1 — Baseline Comparison ==="
	@bash $(HUMAN_DIR)/generate_tables.sh --table 1

table-2:
	@echo "=== DPLEvolve AE: Table 2 — Main HPWL Results ==="
	@bash $(HUMAN_DIR)/generate_tables.sh --table 2

table-3:
	@echo "=== DPLEvolve AE: Table 3 — Constraint Repair ==="
	@bash $(HUMAN_DIR)/generate_tables.sh --table 3

figure-3:
	@echo "=== DPLEvolve AE: Figure 3 ==="
	@bash $(HUMAN_DIR)/generate_tables.sh --figure 3

# ── Provenance ──────────────────────────────────────────────────────────────
provenance:
	@echo "=== DPLEvolve AE: Generating Provenance Record ==="
	@bash $(INTERNAL_DIR)/record_provenance.sh

# ── Cleanup ─────────────────────────────────────────────────────────────────
clean:
	@echo "=== DPLEvolve AE: Cleaning reproduced results ==="
	@rm -rf $(RESULTS_DIR)/reproduced/*
	@rm -rf $(RESULTS_DIR)/tables/*
	@echo "Reproduced results removed. Reference results preserved."

distclean: clean
	@echo "=== DPLEvolve AE: Deep cleaning ==="
	@echo "WARNING: This removes all build artifacts."
	@echo "To rebuild, run: make setup"
	@# Only clean AE-owned build artifacts, never touch third-party source trees
	@rm -rf $(AE_ROOT)/build/
	@echo "Build artifacts removed."
