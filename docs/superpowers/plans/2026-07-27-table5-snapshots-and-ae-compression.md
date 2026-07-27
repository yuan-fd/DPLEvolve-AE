# Table 5 Snapshots and AE Compression Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the stale Table 5 six-tree/config recovery gate with three tracked source snapshots and compress the Artifact Appendix to two pages.

**Architecture:** Table 5 keeps one immutable source snapshot per legalizer implementation and resolves each selected/reference role through an explicit mapping. Input density overrides are applied only by the Table 5 input script. Existing build/evaluation machinery consumes the mapped source directories unchanged, while release tests and the Zenodo audit verify snapshot integrity.

**Tech Stack:** Bash, Python `unittest`, SHA-256 manifests, GNU Make, rsync, LaTeX/acmart.

---

### Task 1: Replace the stale Table 5 contract in tests

**Files:**
- Modify: `tests/unit/test_reproduction_contract.py:222`
- Modify: `tests/integration/test_smoke_pipeline.sh:74`
- Modify: `tests/artifact/test_ae_structure.sh:45`

- [ ] **Step 1: Write the failing unit contract tests**

Replace the combined missing-data test so it applies only to Table 6. Replace
`test_table5_uses_retained_recipes_and_refuses_a_fake_swerv_substitute` with a
test that requires:

```python
def test_table5_uses_three_snapshots_and_local_density_overrides(self):
    prepare = (ROOT / "scripts/reproduce/prepare_table5_inputs.sh").read_text()
    for fragment in (
        "aes_dense_nangate45 DENSE 70",
        "jpeg_util90_nangate45 DENSE 90",
        "swerv_wrapper_nangate45 DENSE_2 60",
    ):
        self.assertIn(fragment, prepare)
    self.assertNotIn("config_dense2.mk", prepare)
    runner = (ROOT / "scripts/reproduce/reproduce_table5.sh").read_text()
    for program in ("legalm", "diamond", "negotiation"):
        self.assertIn(f"programs/{program}/dpl_evolve", runner)
    for mapping in (
        "aes_dense_n45:selected) echo legalm",
        "aes_dense_n45:reference) echo diamond",
        "jpeg_dense_n45:selected|jpeg_dense_n45:reference) echo negotiation",
        "swerv_dense_n45:selected) echo diamond",
        "swerv_dense_n45:reference) echo negotiation",
    ):
        self.assertIn(mapping, runner)
    self.assertNotIn("PAPER_DATA_ROOT}/table5", runner)
```

Add a test that runs the manifest verifier against the tracked program root:

```python
def test_table5_program_snapshots_are_fully_checksummed(self):
    self.run_python(
        "verify_data_manifest.py",
        "--root", ROOT / "artifacts/02-table5-composability",
        "--scope", "programs",
    )
```

- [ ] **Step 2: Update structure and integration expectations**

Require these paths in `test_ae_structure.sh`:

```text
artifacts/02-table5-composability/programs/MANIFEST.sha256
artifacts/02-table5-composability/programs/legalm/dpl_evolve/CMakeLists.txt
artifacts/02-table5-composability/programs/diamond/dpl_evolve/CMakeLists.txt
artifacts/02-table5-composability/programs/negotiation/dpl_evolve/CMakeLists.txt
```

Replace the integration block that expects Table 5 `BLOCKED` with:

```bash
table5_dry_run="$(bash scripts/reproduce/reproduce_table5.sh --dry-run)"
grep -F 'programs/legalm/dpl_evolve' <<<"${table5_dry_run}" >/dev/null
grep -F 'programs/diamond/dpl_evolve' <<<"${table5_dry_run}" >/dev/null
grep -F 'programs/negotiation/dpl_evolve' <<<"${table5_dry_run}" >/dev/null
```

- [ ] **Step 3: Run the focused tests and verify RED**

Run:

```bash
python3 -m unittest \
  tests.unit.test_reproduction_contract.ReproductionContractTests.test_table5_uses_three_snapshots_and_local_density_overrides \
  tests.unit.test_reproduction_contract.ReproductionContractTests.test_table5_program_snapshots_are_fully_checksummed -v
bash tests/artifact/test_ae_structure.sh
```

Expected: failures for the old utilization values, missing `programs` scope,
and absent snapshot paths.

### Task 2: Snapshot and checksum the three source programs

**Files:**
- Create: `artifacts/02-table5-composability/programs/legalm/dpl_evolve/**`
- Create: `artifacts/02-table5-composability/programs/diamond/dpl_evolve/**`
- Create: `artifacts/02-table5-composability/programs/negotiation/dpl_evolve/**`
- Create: `artifacts/02-table5-composability/programs/MANIFEST.sha256`
- Modify: `scripts/reproduce/verify_data_manifest.py:25`

- [ ] **Step 1: Extend the manifest verifier for tracked programs**

Change the scope choices to:

```python
parser.add_argument(
    "--scope", required=True, choices=("table5", "table6", "programs")
)
```

Keep the existing path-containment, symlink, unsigned-file, missing-file, and
digest checks unchanged.

- [ ] **Step 2: Copy the retained source snapshots mechanically**

Run from the AE repository root:

```bash
mkdir -p artifacts/02-table5-composability/programs/{legalm,diamond,negotiation}
rsync -a --delete --exclude='.git/' \
  ../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/framework_dpl_evolve/ \
  artifacts/02-table5-composability/programs/legalm/dpl_evolve/
rsync -a --delete --exclude='.git/' \
  ../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/diamond_dpl_evolve/ \
  artifacts/02-table5-composability/programs/diamond/dpl_evolve/
rsync -a --delete --exclude='.git/' \
  ../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/default_negotiation_dpl_evolve/ \
  artifacts/02-table5-composability/programs/negotiation/dpl_evolve/
```

- [ ] **Step 3: Generate the deterministic snapshot manifest**

Run:

```bash
cd artifacts/02-table5-composability
find programs -type f ! -name MANIFEST.sha256 -print0 \
  | sort -z | xargs -0 sha256sum > programs/MANIFEST.sha256
cd ../..
python3 scripts/reproduce/verify_data_manifest.py \
  --root artifacts/02-table5-composability --scope programs
```

Expected: `[PASS]` with every regular snapshot file enumerated.

- [ ] **Step 4: Run the focused manifest and structure tests**

Run:

```bash
python3 -m unittest \
  tests.unit.test_reproduction_contract.ReproductionContractTests.test_table5_program_snapshots_are_fully_checksummed -v
bash tests/artifact/test_ae_structure.sh
```

Expected: snapshot-specific assertions pass; the old runner contract test
still fails until Task 3.

### Task 3: Map Table 5 roles to snapshots and localize input overrides

**Files:**
- Modify: `scripts/reproduce/reproduce_table5.sh:8`
- Modify: `scripts/reproduce/prepare_table5_inputs.sh:8`
- Modify: `artifacts/02-table5-composability/reproduce.sh:2`
- Modify: `configs/reproduction/table5-sources.tsv:1`
- Modify: `configs/reproduction/table5-inputs.tsv:1`
- Modify: `Makefile:70`

- [ ] **Step 1: Remove the standalone SWERV configuration path**

Delete `SWERV_DESIGN_CONFIG`, its CLI option, and the direct ORFS `make` call
from `prepare_table5_inputs.sh`. Use the common `run_input` helper for exactly:

```bash
run_input aes_dense_nangate45 DENSE 70
run_input jpeg_util90_nangate45 DENSE 90
run_input swerv_wrapper_nangate45 DENSE_2 60
```

These environment overrides remain inside this script and do not edit any
tracked ORFS configuration.

- [ ] **Step 2: Replace six external tree checks with one tracked manifest**

Define:

```bash
TABLE5_ARTIFACT_ROOT="${AE_ROOT}/artifacts/02-table5-composability"
TABLE5_PROGRAM_ROOT="${TABLE5_ARTIFACT_ROOT}/programs"
```

Run `verify_data_manifest.py --root "${TABLE5_ARTIFACT_ROOT}" --scope programs`
before handling check mode or EDA work. Add:

```bash
program_for_row_role() {
  case "$1:$2" in
    aes_dense_n45:selected) echo legalm ;;
    aes_dense_n45:reference) echo diamond ;;
    jpeg_dense_n45:selected|jpeg_dense_n45:reference) echo negotiation ;;
    swerv_dense_n45:selected) echo diamond ;;
    swerv_dense_n45:reference) echo negotiation ;;
    *) repro_die "unknown Table 5 row/role: $1/$2" ;;
  esac
}
```

Use `${TABLE5_PROGRAM_ROOT}/${program}/dpl_evolve` as `--candidate-src`.
`--check-paper-data` must return success after reporting three checksummed
snapshots. Keep `--check-inputs` for regenerated ODB/SDC checks.

- [ ] **Step 3: Expose dry-run through the artifact wrapper**

Add `--dry-run` handling to
`artifacts/02-table5-composability/reproduce.sh` and forward it to the main
runner. Its help text must describe three retained snapshots.

- [ ] **Step 4: Update source/input TSV contracts and Make help**

Record the six role mappings with snapshot ids and `available` status in
`table5-sources.tsv`. Record utilization values `70`, `90`, and `60`, using the
standard pinned design configs and `available` status, in `table5-inputs.tsv`.
Remove help text about DENSE_2 recovery, six source trees, and Table 5 exclusion.
Make `table5-status` directly invoke the passing check.

- [ ] **Step 5: Run focused tests and verify GREEN**

Run:

```bash
python3 -m unittest \
  tests.unit.test_reproduction_contract.ReproductionContractTests.test_table5_uses_three_snapshots_and_local_density_overrides \
  tests.unit.test_reproduction_contract.ReproductionContractTests.test_table5_program_snapshots_are_fully_checksummed -v
make check-table5-data
bash scripts/reproduce/reproduce_table5.sh --dry-run
bash tests/integration/test_smoke_pipeline.sh
```

Expected: all pass; dry-run prints six candidate executions sourced from only
the three tracked directories.

### Task 4: Synchronize release documentation and Zenodo checks

**Files:**
- Modify: `README.md:81`
- Modify: `artifacts/02-table5-composability/README.md:1`
- Modify: `docs/reviewer-walkthrough.md:151`
- Modify: `docs/requirements.md:86`
- Modify: `docs/paper-data-layout.md:50`
- Modify: `docs/artifact-appendix.md:1`
- Modify: `docs/artifact-submission-draft.md:7`
- Modify: `docs/table5-status.md:1`
- Modify: `docs/troubleshooting.md:92`
- Modify: `configs/reproduction/paper-experiments.json`
- Modify: `.zenodo.json:2`
- Modify: `scripts/maintenance/prepare_zenodo.sh:30`

- [ ] **Step 1: Replace stale recovery language**

State consistently that Table 5 reuses three retained implementations and
that the input generator applies Table-5-local 70/90/60 overrides. Document
`make check-table5-data` followed by `make reproduce-table5 THREADS=10`, the
fresh `table5-fresh.tsv` output, and the three selected/reference pairs. Remove
claims that Table 5 is blocked, missing six trees, or needs `config_dense2.mk`.

- [ ] **Step 2: Require snapshots in the Zenodo archive**

Add the program manifest to `required_paths`, run `make check-table5-data`
before repository tests, describe Table 5 as runnable in `README-ZENODO.md`,
and require the manifest plus all three `CMakeLists.txt` paths in the tar
listing check.

- [ ] **Step 3: Add a stale-language regression assertion**

In `test_reproduction_contract.py`, scan release-facing files and fail if a
Table 5 paragraph contains `six source trees`, `config_dense2.mk`, or a
Table-5-specific `BLOCKED` claim.

- [ ] **Step 4: Run documentation, release, and full repository tests**

Run:

```bash
rg -n -i 'Table 5.*BLOCKED|six (complete )?source trees|config_dense2\.mk' \
  README.md artifacts docs configs .zenodo.json scripts/maintenance/prepare_zenodo.sh
make test
make validate-configs
make zenodo-audit
```

Expected: the stale-language search returns no release-facing Table 5 claim;
all tests pass; the audit archive contains all three snapshots.

### Task 5: Compress the Artifact Appendix to two pages

**Files:**
- Modify: `../ARTICLE/artifact_evaluation.tex:528`

- [ ] **Step 1: Condense the checklist without dropping required fields**

Within the Appendix group, use `\footnotesize`, compact list spacing, and
combine related entries into compact bullets for algorithm/program, tools and
build, models/prompts/access, data, environment/hardware, metrics/output,
execution/time/cost, licenses, workflow, and Zenodo. Preserve all factual
values currently present.

- [ ] **Step 2: Remove duplicated prose around the two algorithms**

Keep both algorithms and their labels unchanged. Reduce their prose to one
introductory paragraph and one short validation paragraph after each block.
Use `\scriptsize` inside the algorithm bodies and compact local float spacing.

- [ ] **Step 3: Compact installation, workflow, and expected results**

Keep `make check-demo-models` mandatory. Keep the Table 4, Table 5, Table 6,
Figures 4/5, and full-search commands, outputs, paper values, tolerances, and
validation modes. Merge repeated statements and describe Table 5 as three
snapshots with 70/90/60 Table-5-local inputs.

- [ ] **Step 4: Force-build and inspect page count**

Run from `../ARTICLE`:

```bash
latexmk -gg -pdf -interaction=nonstopmode -halt-on-error artifact_evaluation.tex
pdfinfo artifact_evaluation.pdf | rg '^Pages:'
```

Expected: `Pages: 9`. If it remains 10 pages, shorten repeated prose and local
vertical whitespace only; do not delete required checklist fields or either
algorithm.

- [ ] **Step 5: Visually inspect Appendix pages 8 and 9**

Render pages 8--9 with `pdftoppm`, inspect both pages, and confirm headings,
algorithms, commands, and expected-result paragraphs follow the intended
two-column reading order without clipping.

### Task 6: Final cross-repository verification

**Files:**
- Verify: all files changed above

- [ ] **Step 1: Run artifact verification**

```bash
make check-table5-data
bash scripts/reproduce/reproduce_table5.sh --dry-run
make test
make validate-configs
make zenodo-audit
git diff --check
```

Expected: every command exits zero, and the Table 5 check reports three
available checksummed snapshots.

- [ ] **Step 2: Run paper verification**

```bash
latexmk -gg -pdf -interaction=nonstopmode -halt-on-error artifact_evaluation.tex
pdfinfo artifact_evaluation.pdf | rg '^Pages:'
rg -n -e 'LaTeX Warning:.*undefined' -e '! LaTeX Error' \
  -e 'Overfull \\hbox' artifact_evaluation.log
git diff --check -- artifact_evaluation.tex
```

Expected: nine pages, no undefined references or LaTeX errors, and no overfull
box originating in the Appendix line range.

- [ ] **Step 3: Review repository state**

Confirm the DPLEvolve-AE diff contains only Table 5 snapshots/contracts/docs,
release checks, tests, and plan files. Confirm the ARTICLE diff contains only
the requested Appendix work and preserves pre-existing user files.
