# DPLEvolve Artifact Evaluation
# Reviewer-facing commands delegate to self-contained bundles under artifacts/.

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c

AE_ROOT := $(CURDIR)
SMOKE_THREADS ?= $(shell nproc 2>/dev/null || echo 4)
ARTIFACTS_DIR := $(AE_ROOT)/artifacts
HUMAN_SCRIPTS := $(AE_ROOT)/scripts/human
SHARED_SCRIPTS := $(AE_ROOT)/scripts/shared
MAINTENANCE_SCRIPTS := $(AE_ROOT)/scripts/maintenance

DPL_EVOLVE_AGENT_ROOT ?= $(AE_ROOT)/src/dpl_evolve_agent
ORFS_ROOT ?= $(AE_ROOT)/../OpenROAD-flow-scripts
DPL_EVOLVE_STATE_ROOT ?= $(AE_ROOT)/../dpl_evolve_state
DPL_EVOLVE_PYTHON ?= python3

-include env.local.sh

export AE_ROOT
export DPL_EVOLVE_AGENT_ROOT
export ORFS_ROOT
export DPL_EVOLVE_STATE_ROOT
export DPL_EVOLVE_PYTHON

.PHONY: help evidence table4 table5 table6 smoke smoke-check
.PHONY: check bootstrap setup test test-structure test-integration test-unit
.PHONY: validate-configs provenance zenodo zenodo-audit clean

.DEFAULT_GOAL := help

help:
	@echo "DPLEvolve Artifact Evaluation"
	@echo ""
	@echo "Reviewer path:"
	@echo "  make evidence       Run the Table 4, Table 5, and Table 6 bundles"
	@echo "  make table4         Run only the Table 4 QoR bundle"
	@echo "  make table5         Run only the Table 5 composability bundle"
	@echo "  make table6         Run only the Table 6 cut-row bundle"
	@echo "  make smoke          Run a fresh AES Nangate45 OpenROAD smoke flow"
	@echo "  make smoke-check    Validate an existing reference smoke run"
	@echo ""
	@echo "Environment:"
	@echo "  make check          Inspect the prepared tool environment"
	@echo "  make bootstrap      Create a pinned sibling ORFS workspace"
	@echo "  make setup          Build the pinned Python/Yosys/OpenROAD environment"
	@echo ""
	@echo "Maintenance:"
	@echo "  make test           Run repository tests"
	@echo "  make zenodo-audit   Build an audit archive with placeholder authors allowed"
	@echo "  make zenodo         Build the release archive"
	@echo "  make clean          Remove generated artifact-bundle outputs"
	@echo ""
	@echo "Each bundle can also be run directly from artifacts/."

evidence: table4 table5 table6
	@echo ""
	@echo "[PASS] All packaged paper-evidence bundles passed"

table4:
	@bash "$(ARTIFACTS_DIR)/01-table4-qor/run.sh"

table5:
	@bash "$(ARTIFACTS_DIR)/02-table5-composability/run.sh"

table6:
	@bash "$(ARTIFACTS_DIR)/03-table6-cutrow/run.sh"

smoke:
	@bash "$(ARTIFACTS_DIR)/04-aes-smoke/run.sh" --run --threads $(SMOKE_THREADS)

smoke-check:
	@bash "$(ARTIFACTS_DIR)/04-aes-smoke/run.sh" --check-only

check:
	@bash "$(HUMAN_SCRIPTS)/check_environment.sh"

bootstrap:
	@bash "$(HUMAN_SCRIPTS)/bootstrap_workspace.sh"

setup:
	@bash "$(HUMAN_SCRIPTS)/setup.sh"

test: test-structure test-integration test-unit
	@echo ""
	@echo "[PASS] All repository tests passed"

test-structure:
	@bash "$(AE_ROOT)/tests/artifact/test_ae_structure.sh"

test-integration:
	@bash "$(AE_ROOT)/tests/integration/test_smoke_pipeline.sh"

test-unit:
	@"$(DPL_EVOLVE_PYTHON)" -m pytest "$(AE_ROOT)/tests/unit/" -v 2>/dev/null || \
	 PYTHONPATH="$(AE_ROOT)" "$(DPL_EVOLVE_PYTHON)" -m unittest discover -s "$(AE_ROOT)/tests/unit/" -v

validate-configs:
	@"$(DPL_EVOLVE_PYTHON)" "$(SHARED_SCRIPTS)/validate_config.py" --all

provenance:
	@bash "$(MAINTENANCE_SCRIPTS)/record_provenance.sh"

zenodo:
	@bash "$(MAINTENANCE_SCRIPTS)/prepare_zenodo.sh"

zenodo-audit:
	@bash "$(MAINTENANCE_SCRIPTS)/prepare_zenodo.sh" --allow-placeholder-authors

clean:
	@echo "Removing generated artifact outputs..."
	@find "$(ARTIFACTS_DIR)" -type d -name output -exec \
	  find {} -mindepth 1 ! -name .gitkeep -delete \;
	@echo "Archived inputs and expected values were preserved."
