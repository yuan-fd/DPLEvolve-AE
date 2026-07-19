// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <atomic>
#include <cmath>
#include <mutex>

#include "LegalmCommon.h"
#include "LegalmTechPenalty.h"
#include "PlacementDRC.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace dpl_evolve {

namespace {

constexpr LegalmPaperParams kPaper = legalmPaperParams();

struct LegalmPlaceCell
{
  Node* cell = nullptr;
  int desired_row = 0;
  int desired_site = 0;
  int frontier_row = 0;
  int frontier_site = 0;
  int original_row = 0;
  int original_site = 0;
  int width_sites = 1;
  int height_rows = 1;
  int vertical_step_rows = 1;
  double height_class_weight = 1.0;
  unsigned master_sym = 0;
  bool master_multi_row = false;
  int site_sym_class = -1;
  bool guided = false;
  bool frontier_target = false;
  Group* group = nullptr;
};

struct SiteSymClass
{
  odb::dbSite* site = nullptr;
  unsigned master_sym = 0;
  std::vector<unsigned char> compatible;
  std::vector<odb::dbOrientType::Value> orientations;
};

template <typename Func>
void parallelFor(const int count, const int threads, Func&& func)
{
  if (count <= 0) {
    return;
  }

  const int worker_count = std::max(1, std::min(threads, count));
  if (worker_count == 1) {
    func(0, count, 0);
    return;
  }

#ifdef _OPENMP
#pragma omp parallel num_threads(worker_count)
  {
    const int worker = omp_get_thread_num();
    const int begin = (count * worker) / worker_count;
    const int end = (count * (worker + 1)) / worker_count;
    func(begin, end, worker);
  }
#else
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    const int begin = (count * worker) / worker_count;
    const int end = (count * (worker + 1)) / worker_count;
    workers.emplace_back(
        [&, begin, end, worker]() { func(begin, end, worker); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
#endif
}

}  // namespace

// LEGALM-style full legalization plus Stage 3 lambda-infinity BGD refinement.
//
// Stage 3 uses the same bounded-gradient-descent structure as the paper:
// evaluate a 7x35 stencil inside shifted partitions and treat every overflow
// candidate as +infinity by requiring an exact fit in the maintained free
// slots.
bool Opendp::runLegalmFullLegalization(const EvolveContext& context)
{
  if (network_ == nullptr || grid_ == nullptr) {
    logger_->metric("dpl_evolve__legalm_full__status", 0);
    return false;
  }

  initGrid();
  setFixedGridCells();
  if (!arch_->getRegions().empty()) {
    groupInitPixels2();
    groupInitPixels();
  }

  const int row_count = grid_->getRowCount().v;
  const int site_count = grid_->getRowSiteCount().v;
  const DbuX site_width = grid_->getSiteWidth();
  if (row_count <= 0 || site_count <= 0 || site_width.v <= 0) {
    logger_->metric("dpl_evolve__legalm_full__status", 0);
    return false;
  }
  const LegalmCpuCaps cpu_caps = resolveLegalmCpuCaps(context);
  const int stage3_partition_schemes
      = resolveLegalmStage3PartitionSchemes(context);
  const int stage3_rounds_per_scheme
      = resolveLegalmStage3RoundsPerScheme(context);

  std::vector<LegalmPlaceCell> cells;
  cells.reserve(network_->getNumCells());
  std::vector<SiteSymClass> site_sym_classes;
  auto site_sym_class_index
      = [&](odb::dbSite* site, const unsigned master_sym) {
          for (int i = 0; i < static_cast<int>(site_sym_classes.size()); ++i) {
            if (site_sym_classes[i].site == site
                && site_sym_classes[i].master_sym == master_sym) {
              return i;
            }
          }
          site_sym_classes.push_back({site, master_sym, {}, {}});
          return static_cast<int>(site_sym_classes.size()) - 1;
        };
  int skipped_cells = 0;
  int guided_cells = 0;
  int stage2_grid_handoff_cells = 0;
  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      ++skipped_cells;
      continue;
    }

    const DbuPt init = initialLocation(cell, false);
    DbuPt desired = init;
    const int id = cell->getId();
    const bool has_grid_handoff
        = id >= 0 && id < static_cast<int>(legalm_target_valid_.size())
          && legalm_target_valid_[id] != 0;
    const bool has_dbu_handoff
        = id >= 0 && id < static_cast<int>(guided_initial_valid_.size())
          && guided_initial_valid_[id] != 0;
    const bool guided = has_grid_handoff || has_dbu_handoff;
    if (has_dbu_handoff) {
      const auto& guided_loc = guided_initial_locations_[id];
      desired = {DbuX{guided_loc.first}, DbuY{guided_loc.second}};
    }

    GridPt desired_grid = legalGridPt(cell, desired);
    GridPt frontier_grid = desired_grid;
    if (has_grid_handoff) {
      desired_grid = GridPt{
          GridX{legalm_target_sites_[id]},
          GridY{legalm_target_rows_[id]},
      };
      ++stage2_grid_handoff_cells;
    }
    if (guided) {
      ++guided_cells;
    }
    const GridPt original_grid = legalGridPt(cell, init);
    const bool has_frontier_handoff
        = has_dbu_handoff
          && (frontier_grid.x != original_grid.x
              || frontier_grid.y != original_grid.y);
    const int width_sites = std::max(
        1,
        static_cast<int>(std::ceil(static_cast<double>(cell->getWidth().v)
                                   / static_cast<double>(site_width.v))));
    const int height_rows = std::max(1, grid_->gridHeight(cell).v);
    const unsigned master_sym = dpl_evolve::DetailedOrient::getMasterSymmetry(
        cell->getDbInst()->getMaster());
    cells.push_back(
        {cell,
         std::clamp(desired_grid.y.v, 0, row_count - 1),
         std::clamp(desired_grid.x.v, 0, std::max(0, site_count - width_sites)),
         std::clamp(frontier_grid.y.v, 0, row_count - 1),
         std::clamp(
             frontier_grid.x.v, 0, std::max(0, site_count - width_sites)),
         std::clamp(original_grid.y.v, 0, row_count - 1),
         std::clamp(
             original_grid.x.v, 0, std::max(0, site_count - width_sites)),
         width_sites,
         height_rows,
         height_rows % 2 == 0 ? 2 : 1,
         1.0,
         master_sym,
         cell->getMaster()->isMultiRow(),
         site_sym_class_index(cell->getSite(), master_sym),
         guided,
         has_frontier_handoff,
         cell->getGroup()});
  }

  if (cells.empty()) {
    logger_->metric("dpl_evolve__legalm_full__status", 0);
    return false;
  }

  const int prep_thread_count = std::max(
      1,
      std::min(context.max_threads > 0 ? context.max_threads : 1, row_count));
  for (SiteSymClass& cls : site_sym_classes) {
    cls.compatible.assign(
        static_cast<size_t>(row_count) * static_cast<size_t>(site_count), 0);
    cls.orientations.assign(
        static_cast<size_t>(row_count) * static_cast<size_t>(site_count),
        odb::dbOrientType::R0);
    parallelFor(row_count, prep_thread_count, [&](int begin, int end, int) {
      for (int row = begin; row < end; ++row) {
        for (int site = 0; site < site_count; ++site) {
          const auto orient
              = grid_->getSiteOrientation(GridX{site}, GridY{row}, cls.site);
          const size_t idx
              = (static_cast<size_t>(row) * static_cast<size_t>(site_count))
                + static_cast<size_t>(site);
          if (orient.has_value() && checkMasterSym(cls.master_sym, *orient)) {
            cls.compatible[idx] = 1;
            cls.orientations[idx] = orient->getValue();
          }
        }
      }
    });
  }

  int max_height_rows = 1;
  for (const LegalmPlaceCell& item : cells) {
    max_height_rows = std::max(max_height_rows, item.height_rows);
  }
  std::vector<int> height_class_counts(max_height_rows + 1, 0);
  for (const LegalmPlaceCell& item : cells) {
    ++height_class_counts[std::clamp(item.height_rows, 1, max_height_rows)];
  }
  int active_height_classes = 0;
  for (const int count : height_class_counts) {
    active_height_classes += count > 0 ? 1 : 0;
  }
  for (LegalmPlaceCell& item : cells) {
    const int class_count = std::max(
        1,
        height_class_counts[std::clamp(item.height_rows, 1, max_height_rows)]);
    item.height_class_weight = static_cast<double>(cells.size()) / class_count;
  }

  std::stable_sort(
      cells.begin(), cells.end(), [](const auto& lhs, const auto& rhs) {
        if (lhs.desired_row != rhs.desired_row) {
          return lhs.desired_row < rhs.desired_row;
        }
        if (lhs.desired_site != rhs.desired_site) {
          return lhs.desired_site < rhs.desired_site;
        }
        if (lhs.height_rows != rhs.height_rows) {
          return lhs.height_rows > rhs.height_rows;
        }
        if (lhs.width_sites != rhs.width_sites) {
          return lhs.width_sites > rhs.width_sites;
        }
        return lhs.cell->getId() < rhs.cell->getId();
      });

  const int row_height_dbu
      = arch_ != nullptr && arch_->getNumRows() > 0
            ? std::max(1, arch_->getRow(0)->getHeight().v)
            : std::max(1,
                       (row_count > 1 ? (grid_->gridYToDbu(GridY{1})
                                         - grid_->gridYToDbu(GridY{0}))
                                            .v
                                      : site_width.v));
  const int row_equiv_sites = std::max(
      1,
      static_cast<int>(std::llround(static_cast<double>(row_height_dbu)
                                    / static_cast<double>(site_width.v))));
  // LEGALM 2.0 combines weighted average displacement with a maximum
  // displacement tail term: w = w_am + alpha_max * w_max.
  constexpr double kMaxDispTailWeight = kPaper.alpha_max;
  constexpr double kPtech = kPaper.ptech;
  const bool paper_tech_penalty_enabled
      = drc_engine_ != nullptr && drc_engine_->hasCellEdgeSpacingTable();
  const int max_disp_threshold_sites = static_cast<int>(
      std::llround(kPaper.delta_threshold_rows * row_equiv_sites));

  auto placement_cost =
      [&](const LegalmPlaceCell& item, const int row, const int site) {
        const int original_delta
            = std::abs(site - item.original_site)
              + row_equiv_sites * std::abs(row - item.original_row);
        const int tail = std::max(0, original_delta - max_disp_threshold_sites);
        return item.height_class_weight * static_cast<double>(original_delta)
               + kMaxDispTailWeight * static_cast<double>(tail);
      };

  auto row_lower_bound = [&](const LegalmPlaceCell& item, const int row) {
    const int original_vertical
        = row_equiv_sites * std::abs(row - item.original_row);
    const int tail = std::max(0, original_vertical - max_disp_threshold_sites);
    return item.height_class_weight * static_cast<double>(original_vertical)
           + kMaxDispTailWeight * static_cast<double>(tail);
  };

  auto row_site_compatible = [&](const LegalmPlaceCell& item,
                                 const int row,
                                 const int x) {
    Node* cell = item.cell;
    if (cell == nullptr || cell->getSite() == nullptr || row < 0
        || row >= row_count || x < 0 || x >= site_count) {
      return false;
    }
    if (item.site_sym_class < 0
        || item.site_sym_class >= static_cast<int>(site_sym_classes.size())
        || site_sym_classes[item.site_sym_class]
                   .compatible[(static_cast<size_t>(row)
                                * static_cast<size_t>(site_count))
                               + static_cast<size_t>(x)]
               == 0) {
      return false;
    }
    if (item.master_multi_row && !checkRowPowerCompatible(cell, GridY{row})) {
      return false;
    }
    return true;
  };

  struct Interval
  {
    int first = 0;
    int second = 0;
    Group* group = nullptr;
  };
  std::vector<std::vector<Interval>> free_intervals(row_count);
  for (int row = 0; row < row_count; ++row) {
    int begin = -1;
    Group* active_group = nullptr;
    auto close_interval = [&](const int end) {
      if (begin < 0 || begin >= end) {
        begin = -1;
        active_group = nullptr;
        return;
      }
      free_intervals[row].push_back({begin, end, active_group});
      begin = -1;
      active_group = nullptr;
    };
    for (int site = 0; site < site_count; ++site) {
      const Pixel* pixel = grid_->gridPixel(GridX{site}, GridY{row});
      const bool free = pixel != nullptr && pixel->is_valid
                        && pixel->cell == nullptr
                        && pixel->padding_reserved_by == nullptr;
      Group* pixel_group = free ? pixel->group : nullptr;
      if (free && begin < 0) {
        begin = site;
        active_group = pixel_group;
      } else if ((!free || pixel_group != active_group) && begin >= 0) {
        close_interval(site);
        if (free) {
          begin = site;
          active_group = pixel_group;
        }
      }
    }
    if (begin >= 0) {
      close_interval(site_count);
    }
  };

  auto interval_contains = [](const std::vector<Interval>& intervals,
                              const int x,
                              const int width,
                              const Group* group) {
    const auto it
        = std::lower_bound(intervals.begin(),
                           intervals.end(),
                           x,
                           [](const Interval& interval, const int site) {
                             return interval.second <= site;
                           });
    if (it != intervals.end() && it->group == group && it->first <= x
        && x + width <= it->second) {
      return true;
    }
    return false;
  };

  auto fits_all_rows = [&](const int row,
                           const int x,
                           const int width,
                           const int height_rows,
                           const Group* group) {
    if (row < 0 || row + height_rows > row_count || x < 0
        || x + width > site_count) {
      return false;
    }
    for (int y = row; y < row + height_rows; ++y) {
      if (!interval_contains(free_intervals[y], x, width, group)) {
        return false;
      }
    }
    return true;
  };

  auto subtract_interval = [](std::vector<Interval>& intervals,
                              const int x,
                              const int width,
                              const Group* group) {
    const int x1 = x + width;
    std::vector<Interval> next;
    next.reserve(intervals.size() + 1);
    for (const auto& interval : intervals) {
      if (interval.group != group || x1 <= interval.first
          || x >= interval.second) {
        next.push_back(interval);
        continue;
      }
      if (interval.first < x) {
        next.push_back({interval.first, x, interval.group});
      }
      if (x1 < interval.second) {
        next.push_back({x1, interval.second, interval.group});
      }
    }
    intervals.swap(next);
  };

  auto add_interval = [](std::vector<Interval>& intervals,
                         const int x,
                         const int width,
                         const int site_limit,
                         Group* group) {
    Interval merged{std::clamp(x, 0, site_limit),
                    std::clamp(x + width, 0, site_limit),
                    group};
    if (merged.first >= merged.second) {
      return;
    }
    intervals.push_back(merged);
    std::stable_sort(intervals.begin(),
                     intervals.end(),
                     [](const Interval& lhs, const Interval& rhs) {
                       if (lhs.first != rhs.first) {
                         return lhs.first < rhs.first;
                       }
                       if (lhs.second != rhs.second) {
                         return lhs.second < rhs.second;
                       }
                       return lhs.group < rhs.group;
                     });
    std::vector<Interval> next;
    next.reserve(intervals.size());
    for (const Interval& interval : intervals) {
      if (!next.empty() && next.back().group == interval.group
          && interval.first <= next.back().second) {
        next.back().second = std::max(next.back().second, interval.second);
      } else {
        next.push_back(interval);
      }
    }
    intervals.swap(next);
  };

  auto reserve_slot = [&](const int row,
                          const int x,
                          const int width,
                          const int height_rows,
                          const Group* group) {
    for (int y = row; y < row + height_rows && y < row_count; ++y) {
      subtract_interval(free_intervals[y], x, width, group);
    }
  };

  auto release_slot = [&](const int row,
                          const int x,
                          const int width,
                          const int height_rows,
                          Group* group) {
    for (int y = row; y < row + height_rows && y < row_count; ++y) {
      add_interval(free_intervals[y], x, width, site_count, group);
    }
  };

  int64_t bounded_interval_fallbacks = 0;
  auto find_site_in_row = [&](const LegalmPlaceCell& item,
                              const int row,
                              const bool fast_static_path) {
    std::pair<int, double> best{-1, std::numeric_limits<double>::infinity()};
    if (row < 0 || row + item.height_rows > row_count) {
      return best;
    }
    const auto& intervals = free_intervals[row];
    auto candidate_ok = [&](const int candidate) {
      if (!interval_contains(
              intervals, candidate, item.width_sites, item.group)) {
        return false;
      }
      if (item.height_rows > 1
          && !fits_all_rows(
              row, candidate, item.width_sites, item.height_rows, item.group)) {
        return false;
      }
      if (!row_site_compatible(item, row, candidate)) {
        return false;
      }
      // During initial sequential placement the DPL grid is synchronized, so
      // exact OpenDP checks are safe when the maintained interval path is not
      // sufficient.
      return fast_static_path
             || canBePlaced(item.cell, GridX{candidate}, GridY{row});
    };
    if (row == item.desired_row && item.desired_site >= 0
        && item.desired_site + item.width_sites <= site_count
        && candidate_ok(item.desired_site)) {
      return std::pair<int, double>{
          item.desired_site, placement_cost(item, row, item.desired_site)};
    }
    std::array<int, 16> tried_sites{};
    int tried_count = 0;
    auto update_best_from_interval = [&](const Interval& interval,
                                         const int target_site) {
      if (interval.second - interval.first < item.width_sites) {
        return;
      }
      const int candidate = std::clamp(
          target_site, interval.first, interval.second - item.width_sites);
      for (int i = 0; i < tried_count; ++i) {
        if (tried_sites[i] == candidate) {
          return;
        }
      }
      if (tried_count < static_cast<int>(tried_sites.size())) {
        tried_sites[tried_count++] = candidate;
      }
      if (!candidate_ok(candidate)) {
        return;
      }
      const double cost = placement_cost(item, row, candidate);
      if (cost < best.second) {
        best = {candidate, cost};
      }
    };

    const std::array<int, 4> target_sites{
        item.desired_site,
        item.original_site,
        (item.desired_site + item.original_site) / 2,
        std::clamp(
            item.desired_site, 0, std::max(0, site_count - item.width_sites))};

    for (const int target_site : target_sites) {
      auto it
          = std::lower_bound(intervals.begin(),
                             intervals.end(),
                             target_site,
                             [](const Interval& interval, const int target) {
                               return interval.second <= target;
                             });

      for (auto fwd = it; fwd != intervals.end(); ++fwd) {
        update_best_from_interval(*fwd, target_site);
      }
      auto back = it;
      while (back != intervals.begin()) {
        --back;
        update_best_from_interval(*back, target_site);
      }
    }

    if (best.first >= 0) {
      return best;
    }

    ++bounded_interval_fallbacks;
    for (const auto& interval : intervals) {
      update_best_from_interval(interval, item.desired_site);
      update_best_from_interval(interval, item.original_site);
      update_best_from_interval(interval,
                                (item.desired_site + item.original_site) / 2);
    }
    return best;
  };

  int placed_cells = 0;
  int failed_cells = 0;
  int row_escape_count = 0;
  int guided_placed = 0;
  int64_t site_shift_sum = 0;
  int max_site_shift = 0;
  int interval_count = 0;
  for (const auto& row : free_intervals) {
    interval_count += static_cast<int>(row.size());
  }
  std::vector<int> current_rows(cells.size(), -1);
  std::vector<int> current_sites(cells.size(), -1);

  placement_failures_.clear();
  bool direct_target_path = guided_cells == static_cast<int>(cells.size());
  std::vector<unsigned char> direct_occupied(
      static_cast<size_t>(row_count) * static_cast<size_t>(site_count), 0);
  if (direct_target_path) {
    for (const LegalmPlaceCell& item : cells) {
      const bool fast_static_path = arch_->getRegions().empty()
                                    && item.height_rows == 1
                                    && !item.cell->inGroup();
      if (item.desired_row < 0
          || item.desired_row + item.height_rows > row_count
          || item.desired_site < 0
          || item.desired_site + item.width_sites > site_count
          || !fits_all_rows(item.desired_row,
                            item.desired_site,
                            item.width_sites,
                            item.height_rows,
                            item.group)
          || !row_site_compatible(item, item.desired_row, item.desired_site)
          || (!fast_static_path
              && !canBePlaced(item.cell,
                              GridX{item.desired_site},
                              GridY{item.desired_row}))) {
        direct_target_path = false;
        break;
      }
      for (int dy = 0; dy < item.height_rows; ++dy) {
        const int row = item.desired_row + dy;
        for (int dx = 0; dx < item.width_sites; ++dx) {
          const int site = item.desired_site + dx;
          const size_t idx
              = static_cast<size_t>(row) * static_cast<size_t>(site_count)
                + static_cast<size_t>(site);
          if (direct_occupied[idx] != 0) {
            direct_target_path = false;
            break;
          }
          direct_occupied[idx] = 1;
        }
        if (!direct_target_path) {
          break;
        }
      }
      if (!direct_target_path) {
        break;
      }
    }
  }

  if (direct_target_path) {
    for (int item_idx = 0; item_idx < static_cast<int>(cells.size());
         ++item_idx) {
      const LegalmPlaceCell& item = cells[item_idx];
      placeCell(item.cell, GridX{item.desired_site}, GridY{item.desired_row});
      reserve_slot(item.desired_row,
                   item.desired_site,
                   item.width_sites,
                   item.height_rows,
                   item.group);
      current_rows[item_idx] = item.desired_row;
      current_sites[item_idx] = item.desired_site;
      ++placed_cells;
      ++guided_placed;
      const int abs_shift = std::abs(item.desired_site - item.original_site);
      site_shift_sum += abs_shift;
      max_site_shift = std::max(max_site_shift, abs_shift);
    }
  } else {
    for (int item_idx = 0; item_idx < static_cast<int>(cells.size());
         ++item_idx) {
      const LegalmPlaceCell& item = cells[item_idx];
      int best_row = -1;
      int best_site = -1;
      double best_cost = std::numeric_limits<double>::infinity();
      const bool fast_static_path = arch_->getRegions().empty()
                                    && item.height_rows == 1
                                    && !item.cell->inGroup();
      auto try_row = [&](const int row) {
        if (row < 0 || row >= row_count) {
          return false;
        }
        const double lower_bound = row_lower_bound(item, row);
        if (best_site >= 0 && lower_bound > best_cost) {
          return true;
        }
        const auto [site, cost] = find_site_in_row(item, row, fast_static_path);
        if (site < 0) {
          return false;
        }
        if (cost < best_cost) {
          best_cost = cost;
          best_row = row;
          best_site = site;
        }
        if (row == item.desired_row && site == item.desired_site) {
          return true;
        }
        return false;
      };

      if (!try_row(item.desired_row)) {
        for (int delta = 1; delta < row_count; ++delta) {
          const int up = item.desired_row + delta;
          const int down = item.desired_row - delta;
          if (up >= row_count && down < 0) {
            break;
          }
          if (up < row_count && try_row(up)) {
            break;
          }
          if (down >= 0 && try_row(down)) {
            break;
          }
        }
      }

      if (best_row < 0 || best_site < 0) {
        ++failed_cells;
        placement_failures_.push_back(item.cell);
        continue;
      }

      placeCell(item.cell, GridX{best_site}, GridY{best_row});
      reserve_slot(
          best_row, best_site, item.width_sites, item.height_rows, item.group);
      current_rows[item_idx] = best_row;
      current_sites[item_idx] = best_site;
      ++placed_cells;
      if (item.guided) {
        ++guided_placed;
      }
      if (best_row != item.desired_row) {
        ++row_escape_count;
      }
      const int abs_shift = std::abs(best_site - item.original_site);
      site_shift_sum += abs_shift;
      max_site_shift = std::max(max_site_shift, abs_shift);
    }
  }

  int stage3_attempted = 0;
  int stage3_moved = 0;
  int stage3_row_moves = 0;
  int64_t stage3_candidate_evals = 0;
  int64_t stage3_overflow_rejects = 0;
  int64_t stage3_static_rejects = 0;
  int64_t stage3_tech_evals = 0;
  int64_t stage3_edge_spacing_terms = 0;
  int64_t stage3_pin_short_terms = 0;
  int64_t stage3_pin_access_terms = 0;
  int stage3_partitioned_cells = 0;
  int stage3_boundary_excluded_cells = 0;
  int stage3_rounds = 0;
  int stage3_early_stopped_rounds = 0;
  int stage3_last_round_moves = 0;
  int stage3_thread_count_metric = 0;
  std::atomic<int64_t> stage3_site_improvement{0};
  std::atomic<int> stage3_max_improvement{0};
  std::atomic<int> stage3_changed_cells{0};
  std::vector<unsigned char> stage3_changed(cells.size(), 0);
  const int dbu_per_micron
      = db_ != nullptr && db_->getTech() != nullptr
            ? std::max(1, db_->getTech()->getDbUnitsPerMicron())
            : 1000;
  const int stage3_xhint_sites = std::max(
      1,
      static_cast<int>(std::llround(kPaper.xhint_microns * dbu_per_micron
                                    / static_cast<double>(site_width.v))));
  const int stage3_yhint_rows
      = std::max(1, std::min(kPaper.yhint_rows, row_count));
  const int stage3_partition_cols = std::max(
      1, (site_count + stage3_xhint_sites - 1) / stage3_xhint_sites + 1);
  const int stage3_partition_rows = std::max(
      1, (row_count + stage3_yhint_rows - 1) / stage3_yhint_rows + 1);
  const int stage3_partition_count
      = stage3_partition_cols * stage3_partition_rows;
  const std::vector<LegalmCandidateOffset> stage3_stencil
      = legalmCandidateStencil(cpu_caps);

  if (failed_cells == 0) {
    auto shifted_partition
        = [](const int coord, const int shift, const int stride) {
            return (coord + shift) / std::max(1, stride);
          };

    auto inside_stage3_partition = [&](const int row,
                                       const int site,
                                       const int width,
                                       const int height,
                                       const int partition_col,
                                       const int partition_row,
                                       const int site_shift,
                                       const int row_shift) {
      if (row < 0 || row + height > row_count || site < 0
          || site + width > site_count) {
        return false;
      }
      return shifted_partition(site, site_shift, stage3_xhint_sites)
                 == partition_col
             && shifted_partition(
                    site + width - 1, site_shift, stage3_xhint_sites)
                    == partition_col
             && shifted_partition(row, row_shift, stage3_yhint_rows)
                    == partition_row
             && shifted_partition(
                    row + height - 1, row_shift, stage3_yhint_rows)
                    == partition_row;
    };

    auto stage3_base_priority = [&](const LegalmPlaceCell& item,
                                    const int row,
                                    const int site) {
      const int original_delta
          = std::abs(site - item.original_site)
            + row_equiv_sites * std::abs(row - item.original_row);
      const int tail = std::max(0, original_delta - max_disp_threshold_sites);
      return item.height_class_weight * static_cast<double>(original_delta)
             + kMaxDispTailWeight * static_cast<double>(tail);
    };

    auto stage3_priority = [&](const LegalmPlaceCell& item,
                               const int row,
                               const int site,
                               int64_t* tech_evals,
                               int64_t* edge_spacing_terms,
                               int64_t* pin_short_terms,
                               int64_t* pin_access_terms) {
      double cost = stage3_base_priority(item, row, site);
      if (paper_tech_penalty_enabled && tech_evals != nullptr
          && item.site_sym_class >= 0) {
        const size_t orient_idx
            = (static_cast<size_t>(row) * static_cast<size_t>(site_count))
              + static_cast<size_t>(site);
        const auto orient = odb::dbOrientType(
            site_sym_classes[item.site_sym_class].orientations[orient_idx]);
        const LegalmTechPenaltyResult tech_penalty = computeLegalmTechPenalty(
            drc_engine_.get(), item.cell, GridX{site}, GridY{row}, orient);
        ++(*tech_evals);
        if (edge_spacing_terms != nullptr) {
          *edge_spacing_terms += tech_penalty.edge_spacing_violations;
        }
        if (pin_short_terms != nullptr) {
          *pin_short_terms += tech_penalty.pin_short_violations;
        }
        if (pin_access_terms != nullptr) {
          *pin_access_terms += tech_penalty.pin_access_violations;
        }
        cost += tech_penalty.paperCost(kPtech, row_equiv_sites);
      }
      return cost;
    };

    auto best_lambda_infinity_candidate = [&](const LegalmPlaceCell& item,
                                              const int current_row,
                                              const int current_site,
                                              const double old_cost,
                                              const int partition_col,
                                              const int partition_row,
                                              const int site_shift,
                                              const int row_shift,
                                              int64_t& local_candidate_evals,
                                              int64_t& local_overflow_rejects,
                                              int64_t& local_static_rejects,
                                              int64_t& local_tech_evals,
                                              int64_t& local_edge_spacing_terms,
                                              int64_t& local_pin_short_terms,
                                              int64_t& local_pin_access_terms) {
      auto evaluate_lambda_infinity_candidate =
          [&](const int row, const int candidate, const double incumbent_cost) {
            ++local_candidate_evals;
            const double base_cost = stage3_base_priority(item, row, candidate);
            if (base_cost >= incumbent_cost) {
              return LegalmBgdEvaluation{};
            }
            if (!inside_stage3_partition(row,
                                         candidate,
                                         item.width_sites,
                                         item.height_rows,
                                         partition_col,
                                         partition_row,
                                         site_shift,
                                         row_shift)) {
              return LegalmBgdEvaluation{};
            }
            if (!fits_all_rows(row,
                               candidate,
                               item.width_sites,
                               item.height_rows,
                               item.group)) {
              ++local_overflow_rejects;
              return LegalmBgdEvaluation{};
            }
            if (!row_site_compatible(item, row, candidate)) {
              ++local_static_rejects;
              return LegalmBgdEvaluation{};
            }
            const double cost = stage3_priority(item,
                                                row,
                                                candidate,
                                                &local_tech_evals,
                                                &local_edge_spacing_terms,
                                                &local_pin_short_terms,
                                                &local_pin_access_terms);
            return LegalmBgdEvaluation{cost < incumbent_cost, cost};
          };

      return legalmBestBgdCandidate(stage3_stencil,
                                    current_row,
                                    current_site,
                                    item.vertical_step_rows,
                                    item.width_sites,
                                    item.height_rows,
                                    row_count,
                                    site_count,
                                    old_cost,
                                    evaluate_lambda_infinity_candidate);
    };

    for (int scheme = 0; scheme < stage3_partition_schemes; ++scheme) {
      std::vector<std::mutex> row_mutexes(row_count);
      const int site_shift
          = (scheme * stage3_xhint_sites) / stage3_partition_schemes;
      const int row_shift
          = (scheme * stage3_yhint_rows) / stage3_partition_schemes;
      for (int scheme_round = 0; scheme_round < stage3_rounds_per_scheme;
           ++scheme_round) {
        ++stage3_rounds;
        std::vector<std::vector<int>> partitions(stage3_partition_count);
        std::vector<double> round_priorities(cells.size(), 0.0);
        int round_partitioned_cells = 0;
        int round_boundary_excluded_cells = 0;

        for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
          const LegalmPlaceCell& item = cells[i];
          const int row = current_rows[i];
          const int site = current_sites[i];
          if (!item.cell->isPlaced() || row < 0 || site < 0) {
            continue;
          }
          if (row < 0 || row + item.height_rows > row_count || site < 0
              || site + item.width_sites > site_count) {
            ++round_boundary_excluded_cells;
            continue;
          }
          const double priority = stage3_priority(
              item, row, site, nullptr, nullptr, nullptr, nullptr);
          if (priority <= 0.0) {
            continue;
          }
          round_priorities[i] = priority;
          const int col0
              = shifted_partition(site, site_shift, stage3_xhint_sites);
          const int col1 = shifted_partition(
              site + item.width_sites - 1, site_shift, stage3_xhint_sites);
          const int row0 = shifted_partition(row, row_shift, stage3_yhint_rows);
          const int row1 = shifted_partition(
              row + item.height_rows - 1, row_shift, stage3_yhint_rows);
          if (col0 != col1 || row0 != row1 || col0 < 0 || row0 < 0
              || col0 >= stage3_partition_cols
              || row0 >= stage3_partition_rows) {
            ++round_boundary_excluded_cells;
            continue;
          }
          partitions[(row0 * stage3_partition_cols) + col0].push_back(i);
          ++round_partitioned_cells;
        }

        int round_moved = 0;
        const int stage3_thread_count = std::max(
            1, std::min(context.max_threads, stage3_partition_count));
        stage3_thread_count_metric
            = std::max(stage3_thread_count_metric, stage3_thread_count);
        std::vector<int> local_attempted(stage3_thread_count, 0);
        std::vector<int> local_moved(stage3_thread_count, 0);
        std::vector<int> local_row_moves(stage3_thread_count, 0);
        std::vector<int64_t> local_improvements(stage3_thread_count, 0);
        std::vector<int> local_max_improvements(stage3_thread_count, 0);
        std::vector<int64_t> local_candidate_evals(stage3_thread_count, 0);
        std::vector<int64_t> local_overflow_rejects(stage3_thread_count, 0);
        std::vector<int64_t> local_static_rejects(stage3_thread_count, 0);
        std::vector<int64_t> local_tech_evals(stage3_thread_count, 0);
        std::vector<int64_t> local_edge_spacing_terms(stage3_thread_count, 0);
        std::vector<int64_t> local_pin_short_terms(stage3_thread_count, 0);
        std::vector<int64_t> local_pin_access_terms(stage3_thread_count, 0);

        for (auto& partition : partitions) {
          std::stable_sort(partition.begin(),
                           partition.end(),
                           [&](const int lhs, const int rhs) {
                             const auto& a = cells[lhs];
                             const auto& b = cells[rhs];
                             const double a_priority = round_priorities[lhs];
                             const double b_priority = round_priorities[rhs];
                             if (a_priority != b_priority) {
                               return a_priority > b_priority;
                             }
                             return a.cell->getId() < b.cell->getId();
                           });
        }

        parallelFor(
            stage3_partition_count,
            stage3_thread_count,
            [&](int begin, int end, int worker) {
              int attempted = 0;
              int moved = 0;
              int row_moves = 0;
              int64_t improvements = 0;
              int max_improvement = 0;
              int64_t candidate_evals = 0;
              int64_t overflow_rejects = 0;
              int64_t static_rejects = 0;
              int64_t tech_evals = 0;
              int64_t edge_spacing_terms = 0;
              int64_t pin_short_terms = 0;
              int64_t pin_access_terms = 0;
              std::vector<std::unique_lock<std::mutex>> locks;
              locks.reserve(max_height_rows
                            + 2 * cpu_caps.candidate_vertical_radius);
              for (int part = begin; part < end; ++part) {
                for (const int idx : partitions[part]) {
                  const LegalmPlaceCell& item = cells[idx];
                  const int row = current_rows[idx];
                  const int current_site = current_sites[idx];
                  if (!item.cell->isPlaced() || row < 0 || current_site < 0) {
                    continue;
                  }
                  if (row < 0 || row + item.height_rows > row_count
                      || current_site < 0
                      || current_site + item.width_sites > site_count) {
                    continue;
                  }
                  const int partition_col = shifted_partition(
                      current_site, site_shift, stage3_xhint_sites);
                  const int partition_row
                      = shifted_partition(row, row_shift, stage3_yhint_rows);
                  if (!inside_stage3_partition(row,
                                               current_site,
                                               item.width_sites,
                                               item.height_rows,
                                               partition_col,
                                               partition_row,
                                               site_shift,
                                               row_shift)) {
                    continue;
                  }

                  const double old_cost = stage3_priority(item,
                                                          row,
                                                          current_site,
                                                          &tech_evals,
                                                          &edge_spacing_terms,
                                                          &pin_short_terms,
                                                          &pin_access_terms);
                  ++attempted;
                  const int lock_begin
                      = std::max(0, row - cpu_caps.candidate_vertical_radius);
                  const int lock_end
                      = std::min(row_count - 1,
                                 row + item.height_rows - 1
                                     + cpu_caps.candidate_vertical_radius);
                  locks.clear();
                  for (int lock_row = lock_begin; lock_row <= lock_end;
                       ++lock_row) {
                    locks.emplace_back(row_mutexes[lock_row]);
                  }
                  release_slot(row,
                               current_site,
                               item.width_sites,
                               item.height_rows,
                               item.group);
                  const LegalmBgdCandidate candidate
                      = best_lambda_infinity_candidate(item,
                                                       row,
                                                       current_site,
                                                       old_cost,
                                                       partition_col,
                                                       partition_row,
                                                       site_shift,
                                                       row_shift,
                                                       candidate_evals,
                                                       overflow_rejects,
                                                       static_rejects,
                                                       tech_evals,
                                                       edge_spacing_terms,
                                                       pin_short_terms,
                                                       pin_access_terms);
                  if (candidate.site >= 0 && candidate.cost < old_cost) {
                    reserve_slot(candidate.row,
                                 candidate.site,
                                 item.width_sites,
                                 item.height_rows,
                                 item.group);
                    current_rows[idx] = candidate.row;
                    current_sites[idx] = candidate.site;
                    if (stage3_changed[idx] == 0) {
                      stage3_changed[idx] = 1;
                      ++stage3_changed_cells;
                    }
                    const int old_disp
                        = std::abs(current_site - item.original_site)
                          + row_equiv_sites * std::abs(row - item.original_row);
                    const int new_disp
                        = std::abs(candidate.site - item.original_site)
                          + row_equiv_sites
                                * std::abs(candidate.row - item.original_row);
                    const int improvement = old_disp - new_disp;
                    improvements += improvement;
                    max_improvement = std::max(max_improvement, improvement);
                    ++moved;
                    if (candidate.row != row) {
                      ++row_moves;
                    }
                  } else {
                    reserve_slot(row,
                                 current_site,
                                 item.width_sites,
                                 item.height_rows,
                                 item.group);
                  }
                  locks.clear();
                }
              }
              local_attempted[worker] = attempted;
              local_moved[worker] = moved;
              local_row_moves[worker] = row_moves;
              local_improvements[worker] = improvements;
              local_max_improvements[worker] = max_improvement;
              local_candidate_evals[worker] = candidate_evals;
              local_overflow_rejects[worker] = overflow_rejects;
              local_static_rejects[worker] = static_rejects;
              local_tech_evals[worker] = tech_evals;
              local_edge_spacing_terms[worker] = edge_spacing_terms;
              local_pin_short_terms[worker] = pin_short_terms;
              local_pin_access_terms[worker] = pin_access_terms;
            });
        round_moved
            = std::accumulate(local_moved.begin(), local_moved.end(), 0);
        stage3_attempted += std::accumulate(
            local_attempted.begin(), local_attempted.end(), 0);
        stage3_moved += round_moved;
        stage3_row_moves += std::accumulate(
            local_row_moves.begin(), local_row_moves.end(), 0);
        stage3_site_improvement.fetch_add(
            std::accumulate(local_improvements.begin(),
                            local_improvements.end(),
                            int64_t{0}),
            std::memory_order_relaxed);
        stage3_candidate_evals += std::accumulate(local_candidate_evals.begin(),
                                                  local_candidate_evals.end(),
                                                  int64_t{0});
        stage3_overflow_rejects
            += std::accumulate(local_overflow_rejects.begin(),
                               local_overflow_rejects.end(),
                               int64_t{0});
        stage3_static_rejects += std::accumulate(local_static_rejects.begin(),
                                                 local_static_rejects.end(),
                                                 int64_t{0});
        stage3_tech_evals += std::accumulate(
            local_tech_evals.begin(), local_tech_evals.end(), int64_t{0});
        stage3_edge_spacing_terms
            += std::accumulate(local_edge_spacing_terms.begin(),
                               local_edge_spacing_terms.end(),
                               int64_t{0});
        stage3_pin_short_terms += std::accumulate(local_pin_short_terms.begin(),
                                                  local_pin_short_terms.end(),
                                                  int64_t{0});
        stage3_pin_access_terms
            += std::accumulate(local_pin_access_terms.begin(),
                               local_pin_access_terms.end(),
                               int64_t{0});
        const int round_max_improvement = *std::max_element(
            local_max_improvements.begin(), local_max_improvements.end());
        int observed_max = stage3_max_improvement.load();
        while (round_max_improvement > observed_max
               && !stage3_max_improvement.compare_exchange_weak(
                   observed_max, round_max_improvement)) {
        }
        stage3_partitioned_cells += round_partitioned_cells;
        stage3_boundary_excluded_cells += round_boundary_excluded_cells;
        stage3_last_round_moves = round_moved;
        if (round_moved == 0) {
          stage3_early_stopped_rounds
              += stage3_rounds_per_scheme - scheme_round - 1;
          break;
        }
      }
    }

    for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
      if (stage3_changed[idx] == 0) {
        continue;
      }
      unplaceCell(cells[idx].cell);
    }
    for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
      if (stage3_changed[idx] == 0) {
        continue;
      }
      placeCell(
          cells[idx].cell, GridX{current_sites[idx]}, GridY{current_rows[idx]});
    }
  }

  int frontier_eligible_cells = 0;
  int frontier_cells = 0;
  int frontier_row_residual_cells = 0;
  int frontier_max_residual_sites = 0;
  int frontier_max_residual_rows = 0;
  int frontier_released_cells = 0;
  int frontier_tail_filtered_cells = 0;
  double frontier_total_residual_sites = 0.0;
  double frontier_total_release_sites = 0.0;
  double frontier_total_score = 0.0;
  clearLegalmFrontier();
  if (failed_cells == 0) {
    for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
      const LegalmPlaceCell& item = cells[idx];
      if (item.cell == nullptr || item.cell->getDbInst() == nullptr
          || !item.cell->isPlaced() || current_rows[idx] < 0
          || current_sites[idx] < 0) {
        continue;
      }
      if (!item.frontier_target) {
        continue;
      }
      ++frontier_eligible_cells;
      const int residual_sites = item.frontier_site - current_sites[idx];
      const int residual_rows = item.frontier_row - current_rows[idx];
      const int current_disp_sites
          = std::abs(current_sites[idx] - item.original_site)
            + row_equiv_sites
                  * std::abs(current_rows[idx] - item.original_row);
      const int residual_norm_sites
          = std::abs(residual_sites)
            + row_equiv_sites * std::abs(residual_rows);
      const int release_norm_sites
          = std::abs(item.frontier_site - item.desired_site)
            + row_equiv_sites * std::abs(item.frontier_row - item.desired_row);
      const int displacement_tail
          = std::max(0, current_disp_sites - max_disp_threshold_sites);
      if (residual_norm_sites < 2) {
        continue;
      }
      const double score = 1.75 * static_cast<double>(residual_norm_sites)
                           + 0.35 * static_cast<double>(release_norm_sites)
                           + (residual_rows != 0 ? 0.5 * row_equiv_sites : 0.0)
                           - 0.50 * static_cast<double>(displacement_tail);
      if (score <= 0.0) {
        ++frontier_tail_filtered_cells;
        continue;
      }
      const DbuX current_x = gridToDbu(GridX{current_sites[idx]}, site_width);
      const DbuY current_y = grid_->gridYToDbu(GridY{current_rows[idx]});
      const DbuX target_x = gridToDbu(GridX{item.frontier_site}, site_width);
      const DbuY target_y = grid_->gridYToDbu(GridY{item.frontier_row});
      const DbuX original_x = gridToDbu(GridX{item.original_site}, site_width);
      const DbuY original_y = grid_->gridYToDbu(GridY{item.original_row});
      recordLegalmFrontierCell(item.cell->getDbInst(),
                               current_x,
                               current_y,
                               target_x,
                               target_y,
                               original_x,
                               original_y,
                               residual_sites,
                               residual_rows,
                               score);
      ++frontier_cells;
      frontier_row_residual_cells += residual_rows != 0 ? 1 : 0;
      frontier_released_cells += release_norm_sites > 0 ? 1 : 0;
      frontier_max_residual_sites
          = std::max(frontier_max_residual_sites, std::abs(residual_sites));
      frontier_max_residual_rows
          = std::max(frontier_max_residual_rows, std::abs(residual_rows));
      frontier_total_residual_sites += residual_norm_sites;
      frontier_total_release_sites += release_norm_sites;
      frontier_total_score += score;
    }
  }

  const bool ok = failed_cells == 0;
  logger_->metric("dpl_evolve__legalm_full__status", ok ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__eligible_cells",
                  static_cast<int>(cells.size()));
  logger_->metric("dpl_evolve__legalm_full__skipped_cells", skipped_cells);
  logger_->metric("dpl_evolve__legalm_full__guided_cells", guided_cells);
  logger_->metric("dpl_evolve__legalm_full__stage2_grid_handoff_cells",
                  stage2_grid_handoff_cells);
  logger_->metric("dpl_evolve__legalm_full__stage2_final_overflow_bins",
                  legalm_stage2_final_overflow_bins_);
  logger_->metric("dpl_evolve__legalm_full__stage2_final_overflow_sites",
                  legalm_stage2_final_overflow_sites_);
  logger_->metric("dpl_evolve__legalm_full__stage2_final_max_density",
                  legalm_stage2_final_max_density_);
  logger_->metric("dpl_evolve__legalm_full__stage2_overflow_free_handoff",
                  legalm_stage2_overflow_free_ ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__stage2_to_stage3_state_continuity",
                  stage2_grid_handoff_cells == static_cast<int>(cells.size())
                          && legalm_stage2_overflow_free_
                      ? 1
                      : 0);
  logger_->metric("dpl_evolve__legalm_full__shared_bgd_driver", 1);
  logger_->metric("dpl_evolve__legalm_full__guided_placed", guided_placed);
  logger_->metric("dpl_evolve__legalm_full__placed_cells", placed_cells);
  logger_->metric("dpl_evolve__legalm_full__failed_cells", failed_cells);
  logger_->metric("dpl_evolve__legalm_full__initial_free_intervals",
                  interval_count);
  logger_->metric("dpl_evolve__legalm_full__row_escape_count",
                  row_escape_count);
  logger_->metric("dpl_evolve__legalm_full__avg_site_shift",
                  placed_cells == 0 ? 0.0
                                    : static_cast<double>(site_shift_sum)
                                          / static_cast<double>(placed_cells));
  logger_->metric("dpl_evolve__legalm_full__max_site_shift", max_site_shift);
  logger_->metric("dpl_evolve__legalm_full__row_equiv_sites", row_equiv_sites);
  logger_->metric("dpl_evolve__legalm_full__max_disp_threshold_sites",
                  max_disp_threshold_sites);
  logger_->metric("dpl_evolve__legalm_full__height_classes",
                  active_height_classes);
  logger_->metric("dpl_evolve__legalm_full__direct_target_path",
                  direct_target_path ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__greedy_interval_bridge_used",
                  direct_target_path ? 0 : 1);
  logger_->metric("dpl_evolve__legalm_full__paper_alpha_max",
                  kMaxDispTailWeight);
  logger_->metric("dpl_evolve__legalm_full__paper_ptech", kPtech);
  logger_->metric("dpl_evolve__legalm_full__paper_eq35_soft_penalty_enabled",
                  paper_tech_penalty_enabled ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__paper_eq35_candidate_evals",
                  static_cast<double>(stage3_tech_evals));
  logger_->metric("dpl_evolve__legalm_full__paper_eq35_edge_spacing_terms",
                  static_cast<double>(stage3_edge_spacing_terms));
  logger_->metric("dpl_evolve__legalm_full__paper_eq35_pin_short_terms",
                  static_cast<double>(stage3_pin_short_terms));
  logger_->metric("dpl_evolve__legalm_full__paper_eq35_pin_access_terms",
                  static_cast<double>(stage3_pin_access_terms));
  logger_->metric("dpl_evolve__legalm_full__paper_delta_threshold_rows", 3);
  logger_->metric("dpl_evolve__legalm_full__bounded_interval_fallbacks",
                  static_cast<double>(bounded_interval_fallbacks));
  logger_->metric("dpl_evolve__legalm_stage3__attempted_cells",
                  stage3_attempted);
  logger_->metric("dpl_evolve__legalm_stage3__moved_cells", stage3_moved);
  logger_->metric("dpl_evolve__legalm_stage3__row_moves", stage3_row_moves);
  logger_->metric("dpl_evolve__legalm_stage3__candidate_evals",
                  static_cast<double>(stage3_candidate_evals));
  logger_->metric("dpl_evolve__legalm_stage3__overflow_rejects",
                  static_cast<double>(stage3_overflow_rejects));
  logger_->metric("dpl_evolve__legalm_stage3__static_rejects",
                  static_cast<double>(stage3_static_rejects));
  logger_->metric("dpl_evolve__legalm_stage3__rounds", stage3_rounds);
  logger_->metric("dpl_evolve__legalm_stage3__early_stopped_rounds",
                  stage3_early_stopped_rounds);
  logger_->metric("dpl_evolve__legalm_stage3__partition_schemes",
                  stage3_partition_schemes);
  logger_->metric("dpl_evolve__legalm_stage3__rounds_per_scheme",
                  stage3_rounds_per_scheme);
  logger_->metric("dpl_evolve__legalm_stage3__threads",
                  stage3_thread_count_metric);
  logger_->metric("dpl_evolve__legalm_stage3__candidate_vertical_radius",
                  cpu_caps.candidate_vertical_radius);
  logger_->metric("dpl_evolve__legalm_stage3__candidate_horizontal_steps",
                  cpu_caps.candidate_horizontal_steps);
  logger_->metric("dpl_evolve__legalm_stage3__candidate_stencil_size",
                  static_cast<int>(stage3_stencil.size()));
  logger_->metric("dpl_evolve__legalm_stage3__partitioned_cells",
                  stage3_partitioned_cells);
  logger_->metric("dpl_evolve__legalm_stage3__boundary_excluded_cells",
                  stage3_boundary_excluded_cells);
  logger_->metric("dpl_evolve__legalm_stage3__xhint_sites", stage3_xhint_sites);
  logger_->metric("dpl_evolve__legalm_stage3__yhint_rows", stage3_yhint_rows);
  logger_->metric("dpl_evolve__legalm_stage3__paper_xhint_microns",
                  kPaper.xhint_microns);
  logger_->metric("dpl_evolve__legalm_stage3__paper_yhint_rows",
                  kPaper.yhint_rows);
  logger_->metric(
      "dpl_evolve__legalm_stage3__cpu_cap_candidate_vertical_radius",
      cpu_caps.candidate_vertical_radius);
  logger_->metric(
      "dpl_evolve__legalm_stage3__cpu_cap_candidate_horizontal_steps",
      cpu_caps.candidate_horizontal_steps);
  logger_->metric("dpl_evolve__legalm_stage3__last_round_moves",
                  stage3_last_round_moves);
  logger_->metric("dpl_evolve__legalm_stage3__site_improvement",
                  static_cast<double>(stage3_site_improvement.load()));
  logger_->metric("dpl_evolve__legalm_stage3__max_site_improvement",
                  stage3_max_improvement.load());
  logger_->metric("dpl_evolve__legalm_stage3__lambda_infinity_bgd", 1);
  logger_->metric("dpl_evolve__legalm_frontier__eligible_cells",
                  frontier_eligible_cells);
  logger_->metric("dpl_evolve__legalm_frontier__cells", frontier_cells);
  logger_->metric("dpl_evolve__legalm_frontier__row_residual_cells",
                  frontier_row_residual_cells);
  logger_->metric("dpl_evolve__legalm_frontier__released_cells",
                  frontier_released_cells);
  logger_->metric("dpl_evolve__legalm_frontier__tail_filtered_cells",
                  frontier_tail_filtered_cells);
  logger_->metric("dpl_evolve__legalm_frontier__max_residual_sites",
                  frontier_max_residual_sites);
  logger_->metric("dpl_evolve__legalm_frontier__max_residual_rows",
                  frontier_max_residual_rows);
  logger_->metric("dpl_evolve__legalm_frontier__avg_residual_sites",
                  frontier_cells == 0
                      ? 0.0
                      : frontier_total_residual_sites
                            / static_cast<double>(frontier_cells));
  logger_->metric("dpl_evolve__legalm_frontier__avg_release_sites",
                  frontier_cells == 0
                      ? 0.0
                      : frontier_total_release_sites
                            / static_cast<double>(frontier_cells));
  logger_->metric("dpl_evolve__legalm_frontier__avg_score",
                  frontier_cells == 0
                      ? 0.0
                      : frontier_total_score
                            / static_cast<double>(frontier_cells));
  logger_->metric("dpl_evolve__legalm_frontier__producer_consumer_handoff",
                  frontier_cells > 0 ? 1 : 0);
  logger_->info(DPL,
                1214,
                "LEGALM full legalization placed {} / {} cells self-legally "
                "(guided {}, row escapes {}, failures {}, stage3 moved {}, "
                "stage3 row moves {}, stage3 evals {}, frontier {}, released "
                "avg {:.2f}).",
                placed_cells,
                cells.size(),
                guided_placed,
                row_escape_count,
                failed_cells,
                stage3_moved,
                stage3_row_moves,
                stage3_candidate_evals,
                frontier_cells,
                frontier_cells == 0
                    ? 0.0
                    : frontier_total_release_sites
                          / static_cast<double>(frontier_cells));

  if (ok) {
    reportEvolvePlacementMetrics("legalm_full");
  }
  clearGuidedInitialLocations();
  return ok;
}

}  // namespace dpl_evolve
