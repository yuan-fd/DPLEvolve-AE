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
DPL_EVOLVE_PYTHON ?= $(if $(wildcard $(AE_ROOT)/../.venvs/dplevolve/bin/python),$(AE_ROOT)/../.venvs/dplevolve/bin/python,python3)
WEB_DEMO_PYTHON ?= $(if $(wildcard $(AE_ROOT)/web-demo/.venv/bin/python),$(AE_ROOT)/web-demo/.venv/bin/python,$(DPL_EVOLVE_PYTHON))
THREADS ?= 10
TRACK ?= hpwl
FIGURE_SOURCE ?= retained
DSE_RUN_PREFIX ?=
TABLE4_DELTA_TOLERANCE_PP ?= 0.06
TABLE4_RUNTIME_RATIO_TOLERANCE ?= 0.20
ARIANE_DELTA_TOLERANCE_PP ?= 0.25
ARIANE_RUNTIME_RATIO_TOLERANCE ?= 0.20

-include env.local.sh

export AE_ROOT DPL_EVOLVE_AGENT_ROOT ORFS_ROOT DPL_EVOLVE_STATE_ROOT
export DPL_EVOLVE_PYTHON THREADS

.PHONY: help doctor bootstrap build-tools setup check prepare-paper-inputs
.PHONY: reviewer-prepare reviewer-aes-result reviewer-table6-one
.PHONY: validate-evaluator reproduce-default setup-bo reproduce-bo replay-reviewdse
.PHONY: plan-level1 reproduce-level1
.PHONY: reproduce-table4 summarize-table4 prepare-table5-inputs reproduce-table5 reproduce-table6
.PHONY: reproduce-figure4 reproduce-figure5 reproduce-figures
.PHONY: check-ariane-diagnostic-sources reproduce-ariane-diagnostic
.PHONY: reproduce-available-results reproduce-paper-results reproduce-paper-search
.PHONY: run-dse-small check-demo-models demo-reviewdse plan-demo-reviewdse
.PHONY: plan-dse-paper run-dse-paper summarize-dse-paper paper-data-check paper-data-check-available
.PHONY: fetch-table6-data table5-status check-table5-data check-table6-data
.PHONY: toolchain-smoke smoke smoke-check doctor-smoke
.PHONY: test test-structure test-integration test-unit test-web validate-configs
.PHONY: provenance zenodo zenodo-audit clean

.DEFAULT_GOAL := help

help:
	@echo "DPLEvolve — paper experiment reproduction"
	@echo ""
	@echo "Reviewer walkthrough (bounded fresh execution):"
	@echo "  make reviewer-prepare THREADS=8   Build tools, prepare AES, validate evaluator"
	@echo "  make reviewer-aes-result THREADS=8  Run AES default + one HPWL source replay"
	@echo "  make reviewer-table6-one THREADS=10 Fetch data + run one Table 6 row"
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
	@echo "  make prepare-table5-inputs  Rebuild Table 5 inputs with local 70/90/60 utilization"
	@echo "  make reproduce-table5       Prepare inputs + replay three retained programs"
	@echo "  make reproduce-table6       Replay 27 runs from exact cut-row DEF/V/SDC"
	@echo "  make reproduce-ariane-diagnostic  Replay six exact Ariane diagnostic sources"
	@echo "  make reproduce-figures      Redraw Figures 4/5 from retained author-run logs"
	@echo "  make reproduce-figures FIGURE_SOURCE=fresh DSE_RUN_PREFIX=NAME"
	@echo "                              Redraw Figures 4/5 from a fresh full DSE run"
	@echo "  make reproduce-available-results  Fixed-result validation in the configured Agent artifact"
	@echo "  make reproduce-paper-results  Complete fixed-result paper validation"
	@echo ""
	@echo "3. Exercise or rerun the LLM search itself:"
	@echo "  make plan-level1             Print the three-case Level 1 calibration"
	@echo "  make reproduce-level1 ACKNOWLEDGE_LLM_COST=yes"
	@echo "  make run-dse-small CASE=aes_nangate45  Real 1-Student/1-iteration run"
	@echo "  make check-demo-models       Probe the exact demo models; Student first"
	@echo "  make demo-reviewdse          Live Ariane133 terminal demo (4 Students x 2 iterations)"
	@echo "  make plan-demo-reviewdse     Inspect the configured demo launch"
	@echo "  make plan-dse-paper         Inspect the configured 9-case paper launch"
	@echo "  make run-dse-paper ACKNOWLEDGE_LLM_COST=yes"
	@echo "  make summarize-dse-paper DSE_RUN_PREFIX=NAME"
	@echo "  make reproduce-paper-search ACKNOWLEDGE_LLM_COST=yes"
	@echo ""
	@echo "Supporting checks (not paper reproduction):"
	@echo "  make fetch-table6-data      Download and verify retained Table 6 data"
	@echo "  make check-table6-data      Verify Table 6 DEF/V/SDC/source package"
	@echo "  make check-table5-data      Verify three checksummed Table 5 snapshots"
	@echo "  make paper-data-check       Run both external-data status checks"
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

reviewer-prepare:
	@$(MAKE) bootstrap
	@$(MAKE) build-tools THREADS="$(THREADS)"
	@$(MAKE) prepare-paper-inputs CASE=aes_nangate45 THREADS="$(THREADS)"
	@$(MAKE) validate-evaluator CASE=aes_nangate45 THREADS="$(THREADS)"

reviewer-aes-result:
	@$(MAKE) reproduce-default CASE=aes_nangate45 THREADS="$(THREADS)"
	@$(MAKE) replay-reviewdse CASE=aes_nangate45 TRACK=hpwl THREADS="$(THREADS)"

reviewer-table6-one:
	@$(MAKE) fetch-table6-data
	@$(MAKE) check-table6-data
	@$(MAKE) reproduce-table6 CASE=ariane133_placebatch \
	  PATTERN=center_band_8 ROLE=reviewdse THREADS="$(THREADS)"

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
	@$(MAKE) setup-bo
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
	  $(if $(DSE_RUN_PREFIX),--dse-summary "$(DPL_EVOLVE_STATE_ROOT)/experiment_batches/$(DSE_RUN_PREFIX)_paper9_place/table4-search.tsv",) \
	  --expected "$(ARTIFACTS_DIR)/01-table4-qor/expected/table4.json" \
	  --delta-tolerance-pp "$(TABLE4_DELTA_TOLERANCE_PP)" \
	  --runtime-ratio-tolerance "$(TABLE4_RUNTIME_RATIO_TOLERANCE)" \
	  --output "$(DPL_EVOLVE_STATE_ROOT)/paper_reproduction/table4/table4-fresh.tsv"

prepare-table5-inputs:
	@bash "$(REPRO_SCRIPTS)/prepare_table5_inputs.sh" --threads "$(THREADS)"

reproduce-table5:
	@bash "$(REPRO_SCRIPTS)/reproduce_table5.sh" --threads "$(THREADS)"

reproduce-table6:
	@bash "$(REPRO_SCRIPTS)/reproduce_table6.sh" --threads "$(THREADS)" \
	  $(if $(CASE),--case "$(CASE)",) \
	  $(if $(PATTERN),--pattern "$(PATTERN)",) \
	  $(if $(ROLE),--only-role "$(ROLE)",)

reproduce-figure4:
	@"$(DPL_EVOLVE_PYTHON)" "$(REPRO_SCRIPTS)/reproduce_figures.py" figure4 \
	  --source "$(FIGURE_SOURCE)" \
	  --artifact-root "$(AE_ROOT)" \
	  --state-root "$(DPL_EVOLVE_STATE_ROOT)" \
	  --run-prefix "$(DSE_RUN_PREFIX)" \
	  --output-dir "$(DPL_EVOLVE_STATE_ROOT)/paper_reproduction/figures/$(FIGURE_SOURCE)"

reproduce-figure5:
	@"$(DPL_EVOLVE_PYTHON)" "$(REPRO_SCRIPTS)/reproduce_figures.py" figure5 \
	  --source "$(FIGURE_SOURCE)" \
	  --artifact-root "$(AE_ROOT)" \
	  --state-root "$(DPL_EVOLVE_STATE_ROOT)" \
	  --run-prefix "$(DSE_RUN_PREFIX)" \
	  --flow-variant paper9_place \
	  --output-dir "$(DPL_EVOLVE_STATE_ROOT)/paper_reproduction/figures/$(FIGURE_SOURCE)"

reproduce-figures: reproduce-figure4 reproduce-figure5

check-ariane-diagnostic-sources:
	@bash "$(REPRO_SCRIPTS)/reproduce_ariane_diagnostic.sh" --check-sources

reproduce-ariane-diagnostic:
	@$(MAKE) reproduce-default CASE=ariane133_nangate45 THREADS="$(THREADS)"
	@ARIANE_DELTA_TOLERANCE_PP="$(ARIANE_DELTA_TOLERANCE_PP)" \
	 ARIANE_RUNTIME_RATIO_TOLERANCE="$(ARIANE_RUNTIME_RATIO_TOLERANCE)" \
	 bash "$(REPRO_SCRIPTS)/reproduce_ariane_diagnostic.sh" --threads "$(THREADS)"

reproduce-available-results: paper-data-check-available
	@$(MAKE) reproduce-table4 THREADS="$(THREADS)"
	@$(MAKE) reproduce-table5 THREADS="$(THREADS)"
	@$(MAKE) reproduce-table6 THREADS="$(THREADS)"
	@$(MAKE) reproduce-ariane-diagnostic THREADS="$(THREADS)"
	@$(MAKE) reproduce-figures FIGURE_SOURCE=retained

# Complete fixed-result validation. Check external data before spending the
# 3,600-run BO budget so an incomplete package fails early. This replay is not
# an alternative to the configured Teacher--Student method.
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

fetch-table6-data:
	@bash "$(REPRO_SCRIPTS)/fetch_table6_data.sh"

check-table5-data:
	@bash "$(REPRO_SCRIPTS)/reproduce_table5.sh" --check-paper-data

table5-status:
	@bash "$(REPRO_SCRIPTS)/reproduce_table5.sh" --check-paper-data

check-table6-data:
	@bash "$(REPRO_SCRIPTS)/reproduce_table6.sh" --check-paper-data

plan-level1:
	@bash "$(REPRO_SCRIPTS)/run_level1.sh" --threads "$(THREADS)" --children "$(or $(LEVEL1_CHILDREN),50)" --dry-run

reproduce-level1:
	@ACKNOWLEDGE_LLM_COST="$(ACKNOWLEDGE_LLM_COST)" bash "$(REPRO_SCRIPTS)/run_level1.sh" --threads "$(THREADS)" --children "$(or $(LEVEL1_CHILDREN),50)"

run-dse-small:
	@DSE_RUN_PREFIX="$(DSE_RUN_PREFIX)" \
	 TEACHER_MODEL="$(TEACHER_MODEL)" \
	 TEACHER_REASONING_EFFORT="$(TEACHER_REASONING_EFFORT)" \
	 STUDENT_MODEL="$(STUDENT_MODEL)" \
	 STUDENT_REASONING_EFFORT="$(STUDENT_REASONING_EFFORT)" \
	 bash "$(REPRO_SCRIPTS)/run_dse.sh" \
	  --profile small --case "$(or $(CASE),aes_nangate45)" --threads "$(THREADS)" \
	  --children "$(or $(STUDENTS),1)" --iterations "$(or $(ITERATIONS),1)"

check-demo-models:
	@bash "$(AE_ROOT)/scripts/demo/run_reviewdse_demo.sh" \
	  --teacher-model "$(or $(TEACHER_MODEL),gpt-5.6-sol)" \
	  --teacher-reasoning-effort "$(or $(TEACHER_REASONING_EFFORT),xhigh)" \
	  --student-model "$(or $(STUDENT_MODEL),gpt-5.6-terra)" \
	  --student-reasoning-effort "$(or $(STUDENT_REASONING_EFFORT),xhigh)" \
	  --check-models-only

demo-reviewdse:
	@DSE_RUN_PREFIX="$(DSE_RUN_PREFIX)" bash "$(AE_ROOT)/scripts/demo/run_reviewdse_demo.sh" \
	  --case "$(or $(CASE),ariane133_nangate45)" \
	  --students "$(or $(STUDENTS),4)" \
	  --iterations "$(or $(ITERATIONS),2)" \
	  --threads "$(THREADS)" \
	  --teacher-model "$(or $(TEACHER_MODEL),gpt-5.6-sol)" \
	  --teacher-reasoning-effort "$(or $(TEACHER_REASONING_EFFORT),xhigh)" \
	  --student-model "$(or $(STUDENT_MODEL),gpt-5.6-terra)" \
	  --student-reasoning-effort "$(or $(STUDENT_REASONING_EFFORT),xhigh)"

plan-demo-reviewdse:
	@DSE_RUN_PREFIX="$(DSE_RUN_PREFIX)" bash "$(AE_ROOT)/scripts/demo/run_reviewdse_demo.sh" \
	  --case "$(or $(CASE),ariane133_nangate45)" \
	  --students "$(or $(STUDENTS),4)" \
	  --iterations "$(or $(ITERATIONS),2)" \
	  --threads "$(THREADS)" \
	  --teacher-model "$(or $(TEACHER_MODEL),gpt-5.6-sol)" \
	  --teacher-reasoning-effort "$(or $(TEACHER_REASONING_EFFORT),xhigh)" \
	  --student-model "$(or $(STUDENT_MODEL),gpt-5.6-terra)" \
	  --student-reasoning-effort "$(or $(STUDENT_REASONING_EFFORT),xhigh)" \
	  --dry-run

plan-dse-paper:
	@DSE_RUN_PREFIX="$(DSE_RUN_PREFIX)" bash "$(REPRO_SCRIPTS)/run_dse.sh" --profile paper --threads "$(THREADS)" --dry-run

run-dse-paper:
	@ACKNOWLEDGE_LLM_COST="$(ACKNOWLEDGE_LLM_COST)" DSE_RUN_PREFIX="$(DSE_RUN_PREFIX)" bash "$(REPRO_SCRIPTS)/run_dse.sh" --profile paper --threads "$(THREADS)"

summarize-dse-paper:
	@if [[ -z "$(DSE_RUN_PREFIX)" ]]; then echo "[ERROR] DSE_RUN_PREFIX is required" >&2; exit 2; fi
	@"$(DPL_EVOLVE_PYTHON)" "$(REPRO_SCRIPTS)/summarize_dse_campaign.py" \
	  --batch-root "$(DPL_EVOLVE_STATE_ROOT)/experiment_batches/$(DSE_RUN_PREFIX)_paper9_place" \
	  --orfs-root "$(ORFS_ROOT)" \
	  --expected "$(ARTIFACTS_DIR)/01-table4-qor/expected/table4.json" \
	  --output "$(DPL_EVOLVE_STATE_ROOT)/experiment_batches/$(DSE_RUN_PREFIX)_paper9_place/table4-search.tsv" \
	  --audit-output "$(DPL_EVOLVE_STATE_ROOT)/experiment_batches/$(DSE_RUN_PREFIX)_paper9_place/candidate-eligibility-audit.json"

paper-data-check-available:
	@$(MAKE) check-table6-data
	@$(MAKE) check-ariane-diagnostic-sources

toolchain-smoke smoke:
	@echo "[NOTICE] AES smoke is a toolchain exercise, not a paper reproduction."
	@bash "$(AE_ROOT)/tests/toolchain/aes-smoke/run.sh" --run --threads "$(THREADS)"

smoke-check:
	@bash "$(AE_ROOT)/tests/toolchain/aes-smoke/run.sh" --check-only

test: test-structure test-integration test-unit test-web
	@echo ""
	@echo "[PASS] All repository tests passed"

test-structure:
	@bash "$(AE_ROOT)/tests/artifact/test_ae_structure.sh"

test-integration:
	@bash "$(AE_ROOT)/tests/integration/test_smoke_pipeline.sh"

test-unit:
	@"$(DPL_EVOLVE_PYTHON)" -m pytest "$(AE_ROOT)/tests/unit/" -v 2>/dev/null || \
	 PYTHONPATH="$(AE_ROOT)" "$(DPL_EVOLVE_PYTHON)" -m unittest discover -s "$(AE_ROOT)/tests/unit/" -v

test-web:
	@PYTHONPATH="$(AE_ROOT)" "$(WEB_DEMO_PYTHON)" -m unittest \
	  discover -s "$(AE_ROOT)/web-demo/tests/" -v

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
	@echo "Removing generated artifact scratch outputs only..."
	@find "$(ARTIFACTS_DIR)" -type d -name output -exec \
	  find {} -mindepth 1 ! -name .gitkeep -delete \;
	@echo "Fresh reproduction state under $(DPL_EVOLVE_STATE_ROOT) was preserved."
