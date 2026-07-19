// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"

namespace dpl_evolve {

// Guided initial-location storage helpers shared by LEGALM-style stages.
void Opendp::clearGuidedInitialLocations()
{
  guided_initial_locations_.clear();
  guided_initial_valid_.clear();
  legalm_target_rows_.clear();
  legalm_target_sites_.clear();
  legalm_target_valid_.clear();
  legalm_stage2_final_overflow_sites_ = 0;
  legalm_stage2_final_overflow_bins_ = 0;
  legalm_stage2_final_max_density_ = 0.0;
  legalm_stage2_overflow_free_ = false;
  legalm_stage3_exact_handoff_cells_ = 0;
  legalm_stage3_exact_handoff_target_misses_ = 0;
}

bool Opendp::hasGuidedInitialLocation(const Node* cell) const
{
  if (cell == nullptr) {
    return false;
  }
  const int id = cell->getId();
  return id >= 0 && id < static_cast<int>(guided_initial_valid_.size())
         && guided_initial_valid_[id] != 0;
}

int Opendp::commitGuidedInitialLocationsToDb()
{
  if (network_ == nullptr || grid_ == nullptr || block_ == nullptr) {
    return 0;
  }

  int guided_cells = 0;
  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      continue;
    }

    DbuPt target = initialLocation(cell, false);
    const int id = cell->getId();
    if (id >= 0 && id < static_cast<int>(guided_initial_valid_.size())
        && guided_initial_valid_[id] != 0) {
      const auto& guided = guided_initial_locations_[id];
      target = {DbuX{guided.first}, DbuY{guided.second}};
      ++guided_cells;
    }

    const DbuPt legal = legalPt(cell, target);
    const GridPt grid_pt = legalGridPt(cell, legal);
    const DbuPt snapped{gridToDbu(grid_pt.x, grid_->getSiteWidth()),
                        grid_->gridYToDbu(grid_pt.y)};
    cell->setLeft(snapped.x);
    cell->setBottom(snapped.y);
    cell->setPlaced(true);

    odb::dbInst* inst = cell->getDbInst();
    if (inst != nullptr) {
      odb::dbSite* site = inst->getMaster()->getSite();
      if (site != nullptr) {
        auto orient = grid_->getSiteOrientation(grid_pt.x, grid_pt.y, site);
        if (orient.has_value()) {
          cell->setOrient(orient.value());
          inst->setOrient(orient.value());
        }
      }
      inst->setLocation(core_.xMin() + snapped.x.v,
                        core_.yMin() + snapped.y.v);
    }
  }
  logger_->metric("dpl_evolve__diff_guidance__committed_guided_cells",
                  guided_cells);
  return guided_cells;
}

}  // namespace dpl_evolve
