# Table 5 Source Snapshots and AE Compression Design

## Goal

Make Table 5 functionally reproducible from three retained legalizer source
snapshots, remove the stale six-tree and `config_dense2.mk` recovery contract,
and compress the paper's Artifact Appendix from three pages to two without
removing required MLCAD 2026 AE information or either algorithm block.

## Scope

This change affects only the Table 5 artifact path and the paper's Artifact
Appendix. In particular, the `CORE_UTILIZATION` values below are local to the
three Table 5 input-generation calls in
`scripts/reproduce/prepare_table5_inputs.sh`. The change must not edit ORFS
design configuration files, the global case registry, Table 4 inputs, or any
other experiment profile.

## Source Snapshot Layout

The artifact will track three complete source trees:

```text
artifacts/02-table5-composability/programs/
├── legalm/dpl_evolve/
├── diamond/dpl_evolve/
├── negotiation/dpl_evolve/
└── MANIFEST.sha256
```

They will be copied without `.git` metadata from these retained prepared
sources:

```text
../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/framework_dpl_evolve
../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/diamond_dpl_evolve
../../dpl_evolve_agent/.dpl_evolve_state/seed_sources/default_negotiation_dpl_evolve
```

The framework source is the LEGALM implementation: its active student pipeline
runs LEGALM guidance and full LEGALM legalization. The other two snapshots are
the prepared Diamond and negotiation-primary implementations. The manifest
covers every regular file in the three snapshot directories and is checked
before any Table 5 build or EDA execution.

## Table 5 Mapping and Input Preparation

The runner will map the six table roles to the three shared snapshots:

| Table 5 row | Selected | Reference |
| --- | --- | --- |
| AES dense N45 | LEGALM | Diamond |
| JPEG dense N45 | Negotiation | Negotiation |
| SWERV dense N45 | Diamond | Negotiation |

The input generator will use the pinned ORFS design configurations already
prepared by the artifact and apply only per-invocation environment overrides:

| Table 5 case | Flow variant | `CORE_UTILIZATION` |
| --- | --- | ---: |
| `aes_dense_nangate45` | `DENSE` | 70 |
| `jpeg_util90_nangate45` | `DENSE` | 90 |
| `swerv_wrapper_nangate45` | `DENSE_2` | 60 |

No tracked ORFS `config.mk` is edited. No standalone `config_dense2.mk` is
required. The `DENSE_2` string remains only the SWERV Table 5 output variant.

`reproduce_table5.sh` will continue to create private build/evaluation products
through the existing candidate-matrix runner, but it will take each candidate
from the immutable tracked snapshot selected by the mapping. It will not create
or require six release branches or six duplicated source archives.

## Validation and Failure Handling

The data check will fail before EDA execution if a snapshot file is missing or
does not match `MANIFEST.sha256`. A successful static check reports that all
three snapshots and their row/role mapping are available and checksummed.

Tests will be updated before the implementation so that they initially fail
against the old six-tree/config contract. They will cover:

- the presence and manifest integrity of all three snapshots;
- the exact six-role-to-three-snapshot mapping;
- the local Table 5 utilization values 70, 90, and 60;
- absence of a `config_dense2.mk` dependency;
- absence of stale Table 5 `BLOCKED` claims in release-facing documentation;
- inclusion of all three snapshots in the Zenodo archive contract; and
- Table 5 dry-run command assembly without launching EDA.

The final verification will run the focused unit and integration tests,
`make check-table5-data`, the Table 5 dry run, the repository test suite, and
the Zenodo archive audit that does not upload anything.

## Documentation and Zenodo Packaging

The root README, reviewer walkthrough, artifact docs, source/input contracts,
and `.zenodo.json` will describe the three retained implementations and the
fresh Table 5 replay. Stale statements about a missing SWERV configuration or
six missing source trees will be removed. `prepare_zenodo.sh` will require and
verify the tracked snapshot manifest and archive listing.

The external Table 6 package remains unchanged. Table 5 source snapshots are
part of the Git repository and therefore enter the GitHub and Zenodo snapshots
through the ordinary release copy.

## Artifact Appendix Compression

The two algorithm blocks remain separate and in the order Calibration then
Target Search. The Appendix will retain the official MLCAD 2026 checklist
coverage, GitHub and Zenodo fields, model disclosure, OpenROAD revisions,
Table 5 mapping, commands, expected values, tolerances, and validation paths.

Compression will come from removing duplication rather than deleting required
facts:

- combine closely related checklist entries into fewer compact bullets;
- shorten repeated hardware, software, model, and workflow prose;
- remove prose that restates individual algorithm steps;
- use a local compact font and spacing only inside the Appendix group;
- keep commands concise and share common explanations; and
- express expected results in dense paragraphs keyed by table or figure.

The paper body is unchanged. A successful result is a nine-page PDF in which
the paper occupies pages 1--7 and the Artifact Appendix occupies pages 8--9.
The final check includes visual inspection of both Appendix pages and confirms
that the Appendix introduces no undefined references, LaTeX errors, or new
overfull boxes.

## Non-goals

- Reconstructing the six historical iteration-specific commits.
- Creating new OpenROAD branches for release snapshots.
- Editing global ORFS design configurations or non-Table 5 case settings.
- Running the complete paid Teacher--Student search during AE verification.
- Replacing the final Zenodo DOI placeholder before the deposit exists.
