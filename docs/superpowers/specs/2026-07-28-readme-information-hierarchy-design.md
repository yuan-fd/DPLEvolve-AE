# README Information Hierarchy Design

## Goal

Reorganize the root `README.md` so that a new visitor first understands what
the repository contains and why it exists. Move personal and contact
information out of the opening section and place it at the end of the README.

## Scope

This change is limited to the root README. It does not alter experiment
commands, dependencies, result locations, artifact contents, or release
metadata.

## Opening Section

Keep the paper title as the page title. Replace the current one-line summary
with a short introductory paragraph that explains that the repository contains
the ReviewDSE implementation and the artifact for the accompanying paper. The
paragraph will identify the reproduced tables and figures, mention the
protected Teacher--Student workflow, and state that a ReviewDSE search requires
authenticated Codex access.

Keep the links to the paper, paper with Artifact Appendix, evaluated Zenodo
snapshot, and demonstration video immediately after the introduction. Remove
the artifact contact from this section. Keep the workflow image before the
operational documentation.

## Main Documentation

Preserve the existing order and content of the code structure, dependencies,
installation, experiment, evaluation, and Web Demo sections. Only minor
transition edits needed to make the new introduction read naturally are in
scope.

## Closing Section

Add an `Authors and Artifact Evaluation Contributor` section at the end of the
README.

The `Paper Authors` subsection will list the camera-ready author order and
affiliations without email addresses:

- Zhiyu Zheng, Fudan University
- Yiming Du, Fudan University
- Ziyi Wang, The Chinese University of Hong Kong
- Zhiang Wang, Fudan University

The `Artifact Evaluation Contributor` subsection will list Wenjie Yuan, Fudan
University, and `25303060069@m.fudan.edu.cn` as the artifact contact.

## Verification

After editing the README:

1. Confirm that no author or artifact-contact line appears before `Code
   Structure`.
2. Confirm that all four paper authors, their affiliations, and the artifact
   contributor appear in the closing section.
3. Run `git diff --check` and the repository test suite.
