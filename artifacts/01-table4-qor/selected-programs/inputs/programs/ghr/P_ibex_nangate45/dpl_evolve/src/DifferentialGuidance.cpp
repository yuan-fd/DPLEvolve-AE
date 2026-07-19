// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"

namespace dpl_evolve {

// Guided initial-location storage helpers shared by LEGALM-style stages.
void Opendp::clearGuidedInitialLocations()
{
  guided_initial_locations_.clear();
  guided_initial_valid_.clear();
  clearLegalmHandoffState();
}

void Opendp::clearLegalmHandoffState()
{
  legalm_target_rows_.clear();
  legalm_target_sites_.clear();
  legalm_target_valid_.clear();
  legalm_stage2_final_overflow_sites_ = 0;
  legalm_stage2_final_overflow_bins_ = 0;
  legalm_stage2_final_max_density_ = 0.0;
  legalm_stage2_overflow_free_ = false;
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

std::vector<ResidualHandoffCellSpec> Opendp::buildResidualHandoffSpecs() const
{
  std::vector<ResidualHandoffCellSpec> specs;
  if (network_ == nullptr || grid_ == nullptr || legalm_target_valid_.empty()) {
    return specs;
  }

  specs.reserve(network_->getNumNodes());
  for (const auto& node_ptr : network_->getNodes()) {
    const Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      continue;
    }

    const int id = cell->getId();
    if (id < 0 || id >= static_cast<int>(legalm_target_valid_.size())
        || legalm_target_valid_[id] == 0) {
      continue;
    }

    const GridY current_row = grid_->gridSnapDownY(cell);
    const GridX current_site = grid_->gridX(cell);
    const int residual_rows = std::abs(legalm_target_rows_[id] - current_row.v);
    const int residual_sites
        = std::abs(legalm_target_sites_[id] - current_site.v);
    if (residual_rows == 0 && residual_sites == 0) {
      continue;
    }

    ResidualHandoffCellSpec spec;
    spec.node_id = id;
    spec.target_row = legalm_target_rows_[id];
    spec.target_site = legalm_target_sites_[id];
    spec.residual_rows = residual_rows;
    spec.residual_sites = residual_sites;
    spec.priority = residual_sites + (residual_rows * 64);
    spec.active = true;
    specs.push_back(spec);
  }

  std::sort(specs.begin(),
            specs.end(),
            [](const ResidualHandoffCellSpec& lhs,
               const ResidualHandoffCellSpec& rhs) {
              if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
              }
              return lhs.node_id < rhs.node_id;
            });
  return specs;
}

}  // namespace dpl_evolve
