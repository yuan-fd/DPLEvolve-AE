# Native Artifact Prose Revision Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Rewrite the Artifact Appendix and artifact submission draft in concise, idiomatic academic English while preserving all reproducibility facts and the two-page Appendix layout.

**Architecture:** The paper Appendix remains the compact reviewer-facing document, and the repository submission draft remains the expanded operational version. Shared facts use the same terminology, while exact revisions and internal source paths stay in the README and provenance documentation.

**Tech Stack:** LaTeX with `acmart`, Markdown, GNU Make, `latexmk`, Poppler PDF tools, Git, and the existing Zenodo packaging scripts.

---

### Task 1: Record the factual and layout baseline

**Files:**
- Inspect: `../ARTICLE/artifact_evaluation.tex:529`
- Inspect: `docs/artifact-submission-draft.md:1`
- Inspect: `README.md:1`
- Inspect: `provenance/source-commits.json:1`

- [ ] **Step 1: Extract the current Appendix and submission draft**

Run:

```bash
sed -n '529,780p' ../ARTICLE/artifact_evaluation.tex
sed -n '1,260p' docs/artifact-submission-draft.md
```

Expected: the output contains the DOI `10.5281/zenodo.21629308`, the three
Table 5 source mappings, the Table 4--6 expected results, and both algorithms.

- [ ] **Step 2: Record the facts that must remain unchanged**

Check the following values against both documents:

```text
Paper models: one gpt-5.5 Teacher and four gpt-5.4 Students, xhigh
AE models: gpt-5.6-sol Teacher and gpt-5.6-terra Student, xhigh
Paper search period: April--May 2026
Table 5 utilization: AES 70, JPEG 90, SWERV 60
Table 5 mappings: LEGALM/Diamond, Negotiation/Negotiation, Diamond/Negotiation
Table 6 jobs: 27
Runtime gate: 2x
DOI: 10.5281/zenodo.21629308
```

Expected: each value is present and consistent before editing.

- [ ] **Step 3: Record the current PDF layout**

Run:

```bash
pdfinfo ../ARTICLE/artifact_evaluation.pdf | rg '^(Pages|Page size):'
pdftotext -f 8 -l 9 -layout ../ARTICLE/artifact_evaluation.pdf - | sed -n '1,260p'
```

Expected: the paper has nine letter-sized pages, and the Artifact Appendix
occupies pages 8 and 9.

### Task 2: Rewrite the two-page Artifact Appendix

**Files:**
- Modify: `../ARTICLE/artifact_evaluation.tex:529-760`

- [ ] **Step 1: Rewrite the contributor note, abstract, and checklist**

Use complete sentences after every checklist label. Keep the labels for
scanability, but replace fragments such as:

```text
Pinned OpenROAD, ORFS, Yosys, OpenSTA, GCC/G++ 9+, and CMake 3.16+.
```

with direct sentences of this form:

```text
The artifact builds pinned versions of OpenROAD, ORFS, Yosys, and OpenSTA
from source and requires GCC/G++ 9 or later and CMake 3.16 or later.
```

State model access, data coverage, hardware, cost, licensing, and output
coverage without promotional adjectives or sentence fragments.

- [ ] **Step 2: Rewrite the description without low-level identifiers**

Replace exact OpenROAD commit hashes, provenance filenames, and prompt source
paths with one compact statement:

```text
The repository README records the pinned OpenROAD, ORFS, Yosys, and
yosys-slang revisions and gives the complete setup procedure.
```

Keep the Table 5 utilization values and source mappings because reviewers need
them to interpret Table 5. Keep the model dates and retry policy, but describe
them in ordinary prose.

- [ ] **Step 3: Rewrite the algorithm explanations**

Leave both pseudocode blocks unchanged. Rewrite the surrounding prose so that
it explains, in order, what calibration freezes, what each Student returns,
how the Teacher uses the evidence, and why the packet remains read-only during
target search. Use `Teacher` and `Student` only for the named ReviewDSE roles.

- [ ] **Step 4: Rewrite installation and evaluation prose**

Keep every command unchanged. Replace formulaic statements about alternative
paths with direct scope statements:

```text
The functional check verifies model access and replays the reported results.
The full stochastic search remains available as a separate, cost-gated run.
```

Write each expected-result bullet as a complete sentence. Preserve every
reported percentage, tolerance, row count, and legality count.

- [ ] **Step 5: Rewrite integrity, customization, and methodology text**

Explain failure behavior and customization in complete sentences. Introduce
the methodology links with a full sentence. Do not add revision hashes,
internal implementation paths, or claims that a live closed-loop run is part
of the Functional check.

- [ ] **Step 6: Run a prose and identifier scan**

Run:

```bash
sed -n '529,780p' ../ARTICLE/artifact_evaluation.tex \
  | rg -n 'd5ff63|d14d526|provenance/source-commits|prompt_templates|teacher_loop'
```

Expected: no matches.

### Task 3: Rewrite the synchronized submission draft

**Files:**
- Modify: `docs/artifact-submission-draft.md:1-220`

- [ ] **Step 1: Rewrite the artifact abstract**

Use the same terminology as the Appendix. Describe what the artifact contains,
which paper results it reproduces, why model access is required, how fresh
results are checked, and which platform was tested. Use full sentences and
retain the DOI and live repository URL.

- [ ] **Step 2: Rewrite identification, requirements, and installation**

Keep the paper title, DOI, repository link, license, tested machine, required
software, and installation commands. Remove internal revision paths and hashes;
direct the reader to the README for exact tool revisions.

- [ ] **Step 3: Rewrite the evaluation workflow**

Retain the commands and numerical acceptance criteria for Tables 4--6,
ReviewDSE search, Figures 4--5, and the Ariane diagnostic. Use complete
sentences to distinguish fixed-result replay from a fresh stochastic search
without presenting a non-LLM method path.

- [ ] **Step 4: Rewrite interpretation and limitations**

State the metric contract, Web Demo behavior, Level 1 reconstruction, model
stochasticity, and cross-host tolerance directly. Avoid phrases such as
“shown for completeness,” “does not replace,” and repeated uses of “retained.”

- [ ] **Step 5: Check synchronization and low-level detail**

Run:

```bash
rg -n 'd5ff63|d14d526|provenance/source-commits|prompt_templates|teacher_loop' \
  docs/artifact-submission-draft.md
rg -n 'gpt-5.5|gpt-5.4|gpt-5.6-sol|gpt-5.6-terra|70/90/60|21629308' \
  docs/artifact-submission-draft.md ../ARTICLE/artifact_evaluation.tex
```

Expected: the first command has no matches; the second command confirms that
the shared model, utilization, and DOI facts remain present.

### Task 4: Compile and fit the Appendix to two pages

**Files:**
- Modify if needed: `../ARTICLE/artifact_evaluation.tex:529-760`
- Update: `../ARTICLE/artifact_evaluation.pdf`

- [ ] **Step 1: Compile the paper**

Run:

```bash
cd ../ARTICLE
latexmk -pdf -interaction=nonstopmode artifact_evaluation.tex
```

Expected: `latexmk` exits successfully and produces a nine-page PDF.

- [ ] **Step 2: Check page count and LaTeX diagnostics**

Run:

```bash
pdfinfo artifact_evaluation.pdf | rg '^(Pages|Page size):'
rg -n 'LaTeX Error|Undefined control sequence|Fatal error' artifact_evaluation.log
```

Expected: the PDF has nine letter-sized pages, and the log scan has no matches.

- [ ] **Step 3: Render and inspect pages 8 and 9**

Run:

```bash
render_dir=$(mktemp -d /tmp/dplevolve-native-prose.XXXXXX)
pdftoppm -f 8 -l 9 -png -r 130 artifact_evaluation.pdf "$render_dir/page"
```

Expected: page 8 is substantially filled, neither algorithm is split, both
columns are readable, and all Appendix material ends on page 9.

- [ ] **Step 4: Tighten prose if the layout exceeds two pages**

Shorten sentences and remove repeated explanations before changing font size,
spacing, or margins. Re-run Steps 1--3 until the Appendix fits pages 8 and 9.

### Task 5: Synchronize the PDF and release metadata

**Files:**
- Update: `paper/artifact_evaluation.pdf`
- Modify: `paper/README.md:1-12`
- Modify: `configs/reproduction/paper-experiments.json:1-8`

- [ ] **Step 1: Copy the compiled PDF into the artifact repository**

Run:

```bash
cp ../ARTICLE/artifact_evaluation.pdf paper/artifact_evaluation.pdf
sha256sum ../ARTICLE/artifact_evaluation.pdf paper/artifact_evaluation.pdf
```

Expected: both files have the same SHA-256 value.

- [ ] **Step 2: Update the recorded PDF checksum**

Replace the previous PDF SHA-256 in `paper/README.md` and
`configs/reproduction/paper-experiments.json` with the value reported in Step 1.

- [ ] **Step 3: Run repository validation**

Run:

```bash
make test
make validate-configs
make check-table5-data
make check-table6-data
```

Expected: all unit and Web tests pass, all configurations validate, and both
Table 5 and Table 6 data checks pass.

### Task 6: Rebuild and verify the Zenodo archive

**Files:**
- Generate: the timestamped archive reported by `make zenodo`

- [ ] **Step 1: Build the archive**

Run:

```bash
make zenodo
```

Expected: the command reports a 204 MB archive and a SHA-256 value.

- [ ] **Step 2: Verify a clean extraction**

Extract the reported archive into a new temporary directory, verify its
`MANIFEST.sha256`, and run:

```bash
make test
make validate-configs
make check-table5-data
make check-table6-data
```

Expected: the manifest and all four commands pass without relying on the source
working tree.

- [ ] **Step 3: Copy the verified archive to a stable workspace path**

Run:

```bash
archive_path="$(ls -t /tmp/DPLEvolve-AE-zenodo-*.tar.gz | head -1)"
cp "$archive_path" \
  ../DPLEvolve-AE-zenodo-20260728-native-prose.tar.gz
sha256sum ../DPLEvolve-AE-zenodo-20260728-native-prose.tar.gz
```

Expected: the stable copy has the same SHA-256 value as the verified archive.

### Task 7: Review and commit the final changes

**Files:**
- Review: all files modified in Tasks 2--6

- [ ] **Step 1: Review diffs and unrelated files**

Run:

```bash
git -C ../ARTICLE status --short --branch
git status --short --branch
git -C ../ARTICLE diff --check
git diff --check
```

Expected: only the intended Appendix, submission draft, PDF, and checksum files
are modified. Existing untracked `MLCAD-submission` files remain unstaged.

- [ ] **Step 2: Commit the ARTICLE changes**

Run:

```bash
git -C ../ARTICLE add artifact_evaluation.tex artifact_evaluation.pdf
git -C ../ARTICLE commit -m "Polish artifact appendix prose"
```

Expected: the commit contains only the LaTeX source and compiled PDF.

- [ ] **Step 3: Commit the artifact repository changes**

Run:

```bash
git add docs/artifact-submission-draft.md paper/artifact_evaluation.pdf \
  paper/README.md configs/reproduction/paper-experiments.json
git commit -m "Polish artifact evaluation prose"
```

Expected: the commit contains the synchronized draft, PDF, and checksum
metadata. Do not push either repository unless the user requests it.
