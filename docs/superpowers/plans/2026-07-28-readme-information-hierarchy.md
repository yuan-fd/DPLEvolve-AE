# README Information Hierarchy Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Give the repository a clear opening description and move all author and artifact-contributor information to the end of the root README.

**Architecture:** Make one focused documentation edit in `README.md`. Preserve the operational sections and links, replace the one-line opening with a complete repository introduction, remove the opening contact line, and add a closing credits section sourced from `CITATION.cff` and `.zenodo.json`.

**Tech Stack:** GitHub-flavored Markdown, shell-based content checks, repository test suite

---

### Task 1: Reorganize the root README

**Files:**
- Modify: `README.md:1-16`
- Modify: `README.md` after the `Web Demo` section

- [ ] **Step 1: Record the current hierarchy checks**

Run:

```bash
awk '/^## Code Structure/{exit} {print}' README.md | rg 'Artifact contact'
rg -n '^## Authors and Artifact Evaluation Contributor$' README.md
```

Expected: the first command finds the opening contact line, and the second
command returns no match.

- [ ] **Step 2: Replace the opening summary and remove the contact line**

Use this paragraph directly below the title:

```markdown
This repository contains the artifact for *From Tool Invocation to
Source-Mechanism Exploration: Protected White-Box DSE for Open-Source EDA* and
the implementation of ReviewDSE, a protected white-box design-space
exploration framework for OpenROAD detailed placement. It provides the source
programs, experiment configurations, protected evaluator, and reproduction
workflows for Tables 4--6 and Figures 4--5. The artifact uses a pinned
open-source EDA toolchain and requires authenticated Codex access for the
configured Teacher and Student agents.
```

Keep the four existing resource links and workflow image immediately after the
paragraph. Delete the opening `Artifact contact` paragraph.

- [ ] **Step 3: Add the closing credits section**

Append the following content after the Web Demo instructions:

```markdown
## Authors and Artifact Evaluation Contributor

### Paper Authors

- **Zhiyu Zheng** — Fudan University
- **Yiming Du** — Fudan University
- **Ziyi Wang** — The Chinese University of Hong Kong
- **Zhiang Wang** — Fudan University

### Artifact Evaluation Contributor

- **Wenjie Yuan** — Fudan University
  ([25303060069@m.fudan.edu.cn](mailto:25303060069@m.fudan.edu.cn))
```

- [ ] **Step 4: Verify the information hierarchy and metadata**

Run:

```bash
! awk '/^## Code Structure/{exit} {print}' README.md | rg -q 'Artifact contact|Wenjie Yuan'
rg -n '^## Authors and Artifact Evaluation Contributor$|^### Paper Authors$|^### Artifact Evaluation Contributor$' README.md
rg -n 'Zhiyu Zheng|Yiming Du|Ziyi Wang|Zhiang Wang|Wenjie Yuan|25303060069@m.fudan.edu.cn' README.md
git diff --check
```

Expected: the first command exits successfully because no personal information
appears in the opening section; the remaining commands find the closing
headings, all five names, the contributor email, and no whitespace errors.

- [ ] **Step 5: Run the repository tests**

Run:

```bash
make test
```

Expected: 47 core tests and 9 Web Demo tests pass, and the repository structure
check reports that the repository is reviewer-ready.

- [ ] **Step 6: Commit and push the README update**

Run:

```bash
git add README.md
git commit -m "Reorganize README introduction and credits"
git push origin main
```

Expected: the commit contains only `README.md`, and `main` is synchronized with
`origin/main`.
