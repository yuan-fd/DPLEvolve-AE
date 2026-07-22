# DPLEvolve paper experiment reproduction

SHELL := /bin/bash
.SHELLFLAGS := -euo pipefail -c

AE_ROOT := $(CURDIR)
ARTIFACTS_DIR := $(AE_ROOT)/artifacts
HUMAN_SCRIPTS := $(AE_ROOT)/scripts/human
REPRO_SCRIPTS := $(AE_ROOT)/scripts/reproduce
SHARED_SCRIPTS := $(AE_ROOT)/scripts/shared
MAINTENANCE_SCRIPTS := $(AE_ROOT)/scripts/maintenance

DPL_EVOLVE_AGENT_ROOT ?= $(AE_ROOT)/src/dpl_evolve_agent
ORFS_ROOT ?= $(AE_ROOT)/../OpenROAD-flow-scripts
DPL_EVOLVE_STATE_ROOT ?= $(AE_ROOT)/../dpl_evolve_state
DPL_EVOLVE_PYTHON ?= python3
THREADS ?= 10
TRACK ?= hpwl

-include env.local.sh

export AE_ROOT DPL_EVOLVE_AGENT_ROOT ORFS_ROOT DPL_EVOLVE_STATE_ROOT
export DPL_EVOLVE_PYTHON THREADS

.PHONY: help doctor bootstrap build-tools setup check prepare-paper-inputs
.PHONY: validate-evaluator reproduce-default setup-bo reproduce-bo replay-reviewdse
.PHONY: plan-level1 reproduce-level1
.PHONY: reproduce-table4 summarize-table4 reproduce-table5 reproduce-table6
.PHONY: reproduce-paper-results reproduce-paper-search
.PHONY: run-dse-small plan-dse-paper run-dse-paper paper-data-check
.PHONY: audit-archive evidence table4 table5 table6
.PHONY: toolchain-smoke smoke smoke-check doctor-smoke
.PHONY: test test-structure test-integration test-unit validate-configs
.PHONY: provenance zenodo zenodo-audit clean

.DEFAULT_GOAL := help

help:
	@echo "DPLEvolve — paper experiment reproduction"
	@echo ""
	@echo "1. Prepare the pinned execution environment:"
	@echo "  make doctor                 Inspect this Rocky/Linux host"
	@echo "  make bootstrap              Checkout and patch pinned ORFS/OpenROAD"
	@echo "  make build-tools            Build pinned Yosys/OpenROAD and Python env"
	@echo "  make prepare-paper-inputs   Generate the nine incoming placement ODBs"
	@echo "  make validate-evaluator     Run one fresh protected evaluator trajectory"
	@echo ""
	@echo "2. Reproduce paper experiments with fresh EDA execution:"
	@echo "  make reproduce-default      Nine OpenROAD default reference runs"
	@echo "  make setup-bo               Install pinned Ray Tune/Optuna dependencies"
	@echo "  make reproduce-bo           Nine 400-trial public-knob BO searches"
	@echo "  make replay-reviewdse TRACK=hpwl  Replay nine frozen HPWL programs"
	@echo "  make replay-reviewdse TRACK=ghr   Replay nine runtime-aware programs"
	@echo "  make reproduce-table4       Default + BO + both ReviewDSE replay tracks"
	@echo "  make summarize-table4       Build fresh Table 4 TSV from generated metrics"
	@echo "  make reproduce-table5       Fresh stage-composability counterexamples"
	@echo "  make reproduce-table6       Fresh hard cut-row repair experiment"
	@echo "  make reproduce-paper-results  All non-LLM paper result experiments"
	@echo ""
	@echo "3. Exercise or rerun the LLM search itself:"
	@echo "  make plan-level1             Print the three-case Level 1 calibration"
	@echo "  make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes"
	@echo "  make run-dse-small CASE=aes_nangate45  Real 1-Student/1-iteration run"
	@echo "  make plan-dse-paper         Print exact 9-case paper launch; no API calls"
	@echo "  make run-dse-paper ACKNOWLEDGE_LLM_COST=yes"
	@echo "  make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes"
	@echo ""
	@echo "Supporting checks (not paper reproduction):"
	@echo "  make paper-data-check       Report missing exact Table 5/6 assets"
	@echo "  make audit-archive          Recompute packaged TSV/JSON summaries"
	@echo "  make toolchain-smoke        Optional AES toolchain exercise"
	@echo "  make test                   Repository regression tests"

doctor:
	@bash "$(HUMAN_SCRIPTS)/doctor.sh"

doctor-smoke:
	@bash "$(HUMAN_SCRIPTS)/doctor.sh" --strict-smoke

bootstrap:
	@bash "$(HUMAN_SCRIPTS)/bootstrap_workspace.sh"

build-tools setup:
	@bash "$(HUMAN_SCRIPTS)/setup.sh" --jobs "$(THREADS)"

check:
	@bash "$(HUMAN_SCRIPTS)/check_environment.sh"

prepare-paper-inputs:
	@bash "$(REPRO_SCRIPTS)/prepare_paper_inputs.sh" --threads "$(THREADS)" $(if $(CASE),--case "$(CASE)",)

validate-evaluator:
	@bash "$(REPRO_SCRIPTS)/validate_evaluator.sh" --threads "$(THREADS)" $(if $(CASE),--case "$(CASE)",)

reproduce-default:
	@bash "$(REPRO_SCRIPTS)/run_baselines.sh" --threads "$(THREADS)" $(if $(CASE),--case "$(CASE)",)

setup-bo:
	@PYTHON_BIN="$(DPL_EVOLVE_PYTHON)" bash "$(DPL_EVOLVE_AGENT_ROOT)/scripts/bo/setup_raytune_venv.sh"

reproduce-bo:
	@bash "$(REPRO_SCRIPTS)/run_bo.sh" --threads "$(THREADS)" $(if $(CASE),--case "$(CASE)",)

replay-reviewdse:
	@bash "$(REPRO_SCRIPTS)/replay_selected.sh" --track "$(TRACK)" --threads "$(THREADS)" $(if $(CASE),--case "$(CASE)",)

reproduce-table4:
	@$(MAKE) reproduce-default THREADS="$(THREADS)" $(if $(CASE),CASE="$(CASE)",)
	@$(MAKE) reproduce-bo THREADS="$(THREADS)" $(if $(CASE),CASE="$(CASE)",)
	@$(MAKE) replay-reviewdse TRACK=hpwl THREADS="$(THREADS)" $(if $(CASE),CASE="$(CASE)",)
	@$(MAKE) replay-reviewdse TRACK=ghr THREADS="$(THREADS)" $(if $(CASE),CASE="$(CASE)",)
	@if [[ -z "$(CASE)" ]]; then $(MAKE) summarize-table4; else echo "One-case run complete; run all nine cases before summarize-table4."; fi

summarize-table4:
	@"$(DPL_EVOLVE_PYTHON)" "$(REPRO_SCRIPTS)/summarize_table4.py" \
	  --orfs-root "$(ORFS_ROOT)" \
	  --state-root "$(DPL_EVOLVE_STATE_ROOT)" \
	  --flow-variant paper9_place \
	  --selected-manifest "$(ARTIFACTS_DIR)/01-table4-qor/selected-programs/manifest.json" \
	  --expected "$(ARTIFACTS_DIR)/01-table4-qor/expected/table4.json" \
	  --output "$(DPL_EVOLVE_STATE_ROOT)/paper_reproduction/table4/table4-fresh.tsv"

reproduce-table5:
	@bash "$(REPRO_SCRIPTS)/reproduce_table5.sh" --threads "$(THREADS)"

reproduce-table6:
	@bash "$(REPRO_SCRIPTS)/reproduce_table6.sh" --threads "$(THREADS)"

# Complete no-LLM result reproduction. Check external data before spending the
# 3,600-run BO budget so an incomplete package fails early.
reproduce-paper-results: paper-data-check
	@$(MAKE) reproduce-table4 THREADS="$(THREADS)"
	@$(MAKE) reproduce-table5 THREADS="$(THREADS)"
	@$(MAKE) reproduce-table6 THREADS="$(THREADS)"

# Complete two-level discovery rerun. This is intentionally separate from
# result replay because it incurs the paper-scale model budget.
reproduce-paper-search:
	@if [[ "$(ACKNOWLEDGE_LLM_COST)" != "yes" ]]; then \
	  echo "[ERROR] Full Level 1 + Level 2 search requires ACKNOWLEDGE_LLM_COST=yes" >&2; \
	  exit 2; \
	fi
	@$(MAKE) reproduce-level1 ACKNOWLEDGE_LLM_COST=yes \
	  LEVEL1_CHILDREN="$(or $(LEVEL1_CHILDREN),50)" THREADS="$(THREADS)"
	@$(MAKE) run-dse-paper ACKNOWLEDGE_LLM_COST=yes THREADS="$(THREADS)"

paper-data-check:
	@bash "$(REPRO_SCRIPTS)/check_paper_data.sh"

plan-level1:
	@bash "$(REPRO_SCRIPTS)/run_level1.sh" --threads "$(THREADS)" --children "$(or $(LEVEL1_CHILDREN),50)" --dry-run

reproduce-level1:
	@ACKNOWLEDGE_LLM_COST="$(ACKNOWLEDGE_LLM_COST)" bash "$(REPRO_SCRIPTS)/run_level1.sh" --threads "$(THREADS)" --children "$(or $(LEVEL1_CHILDREN),50)"

run-dse-small:
	@bash "$(REPRO_SCRIPTS)/run_dse.sh" --profile small --case "$(or $(CASE),aes_nangate45)" --threads "$(THREADS)" --children "$(or $(STUDENTS),1)" --iterations "$(or $(ITERATIONS),1)"

plan-dse-paper:
	@bash "$(REPRO_SCRIPTS)/run_dse.sh" --profile paper --threads "$(THREADS)" --dry-run

run-dse-paper:
	@ACKNOWLEDGE_LLM_COST="$(ACKNOWLEDGE_LLM_COST)" bash "$(REPRO_SCRIPTS)/run_dse.sh" --profile paper --threads "$(THREADS)"

# Archived arithmetic/integrity checks. These do not invoke OpenROAD and do not
# count as reproducing a paper experiment.
audit-archive: table4 table5 table6
	@echo ""
	@echo "[PASS] Packaged archive audit passed (no fresh EDA execution)"

evidence:
	@echo "[NOTICE] 'make evidence' is a compatibility alias for 'make audit-archive'."
	@echo "[NOTICE] It does not reproduce the paper experiments."
	@$(MAKE) audit-archive

table4:
	@bash "$(ARTIFACTS_DIR)/01-table4-qor/run.sh"

table5:
	@bash "$(ARTIFACTS_DIR)/02-table5-composability/run.sh"

table6:
	@bash "$(ARTIFACTS_DIR)/03-table6-cutrow/run.sh"

toolchain-smoke smoke:
	@echo "[NOTICE] AES smoke is a toolchain exercise, not a paper reproduction."
	@bash "$(ARTIFACTS_DIR)/04-aes-smoke/run.sh" --run --threads "$(THREADS)"

smoke-check:
	@bash "$(ARTIFACTS_DIR)/04-aes-smoke/run.sh" --check-only

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
	@bash "$(MAINTENANCE_SCRIPTS)/prepare_zenodo.sh" \
	  --allow-placeholder-authors --allow-incomplete-paper-data

clean:
	@echo "Removing generated compact archive-audit outputs only..."
	@find "$(ARTIFACTS_DIR)" -type d -name output -exec \
	  find {} -mindepth 1 ! -name .gitkeep -delete \;
	@echo "Fresh reproduction state under $(DPL_EVOLVE_STATE_ROOT) was preserved."
