# Native Artifact Prose Revision Design

## Objective

Revise the Artifact Appendix in `ARTICLE/artifact_evaluation.tex` and the
submission draft in `docs/artifact-submission-draft.md` so that both read as
concise, idiomatic technical writing. The revision will follow the organization
of `c416.pdf` while preserving the stronger reproducibility detail required for
this artifact.

## Scope

The revision covers all prose in the two-page Artifact Appendix and all prose
in the artifact submission draft. It includes headings, introductory text,
checklist entries, workflow descriptions, result interpretation, limitations,
and failure-reporting guidance.

Commands, mathematical notation, algorithms, model names, experiment counts,
numerical results, the DOI, and Table 5 source mappings will not change.
Conventional pseudocode fields such as `Input` and `Output` are not treated as
prose fragments and will retain their compact form.

## Writing Style

Every prose item will use a complete sentence. Checklist labels may remain for
scanability, but the text following each label will contain a finite verb and a
clear subject. Paragraphs will favor direct statements and ordinary technical
English over promotional or formulaic language.

The revision will:

- use active voice when the actor is relevant;
- use passive voice only when the procedure or result is more important than
  the actor;
- keep `Teacher` and `Student` capitalized when they name ReviewDSE roles;
- use lowercase `agent` when referring to agents generically;
- avoid repeated framing such as “does not replace,” “is shown for
  completeness,” and “retained” when a direct statement conveys the same fact;
- avoid claims that are broader than the documented artifact behavior; and
- preserve the restrained wording and section order used by the reference
  artifact appendix.

## Level of Implementation Detail

The Appendix and submission draft will not enumerate internal implementation
paths, long commit identifiers, or revision manifests. Exact OpenROAD, ORFS,
Yosys, and related revisions belong in the repository README and provenance
documentation. The two revised documents will state that the toolchain is
pinned and direct reviewers to the README without reproducing those values.

Reviewer-facing commands and result locations will remain when they are needed
to run or assess an experiment. Internal source directories, prompt-template
paths, manifest filenames, and prepared source commit hashes will be removed
from narrative paragraphs unless a reviewer must type or inspect them directly.

## Synchronization

The Appendix will remain the compact reviewer-facing version. The submission
draft may provide more operational detail, but shared facts will use matching
terminology and compatible sentences. In particular, both documents will agree
on model access, Table 5 source mappings, Table 6 data, the Level 1
reconstruction, expected results, and the role of fixed-result replay.

## Layout and Validation

The Artifact Appendix will remain on pages 8 and 9 of the compiled paper in
two-column ACM format. The two algorithm blocks will remain intact, and page 8
will remain substantially filled. If the prose grows, sentences will be
shortened without removing evaluation requirements.

Validation will include:

1. compiling `artifact_evaluation.tex` with `latexmk`;
2. confirming that the paper has nine pages and that the Appendix occupies the
   final two pages;
3. visually inspecting pages 8 and 9;
4. scanning both documents for sentence fragments and stale terminology;
5. checking that all recorded numbers, commands, model names, and source
   mappings are unchanged, and that low-level revision data remains available
   through the README; and
6. copying the rebuilt PDF into `DPLEvolve-AE/paper/` and updating its recorded
   SHA-256 value.

## Non-goals

This revision will not change the paper method, artifact behavior, experiment
scope, repository structure, Zenodo packaging procedure, or reproduction
commands. The Zenodo archive will be regenerated only to include the revised
documents and PDF. The revision will not add a non-LLM method path or require a
live closed-loop search for the Functional badge.
