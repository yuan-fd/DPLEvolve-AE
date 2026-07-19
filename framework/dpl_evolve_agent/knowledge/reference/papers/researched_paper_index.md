# Researched Placement Paper Index

This index records paper/source handles used by the legalization and DPO
pseudocode cards.  It is optimized for `rg` lookup by Teacher/Student agents.
Do not treat every listed method as a donor implementation; first map it to the
current path and validation target.

## Important Source Correction

The OpenDP-named committed reference is not the OpenROAD OpenDP engine paper.
Its metadata/content identify it as **Fence-Region-Aware Mixed-Height Standard
Cell Legalization** (GLSVLSI 2019).  Use OpenROAD documentation for OpenROAD
DPL command behavior and use this reference only for fence/fragmented
row/mixed-height legalization ideas.

## Legalization Sources

| id | stage | source handle | use in this repo |
|---|---|---|---|
| openroad_dpl_docs | legalizer context | https://openroad.readthedocs.io/en/latest/main/src/dpl/README.html | Command truth for Diamond Search, NegotiationLegalizer, `detailed_placement`, `improve_placement`, `optimize_mirroring`. |
| abacus_2008 | legalization | DOI `10.1145/1353629.1353640`; DBLP `conf/ispd/SpindlerSJ08` | Row candidate search plus PlaceRow cluster collapse. |
| tetris_2002 | legalization | Hill patent / legacy references | Ordered greedy nearest-site baseline; closest to Diamond family. |
| jezz_2015 | incremental legalization | Search `Jezz: An Effective Legalization Algorithm for Minimum Displacement`; incremental timing-driven placement references | Incremental row-node legality oracle for DPO move dry runs. |
| history_flow_2010 | legalization | IBM Research: History-based VLSI legalization using network flow | NEGOTIATION resource closure with history prices and realization failures. |
| bonnplacelegal_2013 | legalization | Search `BonnPlace Legalization: Minimizing Movement by Iterative Augmentation` | Augmenting-path zone movement with fixed-order packing. |
| darav_network_flow_2017 | legalization | ISPD 2017 slides: A Fast, Robust Network Flow-based Standard-Cell Legalization Method | Maximum-movement-aware flow for stress/cut-row cases. |
| nblg | legalization | NBLG mixed-height legalizer; OpenROAD docs say NegotiationLegalizer is NBLG-based | Native negotiation candidate pricing, congestion/history, postopt. |
| legalm_ispd2025 | legalization | `https://chunyuanzhao.me/pdf/LEGALM.pdf`; DOI `10.1145/3698364.3705356` | LEGALM ALM/BGD overflow reduction, escape, zero-overflow refinement. |
| legalm_2_0 | legalization | DOI `10.1109/TCAD.2025.3597526` | Follow-up ALM methodology and generalized legalization guidance. |
| double_row_2021 | legalization | `https://www.or.uni-bonn.de/~hougardy/paper/HougardyNeuwohnerSchorr2021.pdf`; arXiv `2101.08561` | Exact adjacent-row repair subproblem for mixed-height/fixed-order zones. |
| fence_region_aware_2019 | legalization | OpenDP-named committed reference; DOI `10.1145/3299874.3318012` | Prelegalization, fragmented/fence region moves, quality refinement. |
| domocus_parallel | legalization | Search `Domocus lock-free parallel legalization` / `Improved parallel legalization` | Parallel region candidate generation with deterministic boundary repair. |
| pin_accessible_ripple | legalization | `https://github.com/cuhk-eda/ripple`; search exact paper title | Pin-access-aware mixed-cell-height legalization. |
| constraint_aware_mch_legalization | legalization | Search technology/region/VAC/NIMH/hybrid-row-height/netlist-aware MCH legalization papers | Constraint-aware legal resource filtering and pricing. |
| dreamplace_2019 | placement context | `https://research.nvidia.com/sites/default/files/pubs/2019-06_DREAMPlace%3A-Deep-Learning/54_1_Lin_DREAMPLACE.pdf` | External GPU/global-placement context and Abacus donor reference; not a default DPL target plane. |

## DPO Sources

| id | stage | source handle | use in this repo |
|---|---|---|---|
| fastdp_2005 | DPO | DOI `10.1109/ICCAD.2005.1560039`; DBLP `conf/iccad/PanVC05` | Global swap, local reorder, single-segment clustering. |
| gpu_dpo_lsmc_2026 | DPO | `https://vlsicad.ucsd.edu/Publications/Conferences/425/c425.pdf` | LSMC large-step escape plus accelerated MIS/global-swap/reorder kernels. |
| abcdplace_2020 | DPO / placer | DOI `10.1109/TCAD.2020.2971531`; `https://www.jqgu.net/publications/papers/PD_TCAD2020_Gu.pdf` | Batch-concurrent candidate scoring / independent non-conflicting commit. |
| density_aware_dp_2014 | DPO | DOI `10.1145/2593069.2593142` | Lazy density profit plus instant legalization for swap decisions. |
| hippocrates_2007 | DPO constraints | DOI `10.1109/ASPDAC.2007.357976`; UTDA PDF | Pin-based do-no-harm timing/electrical filters for placement transforms. |
| timing_quadratic_2015 | DPO / timing | DOI `10.1109/VLSI-SoC.2015.7314382` | Quadratic timing target generation plus incremental legalizer. |
| mrdp_2017 | DPO / multi-row | `https://yibolin.com/publications/papers/PLACE_TCAD2017_Lin.pdf` | Multi-row detailed placement and heterogeneous cell handling. |
| pin_access_refinement_2021 | DPO / routability | DOI `10.1109/TCAD.2021.3066528` | Pin-access-driven legal move/swap/reorder filters, future routability route. |
| dfm_spacing_implant_dp | DPO / DFM | Search MIA-aware MCH DP, DDA/region constraints, spacing-cost-aware DP, hybrid-row-height DP | Local DFM/rule-aware candidate filtering for detailed placement. |
| eco_incremental_dp | refinement | Search Hybrid ECO Detailed Placement and incremental detailed placement refinement | Bounded changed-region repair on an existing placement. |
| macro_prototype_refinement | refinement | Search IncreMacro / incremental macro placement refinement | Existing-placement prototype refinement; not global placement from scratch. |

## Cards Generated From This Index

- `knowledge/algorithms/legalization_paper_pseudocode.md`
- `knowledge/algorithms/legalization/`
- `knowledge/algorithms/dpo_paper_pseudocode.md`
- `knowledge/algorithms/dpo_refinement/`
- `knowledge/algorithms/context/`

Recommended `rg` queries:

```bash
rg -n "L[0-9]+\.|D[0-9]+\.|Pseudo code|path purity|handoff|history|LSMC|Abacus|NBLG|LEGALM" knowledge/algorithms
rg -n "HPWLlg|HPWLimprove|HPWLfinal|full-flow|candidate generation|exact-delta" knowledge
```
