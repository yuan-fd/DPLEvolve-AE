// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"

namespace dpl_evolve {

namespace {
struct DiamondSourceSnapshotEntry
{
  int site = 0;
  int row = 0;
  unsigned char valid = 0;
};

std::unordered_map<const Opendp*, std::vector<DiamondSourceSnapshotEntry>>&
diamondSnapshots()
{
  static std::unordered_map<const Opendp*, std::vector<DiamondSourceSnapshotEntry>>
      snapshots;
  return snapshots;
}
}  // namespace

// Guided initial-location storage helpers shared by LEGALM-style stages.
void Opendp::clearGuidedInitialLocations()
{
  clearGuidedSeedLocations();
  clearDiamondSourceSnapshot();
  clearLegalmHandoffState();
}

void Opendp::clearGuidedSeedLocations()
{
  guided_initial_locations_.clear();
  guided_initial_valid_.clear();
  use_guided_initial_locations_ = false;
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

void Opendp::clearDiamondSourceSnapshot()
{
  diamondSnapshots().erase(this);
}

void Opendp::reserveDiamondSourceSnapshot(const int node_count)
{
  diamondSnapshots()[this].assign(std::max(0, node_count), {});
}

void Opendp::recordDiamondSourceSnapshot(const Node* cell,
                                         const int site,
                                         const int row)
{
  if (cell == nullptr) {
    return;
  }
  const int id = cell->getId();
  auto& snapshots = diamondSnapshots()[this];
  if (id < 0 || id >= static_cast<int>(snapshots.size())) {
    return;
  }
  auto& snapshot = snapshots[id];
  snapshot.site = site;
  snapshot.row = row;
  snapshot.valid = 1;
}

bool Opendp::getDiamondSourceSnapshot(const Node* cell, int& site, int& row) const
{
  if (cell == nullptr) {
    return false;
  }
  const auto it = diamondSnapshots().find(this);
  if (it == diamondSnapshots().end()) {
    return false;
  }
  const auto& snapshots = it->second;
  const int id = cell->getId();
  if (id < 0 || id >= static_cast<int>(snapshots.size())) {
    return false;
  }
  const auto& snapshot = snapshots[id];
  if (snapshot.valid == 0) {
    return false;
  }
  site = snapshot.site;
  row = snapshot.row;
  return true;
}

int Opendp::diamondSourceSnapshotCount() const
{
  const auto it = diamondSnapshots().find(this);
  if (it == diamondSnapshots().end()) {
    return 0;
  }
  return static_cast<int>(std::count_if(it->second.begin(),
                                        it->second.end(),
                                        [](const DiamondSourceSnapshotEntry& e) {
                                          return e.valid != 0;
                                        }));
}

std::vector<ResidualHandoffCellSpec> Opendp::buildResidualHandoffSpecs() const
{
  std::vector<ResidualHandoffCellSpec> specs;
  if (network_ == nullptr || grid_ == nullptr) {
    return specs;
  }

  const bool have_diamond_snapshot = diamondSourceSnapshotCount() > 0;
  const bool have_legalm_targets = !legalm_target_valid_.empty();
  if (!have_diamond_snapshot && !have_legalm_targets) {
    return specs;
  }

  int diamond_specs = 0;
  int legalm_specs = 0;
  specs.reserve(network_->getNumNodes());
  for (const auto& node_ptr : network_->getNodes()) {
    const Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      continue;
    }

    const int id = cell->getId();
    int target_row = -1;
    int target_site = -1;
    bool from_diamond = false;
    if (getDiamondSourceSnapshot(cell, target_site, target_row)) {
      from_diamond = true;
    } else if (id >= 0 && id < static_cast<int>(legalm_target_valid_.size())
               && legalm_target_valid_[id] != 0) {
      target_row = legalm_target_rows_[id];
      target_site = legalm_target_sites_[id];
    } else {
      continue;
    }

    const GridY current_row = grid_->gridSnapDownY(cell);
    const GridX current_site = grid_->gridX(cell);
    const int residual_rows = std::abs(target_row - current_row.v);
    const int residual_sites = std::abs(target_site - current_site.v);
    if (residual_rows == 0 && residual_sites == 0) {
      continue;
    }

    ResidualHandoffCellSpec spec;
    spec.node_id = id;
    spec.target_row = target_row;
    spec.target_site = target_site;
    spec.residual_rows = residual_rows;
    spec.residual_sites = residual_sites;
    spec.priority = residual_sites + (residual_rows * 64);
    if (from_diamond) {
      const int row_bias = std::max(0, 3 - residual_rows) * 16;
      const int site_bias = std::max(0, 48 - residual_sites);
      spec.producer_rank = -(row_bias + site_bias);
      spec.producer_sites = residual_sites + (residual_rows * 64);
    }
    spec.active = true;
    specs.push_back(spec);
    if (from_diamond) {
      ++diamond_specs;
    } else {
      ++legalm_specs;
    }
  }

  std::sort(specs.begin(),
            specs.end(),
            [](const ResidualHandoffCellSpec& lhs,
               const ResidualHandoffCellSpec& rhs) {
              if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
              }
              if (lhs.producer_rank != rhs.producer_rank) {
                return lhs.producer_rank < rhs.producer_rank;
              }
              if (lhs.producer_sites != rhs.producer_sites) {
                return lhs.producer_sites < rhs.producer_sites;
              }
              return lhs.node_id < rhs.node_id;
            });
  if (logger_ != nullptr) {
    logger_->metric("dpl_evolve__handoff__diamond_snapshot_specs",
                    diamond_specs);
    logger_->metric("dpl_evolve__handoff__legalm_snapshot_specs",
                    legalm_specs);
    logger_->report("Residual handoff specs built: diamond={} legalm={} total={}",
                    diamond_specs,
                    legalm_specs,
                    specs.size());
  }
  return specs;
}

}  // namespace dpl_evolve
