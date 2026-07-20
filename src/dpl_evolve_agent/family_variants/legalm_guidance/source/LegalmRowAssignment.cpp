// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"

namespace dpl_evolve {

namespace {

struct RowAssignCell
{
  Node* cell = nullptr;
  int row = 0;
  int original_site = 0;
  int desired_site = 0;
  int assigned_site = 0;
  int width_sites = 1;
  bool seeded = false;
};

}  // namespace

// Bounded row assignment for ALM/BGD guided starts before full legalization.
void Opendp::runRowAssignmentGuidance(const EvolveContext&)
{
  if (network_ == nullptr || grid_ == nullptr
      || guided_initial_valid_.empty()) {
    logger_->metric("dpl_evolve__row_assignment__status", 0);
    return;
  }

  const int row_count = grid_->getRowCount().v;
  const int site_count = grid_->getRowSiteCount().v;
  const DbuX site_width = grid_->getSiteWidth();
  if (row_count <= 0 || site_count <= 0 || site_width.v <= 0) {
    logger_->metric("dpl_evolve__row_assignment__status", 0);
    return;
  }

  std::vector<RowAssignCell> cells;
  cells.reserve(network_->getNumCells());
  std::vector<unsigned char> touched_rows(row_count, 0);
  int skipped_cells = 0;
  int seed_guided_cells = 0;

  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell() || cell->inGroup() || isMultiRow(cell)) {
      ++skipped_cells;
      continue;
    }

    const DbuPt init = initialLocation(cell, false);
    DbuPt desired = init;
    const int id = cell->getId();
    const bool seeded = id >= 0
                        && id < static_cast<int>(guided_initial_valid_.size())
                        && guided_initial_valid_[id] != 0;
    if (seeded) {
      const auto& guided = guided_initial_locations_[id];
      desired = {DbuX{guided.first}, DbuY{guided.second}};
      ++seed_guided_cells;
    }

    const GridPt desired_grid = legalGridPt(cell, desired);
    const GridPt init_grid = legalGridPt(cell, init);
    const int row = std::clamp(desired_grid.y.v, 0, row_count - 1);
    const int desired_site = std::clamp(desired_grid.x.v, 0, site_count - 1);
    const int original_site = std::clamp(init_grid.x.v, 0, site_count - 1);
    const int width_sites = std::max(
        1,
        static_cast<int>(std::ceil(static_cast<double>(cell->getWidth().v)
                                   / static_cast<double>(site_width.v))));
    if (seeded) {
      touched_rows[row] = 1;
    }
    cells.push_back({cell,
                     row,
                     original_site,
                     desired_site,
                     desired_site,
                     width_sites,
                     seeded});
  }

  if (seed_guided_cells == 0) {
    logger_->metric("dpl_evolve__row_assignment__status", 1);
    logger_->metric("dpl_evolve__row_assignment__seed_guided_cells", 0);
    return;
  }

  std::vector<std::vector<int>> row_seed_cells(row_count);
  std::vector<std::vector<int>> row_blocking_cells(row_count);
  for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
    const RowAssignCell& cell = cells[i];
    if (touched_rows[cell.row] == 0) {
      continue;
    }
    if (cell.seeded) {
      row_seed_cells[cell.row].push_back(i);
    } else {
      row_blocking_cells[cell.row].push_back(i);
    }
  }

  int touched_row_count = 0;
  int assigned_seed_cells = 0;
  int changed_cells = 0;
  int max_abs_shift = 0;
  int64_t total_abs_shift = 0;
  int failed_assignments = 0;
  int occupied_sites = 0;
  constexpr int kLocalAssignRadius = 8;

  for (int row_id = 0; row_id < row_count; ++row_id) {
    auto& seed_row = row_seed_cells[row_id];
    if (seed_row.empty()) {
      continue;
    }
    ++touched_row_count;
    std::sort(
        seed_row.begin(), seed_row.end(), [&](const int lhs, const int rhs) {
          const RowAssignCell& a = cells[lhs];
          const RowAssignCell& b = cells[rhs];
          if (a.desired_site != b.desired_site) {
            return a.desired_site < b.desired_site;
          }
          return a.cell->getId() < b.cell->getId();
        });

    std::vector<unsigned char> occupied(site_count, 0);
    auto reserve_sites = [&](const int x, const int width) {
      const int x0 = std::clamp(x, 0, std::max(0, site_count - 1));
      const int x1 = std::clamp(x + width, 0, site_count);
      for (int site = x0; site < x1; ++site) {
        if (occupied[site] == 0) {
          ++occupied_sites;
        }
        occupied[site] = 1;
      }
    };
    auto can_place = [&](const int x, const int width) {
      if (x < 0 || x + width > site_count) {
        return false;
      }
      for (int site = x; site < x + width; ++site) {
        if (occupied[site] != 0) {
          return false;
        }
      }
      return true;
    };

    for (const int idx : row_blocking_cells[row_id]) {
      const RowAssignCell& cell = cells[idx];
      reserve_sites(cell.original_site, cell.width_sites);
    }

    for (const int idx : seed_row) {
      RowAssignCell& cell = cells[idx];
      const int desired = std::clamp(
          cell.desired_site, 0, std::max(0, site_count - cell.width_sites));
      int best_site = -1;
      for (int radius = 0; radius <= kLocalAssignRadius && best_site < 0;
           ++radius) {
        const int left = desired - radius;
        const int right = desired + radius;
        if (can_place(left, cell.width_sites)) {
          best_site = left;
          break;
        }
        if (radius != 0 && can_place(right, cell.width_sites)) {
          best_site = right;
          break;
        }
      }

      if (best_site < 0) {
        // Keep the ALM/BGD target when there is no near free slot.  The
        // downstream repair/legalization stage, if a student explicitly
        // installs one, should resolve local conflicts better than a distant
        // row-assignment jump.
        cell.assigned_site = desired;
        ++failed_assignments;
        continue;
      }

      cell.assigned_site = best_site;
      reserve_sites(cell.assigned_site, cell.width_sites);
      ++assigned_seed_cells;
    }
  }

  guided_initial_locations_.assign(network_->getNumNodes(), {0, 0});
  guided_initial_valid_.assign(network_->getNumNodes(), 0);
  for (const RowAssignCell& cell : cells) {
    if (!cell.seeded) {
      continue;
    }
    const int id = cell.cell->getId();
    if (id < 0 || id >= static_cast<int>(guided_initial_valid_.size())) {
      continue;
    }
    const DbuPt assigned_pt{gridToDbu(GridX{cell.assigned_site}, site_width),
                            grid_->gridYToDbu(GridY{cell.row})};
    const DbuPt snapped = legalPt(cell.cell, assigned_pt);
    guided_initial_locations_[id] = {snapped.x.v, snapped.y.v};
    guided_initial_valid_[id] = 1;
    const int abs_shift = std::abs(cell.assigned_site - cell.original_site);
    if (abs_shift > 0) {
      ++changed_cells;
    }
    max_abs_shift = std::max(max_abs_shift, abs_shift);
    total_abs_shift += abs_shift;
  }

  const double avg_shift = assigned_seed_cells == 0
                               ? 0.0
                               : static_cast<double>(total_abs_shift)
                                     / static_cast<double>(assigned_seed_cells);
  logger_->metric("dpl_evolve__row_assignment__status", 1);
  logger_->metric("dpl_evolve__row_assignment__eligible_cells",
                  static_cast<int>(cells.size()));
  logger_->metric("dpl_evolve__row_assignment__skipped_cells", skipped_cells);
  logger_->metric("dpl_evolve__row_assignment__seed_guided_cells",
                  seed_guided_cells);
  logger_->metric("dpl_evolve__row_assignment__touched_rows",
                  touched_row_count);
  logger_->metric("dpl_evolve__row_assignment__assigned_seed_cells",
                  assigned_seed_cells);
  logger_->metric("dpl_evolve__row_assignment__changed_cells", changed_cells);
  logger_->metric("dpl_evolve__row_assignment__failed_assignments",
                  failed_assignments);
  logger_->metric("dpl_evolve__row_assignment__occupied_sites", occupied_sites);
  logger_->metric("dpl_evolve__row_assignment__avg_shift_sites", avg_shift);
  logger_->metric("dpl_evolve__row_assignment__max_shift_sites", max_abs_shift);
  logger_->info(DPL,
                1213,
                "Row assignment guidance assigned {} ALM seed cells across {} "
                "touched rows from {} seeds (changed {}, failed {}, avg shift "
                "{:.2f} sites).",
                assigned_seed_cells,
                touched_row_count,
                seed_guided_cells,
                changed_cells,
                failed_assignments,
                avg_shift);
}

}  // namespace dpl_evolve
