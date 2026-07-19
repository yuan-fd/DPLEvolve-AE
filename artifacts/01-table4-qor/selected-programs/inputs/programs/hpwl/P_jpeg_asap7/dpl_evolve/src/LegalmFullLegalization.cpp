// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <atomic>
#include <mutex>
#include <unordered_map>

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
  int original_row = 0;
  int original_site = 0;
  int width_sites = 1;
  int height_rows = 1;
  int vertical_step_rows = 1;
  double height_class_weight = 1.0;
  unsigned master_sym = 0;
  bool master_multi_row = false;
  int site_sym_class = -1;
  std::vector<LegalmHpwlTerm> hpwl_terms;
  bool guided = false;
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
         legalmBuildHpwlTerms(cell),
         guided,
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
  int64_t total_hpwl_proxy_terms = 0;
  for (const LegalmPlaceCell& item : cells) {
    total_hpwl_proxy_terms += static_cast<int64_t>(item.hpwl_terms.size());
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
  constexpr double kHpwlRegressionPenaltyWeight = 0.08;
  constexpr double kHpwlDeltaRewardWeight = 0.18;
  constexpr double kExtremeDispTailWeight = 1.25;
  constexpr int kIntervalNeighborProbeLimit = 5;
  constexpr int kMaxIntervalCandidateSites = 128;
  constexpr int kAssignmentHpwlProjectionSiteCap = 4;
  constexpr int kAssignmentHpwlProjectionMinCells = 100000;
  constexpr double kLowResidualThresholdScale = 1.0;
  constexpr double kHighResidualThresholdScale = 0.30;
  constexpr double kTailEscapeThresholdScale = 0.35;
  constexpr double kPressureReliefRewardWeight = 3.25;
  constexpr double kPressureDeficitPenaltyWeight = 1.10;
  constexpr double kPressureHpwlSwitchBaseScale = 0.35;
  constexpr double kPressureHpwlExtraDispScale = 0.85;
  constexpr double kPressureHpwlExtremeTailScale = 0.90;
  constexpr double kPressureHpwlReliefScale = 0.65;
  constexpr double kPressureHpwlBudgetScale = 0.50;
  constexpr int kLowResidualRefinementFrontierCap = 10000;
  constexpr int kLowResidualRefinementIntervalProbeLimit = 3;
  constexpr int kLowResidualRefinementCandidateCap = 128;
  constexpr int kLowResidualRefinementNetPinLimit = 100;
  constexpr int kLowResidualCurrentNetAnchorCap = 2;
  constexpr double kLowResidualExactHpwlWeight = 0.20;
  constexpr double kLowResidualExactHpwlMinGainSites = 0.25;
  constexpr double kLowResidualCurrentNetAnchorMinGainSites = 3.0;
  constexpr double kLowResidualCurrentNetAnchorHpwlWeight = 0.24;
  constexpr int kLowResidualChainMaxCells = 7;
  constexpr int kLowResidualChainMaxSpanSites = 36;
  constexpr double kLowResidualChainHpwlWeight = 0.38;
  constexpr int kLowResidualTargetCorrectionFrontierCap = 3072;
  constexpr int kLowResidualTargetCorrectionProbeCap = 6;
  constexpr int kLowResidualTargetCorrectionExactScoreCap = 560;
  constexpr double kLowResidualTargetCorrectionWeakGainSites = 0.20;
  constexpr int kLowResidualTargetReleaseFrontierCap = 1536;
  constexpr int kLowResidualTargetReleaseOwnerProbeCap = 6;
  constexpr int kLowResidualTargetReleaseExactScoreCap = 512;
  constexpr double kLowResidualTargetReleaseHpwlWeight = 0.30;
  constexpr int kHighPressureTailFrontierCap = 4096;
  constexpr int kHighPressureTailCandidateCap = 96;
  constexpr int kHighPressureTailIntervalProbeLimit = 3;
  constexpr int kHighPressureTailNetPinLimit = 100;
  constexpr int kHighPressureTailChainMaxCells = 8;
  constexpr int kHighPressureTailChainMaxSpanSites = 56;
  constexpr int kHighPressureTailGapChainItemProbeLimit = 4;
  constexpr int kHighPressureTailReliefRowSearch = 6;
  constexpr int kHighPressureTailReliefRowAnchorCap = 2;
  constexpr int kHighPressureEndpointReservoirCap = 96;
  constexpr int kHighPressureEndpointReliefRowSearch = 18;
  constexpr int kHighPressureEndpointExtraRowAnchorCap = 2;
  constexpr int kHighPressureEndpointSiteAnchorCap = 14;
  constexpr int kHighPressureEndpointHpwlAnchorCap = 5;
  constexpr int kHighPressureEndpointHpwlRowAnchorCap = 0;
  constexpr int kHighPressureTopMaxOwnerCap = 4;
  constexpr int kHighPressureTopMaxFreeProbeLimit = 4;
  constexpr int kHighPressureTopMaxTwoCellCandidateCap = 48;
  constexpr double kHighPressureTailHpwlMaxLossSites = 0.0;
  constexpr double kHighPressureTailHpwlWeight = 0.52;
  constexpr double kHighPressureTailBinReliefBonus = 6.0;
  constexpr double kHighPressureEndpointMaxReliefWeight = 0.18;
  constexpr double kHighPressureEndpointHpwlAnchorMinGainSites = 1.0;
  constexpr double kHighPressureEndpointHpwlCreditMinGainSites = 2.0;
  constexpr double kHighPressureEndpointHpwlCreditExactMinGainSites = 1.0;
  constexpr double kHighPressureEndpointHpwlCreditDispScale = 0.15;
  constexpr double kHighPressureEndpointHpwlCreditDispSlack = 2.0;
  constexpr double kHighPressureEndpointHpwlSiteTailMinGainSites = 25.0;
  constexpr double kHighPressureTopMaxReleaseDispSlack = 0.35;
  constexpr double kHighPressureTopMaxReleaseReliefWeight = 0.12;
  const bool paper_tech_penalty_enabled
      = drc_engine_ != nullptr && drc_engine_->hasCellEdgeSpacingTable();
  const int max_disp_threshold_sites = static_cast<int>(
      std::llround(kPaper.delta_threshold_rows * row_equiv_sites));
  const double max_hpwl_regression_penalty_sites
      = 4.0 * static_cast<double>(std::max(1, max_disp_threshold_sites));
  const double max_hpwl_delta_sites = max_hpwl_regression_penalty_sites;
  const int low_residual_site_limit
      = std::max(16, static_cast<int>(cells.size() / 2000));
  const int low_residual_bin_limit
      = std::max(16, std::max(1, row_count / 16));
  const bool low_residual_policy
      = legalm_stage2_final_overflow_sites_ <= low_residual_site_limit
        && legalm_stage2_final_overflow_bins_ <= low_residual_bin_limit;
  const bool low_residual_guided_hpwl_assignment
      = low_residual_policy && legalm_stage2_final_overflow_sites_ > 0
        && static_cast<int>(cells.size()) >= kAssignmentHpwlProjectionMinCells;
  const bool pressure_can_spend_hpwl_escape_budget
      = !low_residual_policy && legalm_stage2_final_overflow_sites_ > 0;
  const bool assignment_hpwl_projection_enabled
      = low_residual_guided_hpwl_assignment;
  const int target_row_neighborhood
      = low_residual_policy
            ? 1
            : std::max(1, std::min(2, cpu_caps.candidate_vertical_radius));
  const int row_escape_budget
      = low_residual_policy
            ? std::max(32, static_cast<int>(cells.size() / 12))
            : std::max(64,
                       static_cast<int>((2 * cells.size()) / 3)
                           + 4 * legalm_stage2_final_overflow_sites_);
  const double base_hpwl_escape_threshold
      = std::max(1.0,
                 static_cast<double>(std::max(1, max_disp_threshold_sites))
                     * (low_residual_policy ? kLowResidualThresholdScale
                                            : kHighResidualThresholdScale));

  auto hpwl_regression_penalty = [&](const LegalmPlaceCell& item,
                                     const int row,
                                     const int site) {
    if (item.hpwl_terms.empty()) {
      return 0.0;
    }
    const int64_t left = gridToDbu(GridX{site}, site_width).v;
    const int64_t bottom = grid_->gridYToDbu(GridY{row}).v;
    const int64_t center_x
        = left + static_cast<int64_t>(item.cell->getWidth().v) / 2;
    const int64_t center_y
        = bottom + static_cast<int64_t>(item.cell->getHeight().v) / 2;
    return std::min(legalmHpwlRegressionPenaltySites(item.hpwl_terms,
                                                     center_x,
                                                     center_y,
                                                     site_width.v),
                    max_hpwl_regression_penalty_sites);
  };

  auto hpwl_delta_sites = [&](const LegalmPlaceCell& item,
                              const int row,
                              const int site) {
    if (item.hpwl_terms.empty()) {
      return 0.0;
    }
    const int64_t left = gridToDbu(GridX{site}, site_width).v;
    const int64_t bottom = grid_->gridYToDbu(GridY{row}).v;
    const int64_t center_x
        = left + static_cast<int64_t>(item.cell->getWidth().v) / 2;
    const int64_t center_y
        = bottom + static_cast<int64_t>(item.cell->getHeight().v) / 2;
    return std::clamp(legalmHpwlDeltaSites(item.hpwl_terms,
                                           center_x,
                                           center_y,
                                           site_width.v),
                      -max_hpwl_delta_sites,
                      max_hpwl_delta_sites);
  };

  auto displacement_sites =
      [&](const LegalmPlaceCell& item, const int row, const int site) {
        return std::abs(site - item.original_site)
               + row_equiv_sites * std::abs(row - item.original_row);
      };

  auto hpwl_delta_reward_scale = [&](const int displacement) {
    const int reward_tail
        = std::max(0, displacement - max_disp_threshold_sites);
    return std::clamp(
        1.0
            - static_cast<double>(reward_tail)
                  / static_cast<double>(std::max(1,
                                                 max_disp_threshold_sites)),
        0.0,
        1.0);
  };

  auto hpwl_delta_reward_cost =
      [&](const LegalmPlaceCell& item, const int row, const int site) {
        const double delta = hpwl_delta_sites(item, row, site);
        if (delta >= 0.0) {
          return 0.0;
        }
        return kHpwlDeltaRewardWeight * delta
               * hpwl_delta_reward_scale(displacement_sites(item, row, site));
      };

  auto displacement_cost =
      [&](const LegalmPlaceCell& item, const int row, const int site) {
        const int original_delta = displacement_sites(item, row, site);
        const int tail = std::max(0, original_delta - max_disp_threshold_sites);
        const int extreme_tail
            = std::max(0, original_delta - 2 * max_disp_threshold_sites);
        return item.height_class_weight * static_cast<double>(original_delta)
               + kMaxDispTailWeight * static_cast<double>(tail)
               + kExtremeDispTailWeight * static_cast<double>(extreme_tail);
      };

  auto placement_cost = [&](const LegalmPlaceCell& item,
                            const int row,
                            const int site,
                            const bool include_signed_hpwl_reward) {
    const double hpwl_reward
        = include_signed_hpwl_reward ? hpwl_delta_reward_cost(item, row, site)
                                     : 0.0;
        return displacement_cost(item, row, site)
               + hpwl_reward
               + kHpwlRegressionPenaltyWeight
                     * hpwl_regression_penalty(item, row, site);
      };

  auto row_lower_bound = [&](const LegalmPlaceCell& item,
                             const int row,
                             const bool include_signed_hpwl_reward) {
    const int original_vertical
        = row_equiv_sites * std::abs(row - item.original_row);
    const int tail = std::max(0, original_vertical - max_disp_threshold_sites);
    const int extreme_tail
        = std::max(0, original_vertical - 2 * max_disp_threshold_sites);
    double lower_bound
        = item.height_class_weight * static_cast<double>(original_vertical)
          + kMaxDispTailWeight * static_cast<double>(tail)
          + kExtremeDispTailWeight * static_cast<double>(extreme_tail);
    if (include_signed_hpwl_reward) {
      lower_bound -= kHpwlDeltaRewardWeight * max_hpwl_delta_sites
                     * hpwl_delta_reward_scale(original_vertical);
    }
    return lower_bound;
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
  int64_t assignment_row_probes = 0;
  int64_t assignment_anchor_row_probes = 0;
  int64_t assignment_interval_candidate_evals = 0;
  int64_t assignment_interval_duplicate_rejects = 0;
  int64_t assignment_interval_cap_hits = 0;
  int assignment_max_interval_candidates = 0;
  int64_t assignment_hpwl_projection_terms = 0;
  int64_t assignment_hpwl_projection_sites = 0;
  int64_t assignment_hpwl_projection_shadow_sites = 0;
  int64_t assignment_hpwl_projection_accepts = 0;

  struct AssignmentCandidate
  {
    int row = -1;
    int site = -1;
    double cost = std::numeric_limits<double>::infinity();
    double safe_cost = std::numeric_limits<double>::infinity();
    double pressure_cost = std::numeric_limits<double>::infinity();
    double hpwl_delta = 0.0;
    int displacement = 0;
    int pressure_delta = 0;
    int kind = 0;
    bool hpwl_projection_anchor = false;
  };

  auto better_assignment_candidate = [](const AssignmentCandidate& candidate,
                                        const AssignmentCandidate& incumbent) {
    if (candidate.site < 0) {
      return false;
    }
    if (incumbent.site < 0) {
      return true;
    }
    constexpr double kCostEpsilon = 1.0e-9;
    if (candidate.cost < incumbent.cost - kCostEpsilon) {
      return true;
    }
    if (candidate.cost > incumbent.cost + kCostEpsilon) {
      return false;
    }
    if (candidate.displacement != incumbent.displacement) {
      return candidate.displacement < incumbent.displacement;
    }
    if (candidate.hpwl_delta != incumbent.hpwl_delta) {
      return candidate.hpwl_delta < incumbent.hpwl_delta;
    }
    if (candidate.row != incumbent.row) {
      return candidate.row < incumbent.row;
    }
    return candidate.site < incumbent.site;
  };

  auto find_site_in_row = [&](const LegalmPlaceCell& item,
                              const int row,
                              const bool fast_static_path,
                              const bool include_signed_hpwl_reward,
                              const std::array<int,
                                               kAssignmentHpwlProjectionSiteCap>&
                                  hpwl_projection_sites,
                              const int hpwl_projection_site_count) {
    AssignmentCandidate best;
    best.row = row;
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
    std::array<int, kMaxIntervalCandidateSites> tried_sites{};
    int tried_count = 0;
    auto update_best_from_candidate = [&](const int candidate,
                                          const bool hpwl_projection_anchor) {
      if (candidate < 0 || candidate + item.width_sites > site_count) {
        return;
      }
      for (int i = 0; i < tried_count; ++i) {
        if (tried_sites[i] == candidate) {
          ++assignment_interval_duplicate_rejects;
          return;
        }
      }
      if (tried_count >= static_cast<int>(tried_sites.size())) {
        ++assignment_interval_cap_hits;
        return;
      }
      tried_sites[tried_count++] = candidate;
      ++assignment_interval_candidate_evals;
      if (!candidate_ok(candidate)) {
        return;
      }
      AssignmentCandidate trial;
      trial.row = row;
      trial.site = candidate;
      trial.safe_cost = placement_cost(item, row, candidate, false);
      trial.cost = include_signed_hpwl_reward
                       ? placement_cost(item, row, candidate, true)
                       : trial.safe_cost;
      trial.hpwl_delta = hpwl_delta_sites(item, row, candidate);
      trial.displacement = displacement_sites(item, row, candidate);
      trial.hpwl_projection_anchor = hpwl_projection_anchor;
      if (better_assignment_candidate(trial, best)) {
        best = trial;
      }
    };
    auto update_best_from_interval = [&](const Interval& interval,
                                         const int target_site,
                                         const bool hpwl_projection_anchor) {
      if (interval.group != item.group
          || interval.second - interval.first < item.width_sites) {
        return;
      }
      const int candidate = std::clamp(
          target_site, interval.first, interval.second - item.width_sites);
      update_best_from_candidate(candidate, hpwl_projection_anchor);
      update_best_from_candidate(interval.first, hpwl_projection_anchor);
      update_best_from_candidate(interval.second - item.width_sites,
                                 hpwl_projection_anchor);
    };

    std::array<int, 4 + kAssignmentHpwlProjectionSiteCap> target_sites{};
    std::array<unsigned char, 4 + kAssignmentHpwlProjectionSiteCap>
        target_site_is_projection{};
    int target_site_count = 0;
    auto add_target_site = [&](const int site,
                               const bool hpwl_projection_anchor = false) {
      if (site < 0 || site + item.width_sites > site_count) {
        return;
      }
      for (int i = 0; i < target_site_count; ++i) {
        if (target_sites[i] == site) {
          if (hpwl_projection_anchor) {
            target_site_is_projection[i] = 1;
          }
          return;
        }
      }
      if (target_site_count < static_cast<int>(target_sites.size())) {
        target_sites[target_site_count++] = site;
        target_site_is_projection[target_site_count - 1]
            = hpwl_projection_anchor ? 1 : 0;
      }
    };
    add_target_site(item.desired_site);
    add_target_site(item.original_site);
    add_target_site((item.desired_site + item.original_site) / 2);
    add_target_site(
        std::clamp(
            item.desired_site, 0, std::max(0, site_count - item.width_sites)));
    if (include_signed_hpwl_reward) {
      for (int i = 0; i < hpwl_projection_site_count; ++i) {
        add_target_site(hpwl_projection_sites[i], true);
      }
    }

    for (int target_site_idx = 0; target_site_idx < target_site_count;
         ++target_site_idx) {
      const int target_site = target_sites[target_site_idx];
      const bool hpwl_projection_anchor
          = target_site_is_projection[target_site_idx] != 0;
      auto it
          = std::lower_bound(intervals.begin(),
                             intervals.end(),
                             target_site,
                             [](const Interval& interval, const int target) {
                               return interval.second <= target;
                             });

      int forward_samples = 0;
      for (auto fwd = it;
           fwd != intervals.end() && forward_samples < kIntervalNeighborProbeLimit;
           ++fwd, ++forward_samples) {
        update_best_from_interval(
            *fwd, target_site, hpwl_projection_anchor);
      }
      if (it == intervals.end() && !intervals.empty()) {
        update_best_from_interval(
            intervals.back(), target_site, hpwl_projection_anchor);
      }
      auto back = it;
      int backward_samples = 0;
      while (back != intervals.begin()
             && backward_samples < kIntervalNeighborProbeLimit) {
        --back;
        update_best_from_interval(
            *back, target_site, hpwl_projection_anchor);
        ++backward_samples;
      }
    }

    assignment_max_interval_candidates
        = std::max(assignment_max_interval_candidates, tried_count);
    if (best.site >= 0) {
      return best;
    }

    ++bounded_interval_fallbacks;
    tried_count = 0;
    for (const auto& interval : intervals) {
      update_best_from_interval(interval, item.desired_site, false);
      update_best_from_interval(interval, item.original_site, false);
      update_best_from_interval(interval,
                                (item.desired_site + item.original_site) / 2,
                                false);
    }
    assignment_max_interval_candidates
        = std::max(assignment_max_interval_candidates, tried_count);
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
  std::vector<int> assignment_displacement_sites(cells.size(), 0);
  int max_cell_node_id = 0;
  for (const LegalmPlaceCell& item : cells) {
    max_cell_node_id = std::max(max_cell_node_id, item.cell->getId());
  }
  std::vector<int> cell_index_by_node_id(max_cell_node_id + 1, -1);
  for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
    const int id = cells[idx].cell->getId();
    if (id >= 0 && id < static_cast<int>(cell_index_by_node_id.size())) {
      cell_index_by_node_id[id] = idx;
    }
  }
  std::vector<int> row_demand_remaining(row_count, 0);
  std::vector<int> row_capacity_remaining(row_count, 0);
  for (int row = 0; row < row_count; ++row) {
    for (const Interval& interval : free_intervals[row]) {
      row_capacity_remaining[row] += interval.second - interval.first;
    }
  }
  for (const LegalmPlaceCell& item : cells) {
    for (int dy = 0; dy < item.height_rows; ++dy) {
      const int row = item.desired_row + dy;
      if (row >= 0 && row < row_count) {
        row_demand_remaining[row] += item.width_sites;
      }
    }
  }
  auto pressure_overflow_sites = [](const int demand, const int capacity) {
    return std::max(0, demand - capacity);
  };
  auto pressure_delta_for_candidate = [&](const LegalmPlaceCell& item,
                                          const int candidate_row) {
    std::array<int, 32> touched_rows{};
    int touched_count = 0;
    auto remember_touched_row = [&](const int row) {
      if (row < 0 || row >= row_count) {
        return;
      }
      for (int i = 0; i < touched_count; ++i) {
        if (touched_rows[i] == row) {
          return;
        }
      }
      if (touched_count < static_cast<int>(touched_rows.size())) {
        touched_rows[touched_count++] = row;
      }
    };
    for (int dy = 0; dy < item.height_rows; ++dy) {
      remember_touched_row(item.desired_row + dy);
      remember_touched_row(candidate_row + dy);
    }
    auto footprint_contains = [](const int row,
                                 const int first_row,
                                 const int height_rows) {
      return row >= first_row && row < first_row + height_rows;
    };
    int before_pressure = 0;
    int after_pressure = 0;
    for (int i = 0; i < touched_count; ++i) {
      const int row = touched_rows[i];
      before_pressure += pressure_overflow_sites(row_demand_remaining[row],
                                                 row_capacity_remaining[row]);
      int demand_after = row_demand_remaining[row];
      int capacity_after = row_capacity_remaining[row];
      if (footprint_contains(row, item.desired_row, item.height_rows)) {
        demand_after -= item.width_sites;
      }
      if (footprint_contains(row, candidate_row, item.height_rows)) {
        capacity_after -= item.width_sites;
      }
      after_pressure += pressure_overflow_sites(demand_after, capacity_after);
    }
    return before_pressure - after_pressure;
  };
  auto pressure_cost_for_candidate = [&](AssignmentCandidate& candidate) {
    const int relief = std::max(0, candidate.pressure_delta);
    const int deficit = std::max(0, -candidate.pressure_delta);
    candidate.pressure_cost
        = candidate.safe_cost
          - kPressureReliefRewardWeight * static_cast<double>(relief)
          + kPressureDeficitPenaltyWeight * static_cast<double>(deficit);
  };
  auto update_pressure_model = [&](const LegalmPlaceCell& item,
                                   const int candidate_row) {
    for (int dy = 0; dy < item.height_rows; ++dy) {
      const int source_row = item.desired_row + dy;
      if (source_row >= 0 && source_row < row_count) {
        row_demand_remaining[source_row] = std::max(
            0, row_demand_remaining[source_row] - item.width_sites);
      }
      const int sink_row = candidate_row + dy;
      if (sink_row >= 0 && sink_row < row_count) {
        row_capacity_remaining[sink_row] = std::max(
            0, row_capacity_remaining[sink_row] - item.width_sites);
      }
    }
  };
  auto build_assignment_hpwl_projection_sites =
      [&](const LegalmPlaceCell& item,
          std::array<int, kAssignmentHpwlProjectionSiteCap>& sites,
          int& site_count_out) {
        site_count_out = 0;
        if (!assignment_hpwl_projection_enabled || item.hpwl_terms.empty()) {
          return;
        }

        struct ProjectionAnchor
        {
          int site = -1;
          double score = -std::numeric_limits<double>::infinity();
          int displacement = std::numeric_limits<int>::max();
          bool shadow = false;
        };

        std::array<ProjectionAnchor, kAssignmentHpwlProjectionSiteCap> anchors{};
        int anchor_count = 0;
        const int64_t desired_center_x
            = gridToDbu(GridX{item.desired_site}, site_width).v
              + static_cast<int64_t>(item.cell->getWidth().v) / 2;
        const int64_t grid_origin_x = gridToDbu(GridX{0}, site_width).v;
        auto site_from_center_x = [&](const int64_t center_x) {
          const int64_t left
              = center_x - static_cast<int64_t>(item.cell->getWidth().v) / 2;
          const int site = static_cast<int>(std::llround(
              static_cast<double>(left - grid_origin_x)
              / static_cast<double>(std::max(1, site_width.v))));
          return std::clamp(site, 0, std::max(0, site_count - item.width_sites));
        };
        auto insert_anchor = [&](const int site,
                                 const double gain_sites,
                                 const bool shadow_anchor) {
          if (site < 0 || site + item.width_sites > site_count
              || gain_sites < 0.50) {
            return;
          }
          const int displacement
              = displacement_sites(item, item.desired_row, site);
          const int tail
              = std::max(0, displacement - max_disp_threshold_sites);
          const int extreme_tail
              = std::max(0, displacement - 2 * max_disp_threshold_sites);
          const double score
              = gain_sites - 0.04 * static_cast<double>(tail)
                - 0.12 * static_cast<double>(extreme_tail);
          if (score <= 0.0) {
            return;
          }
          for (int i = 0; i < anchor_count; ++i) {
            if (anchors[i].site == site) {
              if (score > anchors[i].score) {
                anchors[i].score = score;
                anchors[i].displacement = displacement;
              }
              anchors[i].shadow = anchors[i].shadow || shadow_anchor;
              return;
            }
          }
          ProjectionAnchor candidate{site, score, displacement, shadow_anchor};
          int insert_at = anchor_count;
          for (int i = 0; i < anchor_count; ++i) {
            if (candidate.score > anchors[i].score
                || (candidate.score == anchors[i].score
                    && candidate.displacement < anchors[i].displacement)) {
              insert_at = i;
              break;
            }
          }
          if (insert_at >= kAssignmentHpwlProjectionSiteCap) {
            return;
          }
          const int limit
              = std::min(anchor_count, kAssignmentHpwlProjectionSiteCap - 1);
          for (int i = limit; i > insert_at; --i) {
            anchors[i] = anchors[i - 1];
          }
          anchors[insert_at] = candidate;
          anchor_count = std::min(anchor_count + 1,
                                  kAssignmentHpwlProjectionSiteCap);
        };

        for (const LegalmHpwlTerm& term : item.hpwl_terms) {
          int64_t low = term.other_min_x - term.offset_x;
          int64_t high = term.other_max_x - term.offset_x;
          if (low > high) {
            std::swap(low, high);
          }
          const int64_t projected_center_x
              = std::clamp(desired_center_x, low, high);
          const int64_t delta
              = projected_center_x > desired_center_x
                    ? projected_center_x - desired_center_x
                    : desired_center_x - projected_center_x;
          if (delta <= 0) {
            continue;
          }
          const double gain_sites
              = static_cast<double>(delta)
                / static_cast<double>(std::max(1, site_width.v));
          ++assignment_hpwl_projection_terms;
          insert_anchor(site_from_center_x(projected_center_x),
                        gain_sites,
                        false);
          insert_anchor(
              site_from_center_x((desired_center_x + 3 * projected_center_x)
                                 / 4),
              0.75 * gain_sites,
              true);
        }

        site_count_out = anchor_count;
        int shadow_count = 0;
        for (int i = 0; i < anchor_count; ++i) {
          sites[i] = anchors[i].site;
          if (anchors[i].shadow) {
            ++shadow_count;
          }
        }
        assignment_hpwl_projection_sites += site_count_out;
        assignment_hpwl_projection_shadow_sites += shadow_count;
      };
  enum AssignmentKind
  {
    kAssignmentNone = 0,
    kAssignmentTargetRow = 1,
    kAssignmentTargetNearby = 2,
    kAssignmentPressureEscape = 3,
    kAssignmentHpwlEscape = 4
  };
  int row_escape_budget_used = 0;
  int row_escape_budget_rejects = 0;
  int row_escape_budget_pressure_overflows = 0;
  int assignment_target_row_accepts = 0;
  int assignment_target_nearby_accepts = 0;
  int assignment_pressure_escape_accepts = 0;
  int assignment_hpwl_escape_accepts = 0;
  int64_t assignment_hpwl_escape_candidates = 0;
  int64_t assignment_pressure_escape_candidates = 0;
  int64_t assignment_low_residual_escape_rejects = 0;
  int64_t assignment_tail_guard_rejects = 0;
  int64_t assignment_hpwl_threshold_rejects = 0;
  int64_t assignment_pressure_relief_sites = 0;
  int64_t assignment_pressure_deficit_sites = 0;
  int64_t assignment_hpwl_over_pressure_accepts = 0;
  int64_t assignment_hpwl_over_pressure_rejects = 0;
  int64_t assignment_pressure_selected_over_hpwl = 0;

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
      assignment_displacement_sites[item_idx]
          = displacement_sites(item, item.desired_row, item.desired_site);
      ++assignment_target_row_accepts;
      update_pressure_model(item, item.desired_row);
    }
  } else {
    for (int item_idx = 0; item_idx < static_cast<int>(cells.size());
         ++item_idx) {
      const LegalmPlaceCell& item = cells[item_idx];
      AssignmentCandidate best_target;
      AssignmentCandidate best_candidate;
      const bool fast_static_path = arch_->getRegions().empty()
                                    && item.height_rows == 1
                                    && !item.cell->inGroup();
      const bool include_signed_hpwl_reward
          = !low_residual_policy || low_residual_guided_hpwl_assignment;
      std::array<int, kAssignmentHpwlProjectionSiteCap>
          assignment_hpwl_projection_sites{};
      int assignment_hpwl_projection_site_count = 0;
      build_assignment_hpwl_projection_sites(
          item,
          assignment_hpwl_projection_sites,
          assignment_hpwl_projection_site_count);
      std::array<int, 16> probed_rows{};
      int probed_row_count = 0;
      auto row_already_probed = [&](const int row) {
        for (int i = 0; i < probed_row_count; ++i) {
          if (probed_rows[i] == row) {
            return true;
          }
        }
        return false;
      };
      auto remember_row = [&](const int row) {
        if (row < 0 || row >= row_count || row_already_probed(row)
            || probed_row_count >= static_cast<int>(probed_rows.size())) {
          return;
        }
        probed_rows[probed_row_count++] = row;
      };
      auto probe_row = [&](const int row,
                           const bool anchor_probe,
                           const bool allow_prune_stop,
                           const AssignmentCandidate& incumbent,
                           AssignmentCandidate& candidate) {
        if (row < 0 || row >= row_count) {
          return false;
        }
        ++assignment_row_probes;
        if (anchor_probe) {
          ++assignment_anchor_row_probes;
        }
        const double lower_bound
            = row_lower_bound(item, row, include_signed_hpwl_reward);
        if (allow_prune_stop && incumbent.site >= 0
            && lower_bound > incumbent.cost) {
          return allow_prune_stop;
        }
        candidate = find_site_in_row(item,
                                     row,
                                     fast_static_path,
                                     include_signed_hpwl_reward,
                                     assignment_hpwl_projection_sites,
                                     assignment_hpwl_projection_site_count);
        return false;
      };

      if (!low_residual_policy) {
        AssignmentCandidate high_pressure_hpwl_best;
        AssignmentCandidate high_pressure_pressure_best;
        std::array<int, 16> pressure_probed_rows{};
        int pressure_probed_row_count = 0;
        auto pressure_row_already_probed = [&](const int row) {
          for (int i = 0; i < pressure_probed_row_count; ++i) {
            if (pressure_probed_rows[i] == row) {
              return true;
            }
          }
          return false;
        };
        auto consider_pressure_candidate = [&](AssignmentCandidate candidate) {
          if (candidate.site < 0) {
            return;
          }
          candidate.pressure_delta
              = pressure_delta_for_candidate(item, candidate.row);
          pressure_cost_for_candidate(candidate);
          candidate.cost = candidate.pressure_cost;
          if (candidate.row == item.desired_row) {
            candidate.kind = kAssignmentTargetRow;
          } else {
            candidate.kind = kAssignmentPressureEscape;
            ++assignment_pressure_escape_candidates;
          }
          if (better_assignment_candidate(candidate,
                                          high_pressure_pressure_best)) {
            high_pressure_pressure_best = candidate;
          }
        };
        auto try_pressure_row = [&](const int row, const bool anchor_probe) {
          if (row < 0 || row >= row_count
              || pressure_row_already_probed(row)
              || pressure_probed_row_count
                     >= static_cast<int>(pressure_probed_rows.size())) {
            return;
          }
          pressure_probed_rows[pressure_probed_row_count++] = row;
          ++assignment_row_probes;
          if (anchor_probe) {
            ++assignment_anchor_row_probes;
          }
          const std::array<int, kAssignmentHpwlProjectionSiteCap>
              empty_projection_sites{};
          consider_pressure_candidate(
              find_site_in_row(item,
                               row,
                               fast_static_path,
                               false,
                               empty_projection_sites,
                               0));
        };
        auto try_high_pressure_row = [&](const int row,
                                         const bool anchor_probe,
                                         const bool allow_prune_stop) {
          if (row < 0 || row >= row_count || row_already_probed(row)) {
            return false;
          }
          remember_row(row);
          AssignmentCandidate candidate;
          const bool prune_stop
              = probe_row(row,
                          anchor_probe,
                          allow_prune_stop,
                          high_pressure_hpwl_best,
                          candidate);
          if (prune_stop) {
            return true;
          }
          if (candidate.site < 0) {
            return false;
          }
          candidate.kind = row == item.desired_row ? kAssignmentTargetRow
                                                   : kAssignmentHpwlEscape;
          if (row != item.desired_row) {
            ++assignment_hpwl_escape_candidates;
          }
          if (better_assignment_candidate(candidate, high_pressure_hpwl_best)) {
            high_pressure_hpwl_best = candidate;
          }
          try_pressure_row(row, false);
          return false;
        };

        const std::array<int, 3> high_pressure_anchor_rows{
            item.desired_row,
            item.original_row,
            (item.desired_row + item.original_row) / 2};
        for (const int row : high_pressure_anchor_rows) {
          try_high_pressure_row(row, true, false);
        }
        for (int delta = 1; delta <= target_row_neighborhood + 2; ++delta) {
          try_pressure_row(item.desired_row + delta, false);
          try_pressure_row(item.desired_row - delta, false);
        }
        for (int delta = 1; delta < row_count; ++delta) {
          const int up = item.desired_row + delta;
          const int down = item.desired_row - delta;
          if (up >= row_count && down < 0) {
            break;
          }
          if (up < row_count && try_high_pressure_row(up, false, true)) {
            break;
          }
          if (down >= 0 && try_high_pressure_row(down, false, true)) {
            break;
          }
        }

        if (high_pressure_hpwl_best.row < 0
            || high_pressure_hpwl_best.site < 0) {
          ++failed_cells;
          placement_failures_.push_back(item.cell);
          continue;
        }

        AssignmentCandidate high_pressure_best = high_pressure_hpwl_best;
        high_pressure_hpwl_best.pressure_delta
            = pressure_delta_for_candidate(item, high_pressure_hpwl_best.row);
        if (high_pressure_pressure_best.site >= 0
            && (high_pressure_pressure_best.row
                    != high_pressure_hpwl_best.row
                || high_pressure_pressure_best.site
                       != high_pressure_hpwl_best.site)) {
          const double hpwl_improvement
              = high_pressure_pressure_best.hpwl_delta
                - high_pressure_hpwl_best.hpwl_delta;
          const int pressure_relief_advantage
              = high_pressure_pressure_best.pressure_delta
                - high_pressure_hpwl_best.pressure_delta;
          const int extra_displacement = std::max(
              0,
              high_pressure_hpwl_best.displacement
                  - high_pressure_pressure_best.displacement);
          const int hpwl_extreme_tail = std::max(
              0,
              high_pressure_hpwl_best.displacement
                  - 2 * max_disp_threshold_sites);
          const int pressure_extreme_tail = std::max(
              0,
              high_pressure_pressure_best.displacement
                  - 2 * max_disp_threshold_sites);
          const double required_hpwl_improvement
              = std::max(1.0,
                         kPressureHpwlSwitchBaseScale
                             * static_cast<double>(
                                 std::max(1, max_disp_threshold_sites)))
                + kPressureHpwlExtraDispScale
                      * static_cast<double>(extra_displacement)
                + kPressureHpwlExtremeTailScale
                      * static_cast<double>(
                          std::max(0, hpwl_extreme_tail - pressure_extreme_tail))
                + kPressureHpwlReliefScale
                      * static_cast<double>(
                          std::max(0, pressure_relief_advantage))
                + (row_escape_budget_used >= row_escape_budget
                       ? kPressureHpwlBudgetScale
                             * static_cast<double>(std::max(
                                 0,
                                 high_pressure_hpwl_best.displacement
                                     - high_pressure_pressure_best.displacement))
                       : 0.0);
          const int free_displacement_slack
              = std::max(2, max_disp_threshold_sites / 3);
          const bool hpwl_has_small_tail_cost
              = high_pressure_hpwl_best.displacement
                <= high_pressure_pressure_best.displacement
                       + free_displacement_slack;
          const bool pressure_has_economic_advantage
              = pressure_relief_advantage > 0 || !hpwl_has_small_tail_cost;
          if (pressure_has_economic_advantage
              && hpwl_improvement < required_hpwl_improvement) {
            high_pressure_best = high_pressure_pressure_best;
            ++assignment_hpwl_over_pressure_rejects;
            ++assignment_pressure_selected_over_hpwl;
          } else {
            ++assignment_hpwl_over_pressure_accepts;
          }
        }

        switch (high_pressure_best.kind) {
          case kAssignmentTargetRow:
            ++assignment_target_row_accepts;
            break;
          case kAssignmentPressureEscape:
            ++assignment_pressure_escape_accepts;
            break;
          case kAssignmentHpwlEscape:
            ++assignment_hpwl_escape_accepts;
            break;
          default:
            break;
        }
        const int best_row = high_pressure_best.row;
        const int best_site = high_pressure_best.site;
        const int pressure_delta
            = pressure_delta_for_candidate(item, best_row);
        placeCell(item.cell, GridX{best_site}, GridY{best_row});
        reserve_slot(best_row,
                     best_site,
                     item.width_sites,
                     item.height_rows,
                     item.group);
        current_rows[item_idx] = best_row;
        current_sites[item_idx] = best_site;
        assignment_displacement_sites[item_idx]
            = high_pressure_best.displacement;
        assignment_pressure_relief_sites += std::max(0, pressure_delta);
        assignment_pressure_deficit_sites += std::max(0, -pressure_delta);
        ++placed_cells;
        if (item.guided) {
          ++guided_placed;
        }
        if (best_row != item.desired_row) {
          ++row_escape_count;
          ++row_escape_budget_used;
          if (row_escape_budget_used > row_escape_budget) {
            ++row_escape_budget_pressure_overflows;
          }
        }
        const int abs_shift = std::abs(best_site - item.original_site);
        site_shift_sum += abs_shift;
        max_site_shift = std::max(max_site_shift, abs_shift);
        update_pressure_model(item, best_row);
        continue;
      }

      auto consider_target_row = [&](const int row) {
        if (row < 0 || row >= row_count || row_already_probed(row)) {
          return;
        }
        remember_row(row);
        AssignmentCandidate candidate;
        probe_row(row, false, false, best_target, candidate);
        if (candidate.site < 0) {
          return;
        }
        candidate.kind = row == item.desired_row ? kAssignmentTargetRow
                                                 : kAssignmentTargetNearby;
        if (better_assignment_candidate(candidate, best_target)) {
          best_target = candidate;
        }
      };

      consider_target_row(item.desired_row);
      const bool desired_row_candidate_available
          = best_target.site >= 0 && best_target.row == item.desired_row;
      const bool desired_row_tail_outlier
          = desired_row_candidate_available
            && best_target.displacement > 2 * max_disp_threshold_sites;
      if (!desired_row_candidate_available || desired_row_tail_outlier) {
        for (int delta = 1; delta <= target_row_neighborhood; ++delta) {
          consider_target_row(item.desired_row + delta);
          consider_target_row(item.desired_row - delta);
        }
      }
      best_candidate = best_target;
      const bool target_candidate_available = best_target.site >= 0;

      auto consider_escape_candidate = [&](AssignmentCandidate candidate) {
        if (candidate.site < 0) {
          return;
        }
        if (!target_candidate_available) {
          candidate.kind = kAssignmentPressureEscape;
          if (low_residual_policy) {
            candidate.cost = candidate.safe_cost;
          }
          ++assignment_pressure_escape_candidates;
          if (better_assignment_candidate(candidate, best_candidate)) {
            best_candidate = candidate;
          }
          return;
        }
        if (desired_row_tail_outlier && low_residual_policy) {
          candidate.kind = kAssignmentPressureEscape;
          candidate.cost = candidate.safe_cost;
          ++assignment_pressure_escape_candidates;
          if (candidate.displacement < best_candidate.displacement
              && better_assignment_candidate(candidate, best_candidate)) {
            best_candidate = candidate;
          }
          return;
        }

        ++assignment_hpwl_escape_candidates;
        if (!pressure_can_spend_hpwl_escape_budget) {
          ++assignment_low_residual_escape_rejects;
          return;
        }
        if (row_escape_budget_used >= row_escape_budget) {
          ++row_escape_budget_rejects;
          return;
        }
        const double hpwl_reward_sites
            = best_target.hpwl_delta - candidate.hpwl_delta;
        const int tail
            = std::max(0, candidate.displacement - max_disp_threshold_sites);
        const int extreme_tail = std::max(
            0, candidate.displacement - 2 * max_disp_threshold_sites);
        const double tail_required
            = kTailEscapeThresholdScale
              * static_cast<double>(tail + 2 * extreme_tail);
        if (hpwl_reward_sites < base_hpwl_escape_threshold) {
          ++assignment_hpwl_threshold_rejects;
          return;
        }
        if (hpwl_reward_sites < base_hpwl_escape_threshold + tail_required) {
          ++assignment_tail_guard_rejects;
          return;
        }
        candidate.kind = kAssignmentHpwlEscape;
        if (better_assignment_candidate(candidate, best_candidate)) {
          best_candidate = candidate;
        }
      };

      const bool search_escape_rows
          = desired_row_tail_outlier
            || (!desired_row_candidate_available
            && (!target_candidate_available
                || pressure_can_spend_hpwl_escape_budget));
      if (search_escape_rows) {
        const std::array<int, 2> anchor_row_candidates{
            item.original_row, (item.desired_row + item.original_row) / 2};
        for (const int anchor_row : anchor_row_candidates) {
          if (anchor_row < 0 || anchor_row >= row_count
              || row_already_probed(anchor_row)) {
            continue;
          }
          remember_row(anchor_row);
          AssignmentCandidate candidate;
          probe_row(anchor_row, true, false, best_candidate, candidate);
          consider_escape_candidate(candidate);
        }
      }

      if (search_escape_rows) {
        for (int delta = target_row_neighborhood + 1; delta < row_count;
             ++delta) {
          const int up = item.desired_row + delta;
          const int down = item.desired_row - delta;
          if (up >= row_count && down < 0) {
            break;
          }
          bool stop_scan = false;
          auto scan_escape_row = [&](const int row) {
            if (row < 0 || row >= row_count || row_already_probed(row)) {
              return false;
            }
            AssignmentCandidate candidate;
            const bool prune_stop
                = probe_row(row, false, true, best_candidate, candidate);
            if (prune_stop) {
              return true;
            }
            consider_escape_candidate(candidate);
            return false;
          };
          if (up < row_count) {
            stop_scan = scan_escape_row(up);
          }
          if (!stop_scan && down >= 0) {
            stop_scan = scan_escape_row(down);
          }
          if (stop_scan) {
            break;
          }
        }
      }

      if (best_candidate.row < 0 || best_candidate.site < 0) {
        ++failed_cells;
        placement_failures_.push_back(item.cell);
        continue;
      }

      switch (best_candidate.kind) {
        case kAssignmentTargetRow:
          ++assignment_target_row_accepts;
          break;
        case kAssignmentTargetNearby:
          ++assignment_target_nearby_accepts;
          break;
        case kAssignmentPressureEscape:
          ++assignment_pressure_escape_accepts;
          break;
        case kAssignmentHpwlEscape:
          ++assignment_hpwl_escape_accepts;
          break;
        default:
          break;
      }
      if (best_candidate.hpwl_projection_anchor) {
        ++assignment_hpwl_projection_accepts;
      }

      const int best_row = best_candidate.row;
      const int best_site = best_candidate.site;
      const int pressure_delta = pressure_delta_for_candidate(item, best_row);
      placeCell(item.cell, GridX{best_site}, GridY{best_row});
      reserve_slot(
          best_row, best_site, item.width_sites, item.height_rows, item.group);
      current_rows[item_idx] = best_row;
      current_sites[item_idx] = best_site;
      assignment_displacement_sites[item_idx] = best_candidate.displacement;
      assignment_pressure_relief_sites += std::max(0, pressure_delta);
      assignment_pressure_deficit_sites += std::max(0, -pressure_delta);
      ++placed_cells;
      if (item.guided) {
        ++guided_placed;
      }
      if (best_row != item.desired_row) {
        ++row_escape_count;
        ++row_escape_budget_used;
        if (row_escape_budget_used > row_escape_budget
            && best_candidate.kind != kAssignmentHpwlEscape) {
          ++row_escape_budget_pressure_overflows;
        }
      }
      const int abs_shift = std::abs(best_site - item.original_site);
      site_shift_sum += abs_shift;
      max_site_shift = std::max(max_site_shift, abs_shift);
      update_pressure_model(item, best_row);
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
  int low_residual_refinement_enabled = 0;
  int low_residual_refinement_frontier = 0;
  int low_residual_refinement_attempted = 0;
  int low_residual_refinement_moved = 0;
  int low_residual_refinement_row_moves = 0;
  int64_t low_residual_refinement_candidate_evals = 0;
  int64_t low_residual_refinement_swap_evals = 0;
  int low_residual_refinement_swap_moves = 0;
  int low_residual_refinement_free_moves = 0;
  int low_residual_refinement_frontier_before_cap = 0;
  int low_residual_refinement_frontier_pruned_no_bbox_terms = 0;
  int low_residual_refinement_frontier_pruned_low_density = 0;
  int64_t low_residual_refinement_legal_rejects = 0;
  int64_t low_residual_refinement_static_rejects = 0;
  int64_t low_residual_refinement_tail_rejects = 0;
  int64_t low_residual_refinement_exact_calls_avoided = 0;
  int64_t low_residual_refinement_fast_delta_calls = 0;
  int64_t low_residual_refinement_full_net_scans = 0;
  int64_t low_residual_refinement_no_bbox_candidate_skips = 0;
  double low_residual_refinement_hpwl_gain_sites = 0.0;
  int64_t low_residual_refinement_disp_gain_sites = 0;
  int64_t low_residual_current_net_anchor_terms = 0;
  int64_t low_residual_current_net_anchor_raw_terms = 0;
  int64_t low_residual_current_net_anchor_prefilter_rejects = 0;
  int64_t low_residual_current_net_anchor_target_rejects = 0;
  int64_t low_residual_current_net_anchor_safe_alternates = 0;
  int64_t low_residual_current_net_anchor_candidates = 0;
  int64_t low_residual_current_net_anchor_scored = 0;
  int64_t low_residual_current_net_anchor_moves = 0;
  double low_residual_current_net_anchor_hpwl_gain_sites = 0.0;
  int64_t low_residual_current_net_anchor_disp_gain_sites = 0;
  int64_t low_residual_target_correction_frontier = 0;
  int64_t low_residual_target_correction_candidates = 0;
  int64_t low_residual_target_correction_nearby_probes = 0;
  int64_t low_residual_target_correction_self_overlap_probes = 0;
  int64_t low_residual_target_correction_prefilter_rejects = 0;
  int64_t low_residual_target_correction_potential_evals = 0;
  int64_t low_residual_target_correction_potential_skips = 0;
  int64_t low_residual_target_correction_potential_rejects = 0;
  int64_t low_residual_target_correction_exact_cap_rejects = 0;
  int64_t low_residual_target_correction_scored = 0;
  int64_t low_residual_target_correction_moves = 0;
  int64_t low_residual_target_correction_free_moves = 0;
  int64_t low_residual_target_correction_swap_moves = 0;
  int64_t low_residual_target_correction_legal_rejects = 0;
  int64_t low_residual_target_correction_static_rejects = 0;
  int64_t low_residual_target_correction_disp_rejects = 0;
  int64_t low_residual_target_correction_hpwl_rejects = 0;
  double low_residual_target_correction_hpwl_gain_sites = 0.0;
  int64_t low_residual_target_correction_disp_gain_sites = 0;
  int64_t low_residual_target_correction_target_gain_sites = 0;
  int64_t low_residual_target_release_frontier = 0;
  int64_t low_residual_target_release_blockers = 0;
  int64_t low_residual_target_release_old_slot_probes = 0;
  int64_t low_residual_target_release_neighbor_probes = 0;
  int64_t low_residual_target_release_candidates = 0;
  int64_t low_residual_target_release_scored = 0;
  int64_t low_residual_target_release_moves = 0;
  int64_t low_residual_target_release_multi_blocker_rejects = 0;
  int64_t low_residual_target_release_exact_cap_rejects = 0;
  int64_t low_residual_target_release_legal_rejects = 0;
  int64_t low_residual_target_release_static_rejects = 0;
  int64_t low_residual_target_release_disp_rejects = 0;
  int64_t low_residual_target_release_hpwl_rejects = 0;
  int64_t low_residual_target_release_exact_calls_avoided = 0;
  int64_t low_residual_target_release_full_net_scans = 0;
  double low_residual_target_release_hpwl_gain_sites = 0.0;
  int64_t low_residual_target_release_disp_gain_sites = 0;
  int64_t low_residual_target_release_target_gain_sites = 0;
  int64_t low_residual_chain_candidates = 0;
  int64_t low_residual_chain_moves = 0;
  int64_t low_residual_chain_moved_cells = 0;
  int64_t low_residual_chain_legal_rejects = 0;
  int64_t low_residual_chain_static_rejects = 0;
  int64_t low_residual_chain_tail_rejects = 0;
  int64_t low_residual_chain_no_bbox_skips = 0;
  int64_t low_residual_chain_fast_delta_calls = 0;
  int64_t low_residual_chain_exact_calls_avoided = 0;
  int64_t low_residual_chain_full_net_scans = 0;
  int64_t low_residual_frontier_regions_skipped = 0;
  double low_residual_chain_hpwl_gain_sites = 0.0;
  int64_t low_residual_chain_disp_gain_sites = 0;
  int high_pressure_tail_refinement_enabled = 0;
  int high_pressure_tail_frontier_before_cap = 0;
  int high_pressure_tail_frontier = 0;
  int high_pressure_tail_attempted = 0;
  int high_pressure_tail_moved = 0;
  int high_pressure_tail_moved_cells = 0;
  int64_t high_pressure_tail_interval_candidates = 0;
  int64_t high_pressure_tail_swap_candidates = 0;
  int64_t high_pressure_tail_chain_candidates = 0;
  int64_t high_pressure_tail_gap_chain_candidates = 0;
  int high_pressure_tail_interval_moves = 0;
  int high_pressure_tail_swap_moves = 0;
  int high_pressure_tail_chain_moves = 0;
  int high_pressure_tail_gap_chain_moves = 0;
  int64_t high_pressure_tail_legal_rejects = 0;
  int64_t high_pressure_tail_static_rejects = 0;
  int64_t high_pressure_tail_hpwl_rejects = 0;
  int64_t high_pressure_tail_disp_rejects = 0;
  int64_t high_pressure_tail_tail_rejects = 0;
  int64_t high_pressure_tail_chain_legal_rejects = 0;
  int64_t high_pressure_tail_chain_static_rejects = 0;
  int64_t high_pressure_tail_chain_hpwl_rejects = 0;
  int64_t high_pressure_tail_chain_disp_rejects = 0;
  int64_t high_pressure_tail_chain_tail_rejects = 0;
  int64_t high_pressure_tail_gap_chain_duplicate_skips = 0;
  int64_t high_pressure_tail_gap_chain_prefilter_legal_skips = 0;
  int64_t high_pressure_tail_gap_chain_prefilter_static_skips = 0;
  int64_t high_pressure_tail_gap_chain_prefilter_disp_skips = 0;
  int64_t high_pressure_tail_gap_chain_prefilter_tail_skips = 0;
  int64_t high_pressure_tail_relief_row_anchors = 0;
  int64_t high_pressure_tail_relief_row_scored = 0;
  int64_t high_pressure_tail_relief_row_moves = 0;
  double high_pressure_tail_relief_row_hpwl_gain_sites = 0.0;
  int64_t high_pressure_tail_relief_row_disp_gain_sites = 0;
  std::array<int64_t, 3> high_pressure_tail_accept_old_tail_bins{0, 0, 0};
  std::array<int64_t, 3> high_pressure_tail_accept_new_tail_bins{0, 0, 0};
  int high_pressure_endpoint_reservoir_size = 0;
  int high_pressure_endpoint_owner_count = 0;
  int high_pressure_endpoint_before_max_disp = 0;
  int high_pressure_endpoint_after_max_disp = 0;
  int high_pressure_endpoint_before_p99_disp = 0;
  int high_pressure_endpoint_after_p99_disp = 0;
  int64_t high_pressure_endpoint_scored = 0;
  int64_t high_pressure_endpoint_moves = 0;
  int64_t high_pressure_endpoint_legal_rejects = 0;
  int64_t high_pressure_endpoint_static_rejects = 0;
  int64_t high_pressure_endpoint_hpwl_rejects = 0;
  int64_t high_pressure_endpoint_disp_rejects = 0;
  int64_t high_pressure_endpoint_tail_rejects = 0;
  int64_t high_pressure_endpoint_delta_calls = 0;
  int64_t high_pressure_endpoint_full_net_scans = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_sites = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_terms = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_scored = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_tail_rejects = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_moves = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_rows = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_terms = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_free_gap_score = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_scored = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_tail_rejects = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_moves = 0;
  int64_t high_pressure_endpoint_hpwl_credit_candidates = 0;
  int64_t high_pressure_endpoint_hpwl_credit_moves = 0;
  int64_t high_pressure_endpoint_hpwl_credit_rejects = 0;
  double high_pressure_endpoint_hpwl_gain_sites = 0.0;
  double high_pressure_endpoint_hpwl_loss_sites = 0.0;
  double high_pressure_endpoint_hpwl_anchor_gain_sites = 0.0;
  double high_pressure_endpoint_hpwl_anchor_loss_sites = 0.0;
  double high_pressure_endpoint_hpwl_row_anchor_gain_sites = 0.0;
  double high_pressure_endpoint_hpwl_row_anchor_loss_sites = 0.0;
  double high_pressure_endpoint_hpwl_credit_gain_sites = 0.0;
  int64_t high_pressure_endpoint_disp_gain_sites = 0;
  int64_t high_pressure_endpoint_hpwl_anchor_disp_gain_sites = 0;
  int64_t high_pressure_endpoint_hpwl_row_anchor_disp_gain_sites = 0;
  int64_t high_pressure_endpoint_hpwl_credit_disp_gain_sites = 0;
  std::array<int64_t, 3> high_pressure_endpoint_before_tail_bins{0, 0, 0};
  std::array<int64_t, 3> high_pressure_endpoint_after_tail_bins{0, 0, 0};
  int high_pressure_topmax_actual_owner_count = 0;
  int high_pressure_topmax_owner_count = 0;
  int high_pressure_topmax_before_max_disp = 0;
  int high_pressure_topmax_after_max_disp = 0;
  int high_pressure_topmax_before_p99_disp = 0;
  int high_pressure_topmax_after_p99_disp = 0;
  int64_t high_pressure_topmax_candidates = 0;
  int64_t high_pressure_topmax_two_cell_candidates = 0;
  int64_t high_pressure_topmax_moves = 0;
  int64_t high_pressure_topmax_two_cell_moves = 0;
  int64_t high_pressure_topmax_actual_candidates = 0;
  int64_t high_pressure_topmax_actual_two_cell_candidates = 0;
  int64_t high_pressure_topmax_actual_moves = 0;
  int64_t high_pressure_topmax_legal_rejects = 0;
  int64_t high_pressure_topmax_static_rejects = 0;
  int64_t high_pressure_topmax_hpwl_rejects = 0;
  int64_t high_pressure_topmax_disp_rejects = 0;
  int64_t high_pressure_topmax_tail_rejects = 0;
  int64_t high_pressure_topmax_actual_legal_rejects = 0;
  int64_t high_pressure_topmax_actual_static_rejects = 0;
  int64_t high_pressure_topmax_actual_hpwl_rejects = 0;
  int64_t high_pressure_topmax_actual_disp_rejects = 0;
  int64_t high_pressure_topmax_actual_tail_rejects = 0;
  int64_t high_pressure_topmax_delta_calls = 0;
  int64_t high_pressure_topmax_full_net_scans = 0;
  double high_pressure_topmax_hpwl_gain_sites = 0.0;
  double high_pressure_topmax_hpwl_loss_sites = 0.0;
  int64_t high_pressure_topmax_disp_gain_sites = 0;
  std::array<int64_t, 3> high_pressure_topmax_before_tail_bins{0, 0, 0};
  std::array<int64_t, 3> high_pressure_topmax_after_tail_bins{0, 0, 0};
  int64_t high_pressure_tail_no_bbox_accepts = 0;
  int64_t high_pressure_tail_fast_delta_calls = 0;
  int64_t high_pressure_tail_exact_calls_avoided = 0;
  int64_t high_pressure_tail_full_net_scans = 0;
  double high_pressure_tail_hpwl_gain_sites = 0.0;
  double high_pressure_tail_hpwl_loss_sites = 0.0;
  int64_t high_pressure_tail_disp_gain_sites = 0;
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

  auto current_cell_center = [&](const int idx,
                                 const int override_row,
                                 const int override_site) {
    const LegalmPlaceCell& item = cells[idx];
    const int row = override_row >= 0 ? override_row : current_rows[idx];
    const int site = override_site >= 0 ? override_site : current_sites[idx];
    if (row >= 0 && site >= 0) {
      const int64_t left = gridToDbu(GridX{site}, site_width).v;
      const int64_t bottom = grid_->gridYToDbu(GridY{row}).v;
      return std::pair<int64_t, int64_t>{
          left + static_cast<int64_t>(item.cell->getWidth().v) / 2,
          bottom + static_cast<int64_t>(item.cell->getHeight().v) / 2};
    }
    return std::pair<int64_t, int64_t>{item.cell->getCenterX().v,
                                       item.cell->getCenterY().v};
  };

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
      // Stage 3 is the zero-overflow tail repair/refinement pass.  Keep the
      // signed HPWL reward in the assignment handoff, but do not let it block
      // legal displacement-tail recovery when a row/segment choice was too
      // aggressive.
      return displacement_cost(item, row, site)
             + kHpwlRegressionPenaltyWeight
                   * hpwl_regression_penalty(item, row, site);
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

    if (low_residual_policy && row_escape_count > 0) {
      low_residual_refinement_enabled = 1;
      const int local_vertical_radius
          = std::max(1, std::min(2, cpu_caps.candidate_vertical_radius));
      const int local_horizontal_steps
          = std::max(4, std::min(12, cpu_caps.candidate_horizontal_steps));
      const int displacement_slack
          = std::max(1, max_disp_threshold_sites / 8);

      struct LowResidualNetSummary
      {
        const Edge* edge = nullptr;
        bool valid = false;
        int64_t min_x = 0;
        int64_t max_x = 0;
        int64_t min_y = 0;
        int64_t max_y = 0;
        int64_t second_min_x = 0;
        int64_t second_max_x = 0;
        int64_t second_min_y = 0;
        int64_t second_max_y = 0;
        int min_x_count = 0;
        int max_x_count = 0;
        int min_y_count = 0;
        int max_y_count = 0;
        int64_t hpwl_dbu = 0;
      };
      struct LowResidualIncidentNet
      {
        int net_idx = -1;
        std::vector<std::pair<int, int>> pin_offsets;
      };
      struct LowResidualDelta
      {
        double hpwl_delta_sites = 0.0;
        bool bbox_active = false;
      };

      std::vector<LowResidualNetSummary> low_residual_net_summaries;
      std::vector<std::vector<LowResidualIncidentNet>> low_residual_cell_nets(
          cells.size());
      std::unordered_map<const Edge*, int> low_residual_net_index;
      low_residual_net_index.reserve(static_cast<size_t>(
          std::max<int64_t>(1, total_hpwl_proxy_terms)));

      auto low_residual_cell_idx = [&](const Node* node) {
        if (node == nullptr) {
          return -1;
        }
        const int id = node->getId();
        if (id >= 0 && id < static_cast<int>(cell_index_by_node_id.size())) {
          return cell_index_by_node_id[id];
        }
        return -1;
      };
      auto low_residual_pin_xy = [&](const Pin* pin,
                                     const int override_idx_a,
                                     const int override_row_a,
                                     const int override_site_a,
                                     const int override_idx_b,
                                     const int override_row_b,
                                     const int override_site_b) {
        const Node* node = pin != nullptr ? pin->getNode() : nullptr;
        const int idx = low_residual_cell_idx(node);
        std::pair<int64_t, int64_t> center{0, 0};
        if (idx >= 0) {
          if (idx == override_idx_a) {
            center = current_cell_center(idx, override_row_a, override_site_a);
          } else if (idx == override_idx_b) {
            center = current_cell_center(idx, override_row_b, override_site_b);
          } else if (current_rows[idx] >= 0 && current_sites[idx] >= 0) {
            center = current_cell_center(idx, -1, -1);
          } else {
            center = {node->getCenterX().v, node->getCenterY().v};
          }
        } else if (node != nullptr) {
          center = {node->getCenterX().v, node->getCenterY().v};
        }
        return std::pair<int64_t, int64_t>{
            center.first + (pin != nullptr ? pin->getOffsetX().v : 0),
            center.second + (pin != nullptr ? pin->getOffsetY().v : 0)};
      };

      auto low_residual_net_for_edge = [&](const Edge* edge) {
        const auto found = low_residual_net_index.find(edge);
        if (found != low_residual_net_index.end()) {
          return found->second;
        }
        const int net_idx = static_cast<int>(low_residual_net_summaries.size());
        low_residual_net_index.emplace(edge, net_idx);
        LowResidualNetSummary summary;
        summary.edge = edge;
        low_residual_net_summaries.push_back(summary);
        return net_idx;
      };

      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        std::vector<LowResidualIncidentNet>& incidents
            = low_residual_cell_nets[idx];
        for (const Pin* cell_pin : cells[idx].cell->getPins()) {
          if (cell_pin == nullptr || cell_pin->getEdge() == nullptr) {
            continue;
          }
          const Edge* edge = cell_pin->getEdge();
          if (edge->getNumPins() <= 1
              || edge->getNumPins() >= kLowResidualRefinementNetPinLimit) {
            continue;
          }
          const int net_idx = low_residual_net_for_edge(edge);
          auto incident = std::find_if(
              incidents.begin(),
              incidents.end(),
              [&](const LowResidualIncidentNet& item) {
                return item.net_idx == net_idx;
              });
          if (incident == incidents.end()) {
            incidents.push_back({net_idx, {}});
            incident = std::prev(incidents.end());
          }
          incident->pin_offsets.push_back(
              {cell_pin->getOffsetX().v, cell_pin->getOffsetY().v});
        }
      }

      auto low_residual_recompute_net = [&](const int net_idx) {
        if (net_idx < 0
            || net_idx >= static_cast<int>(low_residual_net_summaries.size())) {
          return;
        }
        LowResidualNetSummary& summary = low_residual_net_summaries[net_idx];
        summary.valid = false;
        summary.min_x = std::numeric_limits<int64_t>::max();
        summary.max_x = std::numeric_limits<int64_t>::min();
        summary.min_y = std::numeric_limits<int64_t>::max();
        summary.max_y = std::numeric_limits<int64_t>::min();
        summary.second_min_x = std::numeric_limits<int64_t>::max();
        summary.second_max_x = std::numeric_limits<int64_t>::min();
        summary.second_min_y = std::numeric_limits<int64_t>::max();
        summary.second_max_y = std::numeric_limits<int64_t>::min();
        summary.min_x_count = 0;
        summary.max_x_count = 0;
        summary.min_y_count = 0;
        summary.max_y_count = 0;
        summary.hpwl_dbu = 0;
        if (summary.edge == nullptr) {
          return;
        }
        int pin_count = 0;
        for (const Pin* pin : summary.edge->getPins()) {
          if (pin == nullptr || pin->getNode() == nullptr) {
            continue;
          }
          const auto [pin_x, pin_y]
              = low_residual_pin_xy(pin, -1, -1, -1, -1, -1, -1);
          if (pin_count == 0 || pin_x < summary.min_x) {
            summary.second_min_x = summary.min_x;
            summary.min_x = pin_x;
            summary.min_x_count = 1;
          } else if (pin_x == summary.min_x) {
            ++summary.min_x_count;
          } else if (pin_x < summary.second_min_x) {
            summary.second_min_x = pin_x;
          }
          if (pin_count == 0 || pin_x > summary.max_x) {
            summary.second_max_x = summary.max_x;
            summary.max_x = pin_x;
            summary.max_x_count = 1;
          } else if (pin_x == summary.max_x) {
            ++summary.max_x_count;
          } else if (pin_x > summary.second_max_x) {
            summary.second_max_x = pin_x;
          }
          if (pin_count == 0 || pin_y < summary.min_y) {
            summary.second_min_y = summary.min_y;
            summary.min_y = pin_y;
            summary.min_y_count = 1;
          } else if (pin_y == summary.min_y) {
            ++summary.min_y_count;
          } else if (pin_y < summary.second_min_y) {
            summary.second_min_y = pin_y;
          }
          if (pin_count == 0 || pin_y > summary.max_y) {
            summary.second_max_y = summary.max_y;
            summary.max_y = pin_y;
            summary.max_y_count = 1;
          } else if (pin_y == summary.max_y) {
            ++summary.max_y_count;
          } else if (pin_y > summary.second_max_y) {
            summary.second_max_y = pin_y;
          }
          ++pin_count;
        }
        if (pin_count > 1) {
          if (summary.second_min_x == std::numeric_limits<int64_t>::max()) {
            summary.second_min_x = summary.min_x;
          }
          if (summary.second_max_x == std::numeric_limits<int64_t>::min()) {
            summary.second_max_x = summary.max_x;
          }
          if (summary.second_min_y == std::numeric_limits<int64_t>::max()) {
            summary.second_min_y = summary.min_y;
          }
          if (summary.second_max_y == std::numeric_limits<int64_t>::min()) {
            summary.second_max_y = summary.max_y;
          }
          summary.valid = true;
          summary.hpwl_dbu = (summary.max_x - summary.min_x)
                             + (summary.max_y - summary.min_y);
        }
      };
      for (int net_idx = 0;
           net_idx < static_cast<int>(low_residual_net_summaries.size());
           ++net_idx) {
        low_residual_recompute_net(net_idx);
      }

      auto low_residual_full_net_hpwl = [&](const int net_idx,
                                            const int override_idx_a,
                                            const int override_row_a,
                                            const int override_site_a,
                                            const int override_idx_b,
                                            const int override_row_b,
                                            const int override_site_b) {
        const LowResidualNetSummary& summary
            = low_residual_net_summaries[net_idx];
        int64_t min_x = std::numeric_limits<int64_t>::max();
        int64_t max_x = std::numeric_limits<int64_t>::min();
        int64_t min_y = std::numeric_limits<int64_t>::max();
        int64_t max_y = std::numeric_limits<int64_t>::min();
        int pin_count = 0;
        for (const Pin* pin : summary.edge->getPins()) {
          if (pin == nullptr || pin->getNode() == nullptr) {
            continue;
          }
          const auto [pin_x, pin_y]
              = low_residual_pin_xy(pin,
                                    override_idx_a,
                                    override_row_a,
                                    override_site_a,
                                    override_idx_b,
                                    override_row_b,
                                    override_site_b);
          min_x = std::min(min_x, pin_x);
          max_x = std::max(max_x, pin_x);
          min_y = std::min(min_y, pin_y);
          max_y = std::max(max_y, pin_y);
          ++pin_count;
        }
        if (pin_count <= 1) {
          return int64_t{0};
        }
        return (max_x - min_x) + (max_y - min_y);
      };

      auto low_residual_cell_has_bbox_term = [&](const int idx) {
        if (idx < 0 || idx >= static_cast<int>(low_residual_cell_nets.size())) {
          return false;
        }
        const auto [center_x, center_y] = current_cell_center(idx, -1, -1);
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[idx]) {
          const LowResidualNetSummary& summary
              = low_residual_net_summaries[incident.net_idx];
          if (!summary.valid) {
            continue;
          }
          for (const auto& offset : incident.pin_offsets) {
            const int64_t pin_x = center_x + offset.first;
            const int64_t pin_y = center_y + offset.second;
            if (pin_x == summary.min_x || pin_x == summary.max_x
                || pin_y == summary.min_y || pin_y == summary.max_y) {
              return true;
            }
          }
        }
        return false;
      };

      auto low_residual_move_delta = [&](const int idx,
                                         const int row,
                                         const int site,
                                         const bool count_runtime) {
        LowResidualDelta result;
        if (count_runtime) {
          ++low_residual_refinement_fast_delta_calls;
        }
        if (idx < 0 || idx >= static_cast<int>(low_residual_cell_nets.size())) {
          return result;
        }
        const auto [old_center_x, old_center_y]
            = current_cell_center(idx, -1, -1);
        const auto [new_center_x, new_center_y]
            = current_cell_center(idx, row, site);
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[idx]) {
          const LowResidualNetSummary& summary
              = low_residual_net_summaries[incident.net_idx];
          if (!summary.valid) {
            continue;
          }
          int moving_min_x_count = 0;
          int moving_max_x_count = 0;
          int moving_min_y_count = 0;
          int moving_max_y_count = 0;
          bool insertion_changes_bbox = false;
          for (const auto& offset : incident.pin_offsets) {
            const int64_t old_pin_x = old_center_x + offset.first;
            const int64_t old_pin_y = old_center_y + offset.second;
            const int64_t new_pin_x = new_center_x + offset.first;
            const int64_t new_pin_y = new_center_y + offset.second;
            moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
            moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
            moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
            moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
            insertion_changes_bbox
                = insertion_changes_bbox || new_pin_x < summary.min_x
                  || new_pin_x > summary.max_x || new_pin_y < summary.min_y
                  || new_pin_y > summary.max_y;
          }
          const bool removal_changes_bbox
              = (moving_min_x_count > 0
                 && moving_min_x_count >= summary.min_x_count)
                || (moving_max_x_count > 0
                    && moving_max_x_count >= summary.max_x_count)
                || (moving_min_y_count > 0
                    && moving_min_y_count >= summary.min_y_count)
                || (moving_max_y_count > 0
                    && moving_max_y_count >= summary.max_y_count);
          if (!removal_changes_bbox && !insertion_changes_bbox) {
            continue;
          }
          result.bbox_active = true;
          if (count_runtime) {
            ++low_residual_refinement_full_net_scans;
          }
          const int64_t new_hpwl = low_residual_full_net_hpwl(
              incident.net_idx, idx, row, site, -1, -1, -1);
          result.hpwl_delta_sites += static_cast<double>(new_hpwl
                                                         - summary.hpwl_dbu)
                                      / static_cast<double>(
                                          std::max(1, site_width.v));
        }
        if (count_runtime && !result.bbox_active) {
          ++low_residual_refinement_exact_calls_avoided;
        }
        return result;
      };

      auto low_residual_swap_delta = [&](const int idx,
                                         const int row,
                                         const int site,
                                         const int other_idx,
                                         const int other_row,
                                         const int other_site,
                                         const bool count_runtime) {
        LowResidualDelta result;
        if (count_runtime) {
          ++low_residual_refinement_fast_delta_calls;
        }
        std::vector<int> touched_nets;
        touched_nets.reserve(low_residual_cell_nets[idx].size()
                             + low_residual_cell_nets[other_idx].size());
        auto add_touched_net = [&](const int net_idx) {
          if (std::find(touched_nets.begin(), touched_nets.end(), net_idx)
              == touched_nets.end()) {
            touched_nets.push_back(net_idx);
          }
        };
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[idx]) {
          add_touched_net(incident.net_idx);
        }
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[other_idx]) {
          add_touched_net(incident.net_idx);
        }
        const auto [old_center_x, old_center_y]
            = current_cell_center(idx, -1, -1);
        const auto [new_center_x, new_center_y]
            = current_cell_center(idx, row, site);
        const auto [old_other_center_x, old_other_center_y]
            = current_cell_center(other_idx, -1, -1);
        const auto [new_other_center_x, new_other_center_y]
            = current_cell_center(other_idx, other_row, other_site);

        for (const int net_idx : touched_nets) {
          const LowResidualNetSummary& summary
              = low_residual_net_summaries[net_idx];
          if (!summary.valid) {
            continue;
          }
          int moving_min_x_count = 0;
          int moving_max_x_count = 0;
          int moving_min_y_count = 0;
          int moving_max_y_count = 0;
          bool insertion_changes_bbox = false;
          auto visit_moving_pins = [&](const int cell_idx,
                                       const int64_t from_center_x,
                                       const int64_t from_center_y,
                                       const int64_t to_center_x,
                                       const int64_t to_center_y) {
            if (cell_idx < 0
                || cell_idx
                       >= static_cast<int>(low_residual_cell_nets.size())) {
              return;
            }
            for (const LowResidualIncidentNet& incident :
                 low_residual_cell_nets[cell_idx]) {
              if (incident.net_idx != net_idx) {
                continue;
              }
              for (const auto& offset : incident.pin_offsets) {
                const int64_t old_pin_x = from_center_x + offset.first;
                const int64_t old_pin_y = from_center_y + offset.second;
                const int64_t new_pin_x = to_center_x + offset.first;
                const int64_t new_pin_y = to_center_y + offset.second;
                moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
                moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
                moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
                moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
                insertion_changes_bbox
                    = insertion_changes_bbox || new_pin_x < summary.min_x
                      || new_pin_x > summary.max_x || new_pin_y < summary.min_y
                      || new_pin_y > summary.max_y;
              }
            }
          };
          visit_moving_pins(
              idx, old_center_x, old_center_y, new_center_x, new_center_y);
          visit_moving_pins(other_idx,
                            old_other_center_x,
                            old_other_center_y,
                            new_other_center_x,
                            new_other_center_y);
          const bool removal_changes_bbox
              = (moving_min_x_count > 0
                 && moving_min_x_count >= summary.min_x_count)
                || (moving_max_x_count > 0
                    && moving_max_x_count >= summary.max_x_count)
                || (moving_min_y_count > 0
                    && moving_min_y_count >= summary.min_y_count)
                || (moving_max_y_count > 0
                    && moving_max_y_count >= summary.max_y_count);
          if (!removal_changes_bbox && !insertion_changes_bbox) {
            continue;
          }
          result.bbox_active = true;
          if (count_runtime) {
            ++low_residual_refinement_full_net_scans;
          }
          const int64_t new_hpwl = low_residual_full_net_hpwl(
              net_idx, idx, row, site, other_idx, other_row, other_site);
          result.hpwl_delta_sites += static_cast<double>(new_hpwl
                                                         - summary.hpwl_dbu)
                                      / static_cast<double>(
                                          std::max(1, site_width.v));
        }
        if (count_runtime && !result.bbox_active) {
          ++low_residual_refinement_exact_calls_avoided;
        }
        return result;
      };

      struct LowResidualChainMove
      {
        int idx = -1;
        int row = -1;
        int site = -1;
      };

      auto low_residual_chain_pin_xy =
          [&](const Pin* pin,
              const std::array<LowResidualChainMove,
                               kLowResidualChainMaxCells>& moves,
              const int move_count) {
            const Node* node = pin != nullptr ? pin->getNode() : nullptr;
            const int idx = low_residual_cell_idx(node);
            std::pair<int64_t, int64_t> center{0, 0};
            if (idx >= 0) {
              bool overridden = false;
              for (int i = 0; i < move_count; ++i) {
                if (moves[i].idx == idx) {
                  center = current_cell_center(idx, moves[i].row, moves[i].site);
                  overridden = true;
                  break;
                }
              }
              if (!overridden) {
                if (current_rows[idx] >= 0 && current_sites[idx] >= 0) {
                  center = current_cell_center(idx, -1, -1);
                } else {
                  center = {node->getCenterX().v, node->getCenterY().v};
                }
              }
            } else if (node != nullptr) {
              center = {node->getCenterX().v, node->getCenterY().v};
            }
            return std::pair<int64_t, int64_t>{
                center.first + (pin != nullptr ? pin->getOffsetX().v : 0),
                center.second + (pin != nullptr ? pin->getOffsetY().v : 0)};
          };

      auto low_residual_full_net_hpwl_chain =
          [&](const int net_idx,
              const std::array<LowResidualChainMove,
                               kLowResidualChainMaxCells>& moves,
              const int move_count) {
            const LowResidualNetSummary& summary
                = low_residual_net_summaries[net_idx];
            int64_t min_x = std::numeric_limits<int64_t>::max();
            int64_t max_x = std::numeric_limits<int64_t>::min();
            int64_t min_y = std::numeric_limits<int64_t>::max();
            int64_t max_y = std::numeric_limits<int64_t>::min();
            int pin_count = 0;
            for (const Pin* pin : summary.edge->getPins()) {
              if (pin == nullptr || pin->getNode() == nullptr) {
                continue;
              }
              const auto [pin_x, pin_y]
                  = low_residual_chain_pin_xy(pin, moves, move_count);
              min_x = std::min(min_x, pin_x);
              max_x = std::max(max_x, pin_x);
              min_y = std::min(min_y, pin_y);
              max_y = std::max(max_y, pin_y);
              ++pin_count;
            }
            if (pin_count <= 1) {
              return int64_t{0};
            }
            return (max_x - min_x) + (max_y - min_y);
          };

      auto low_residual_chain_delta =
          [&](const std::array<LowResidualChainMove,
                               kLowResidualChainMaxCells>& moves,
              const int move_count,
              const bool count_runtime) {
            LowResidualDelta result;
            if (count_runtime) {
              ++low_residual_chain_fast_delta_calls;
            }
            std::vector<int> touched_nets;
            for (int i = 0; i < move_count; ++i) {
              const int move_idx = moves[i].idx;
              if (move_idx < 0
                  || move_idx
                         >= static_cast<int>(low_residual_cell_nets.size())) {
                continue;
              }
              for (const LowResidualIncidentNet& incident :
                   low_residual_cell_nets[move_idx]) {
                if (std::find(touched_nets.begin(),
                              touched_nets.end(),
                              incident.net_idx)
                    == touched_nets.end()) {
                  touched_nets.push_back(incident.net_idx);
                }
              }
            }

            for (const int net_idx : touched_nets) {
              const LowResidualNetSummary& summary
                  = low_residual_net_summaries[net_idx];
              if (!summary.valid) {
                continue;
              }
              int moving_min_x_count = 0;
              int moving_max_x_count = 0;
              int moving_min_y_count = 0;
              int moving_max_y_count = 0;
              bool insertion_changes_bbox = false;
              for (const Pin* pin : summary.edge->getPins()) {
                if (pin == nullptr || pin->getNode() == nullptr) {
                  continue;
                }
                const int pin_cell_idx = low_residual_cell_idx(pin->getNode());
                bool moving_pin = false;
                for (int i = 0; i < move_count; ++i) {
                  if (moves[i].idx == pin_cell_idx) {
                    moving_pin = true;
                    break;
                  }
                }
                if (!moving_pin) {
                  continue;
                }
                const auto [old_pin_x, old_pin_y]
                    = low_residual_pin_xy(pin, -1, -1, -1, -1, -1, -1);
                const auto [new_pin_x, new_pin_y]
                    = low_residual_chain_pin_xy(pin, moves, move_count);
                moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
                moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
                moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
                moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
                insertion_changes_bbox
                    = insertion_changes_bbox || new_pin_x < summary.min_x
                      || new_pin_x > summary.max_x || new_pin_y < summary.min_y
                      || new_pin_y > summary.max_y;
              }
              const bool removal_changes_bbox
                  = (moving_min_x_count > 0
                     && moving_min_x_count >= summary.min_x_count)
                    || (moving_max_x_count > 0
                        && moving_max_x_count >= summary.max_x_count)
                    || (moving_min_y_count > 0
                        && moving_min_y_count >= summary.min_y_count)
                    || (moving_max_y_count > 0
                        && moving_max_y_count >= summary.max_y_count);
              if (!removal_changes_bbox && !insertion_changes_bbox) {
                continue;
              }
              result.bbox_active = true;
              if (count_runtime) {
                ++low_residual_chain_full_net_scans;
              }
              const int64_t new_hpwl
                  = low_residual_full_net_hpwl_chain(
                      net_idx, moves, move_count);
              result.hpwl_delta_sites
                  += static_cast<double>(new_hpwl - summary.hpwl_dbu)
                     / static_cast<double>(std::max(1, site_width.v));
            }
            if (count_runtime && !result.bbox_active) {
              ++low_residual_chain_exact_calls_avoided;
            }
            return result;
          };

      auto low_residual_target_release_delta =
          [&](const std::array<LowResidualChainMove,
                               kLowResidualChainMaxCells>& moves,
              const int move_count) {
            LowResidualDelta result;
            std::vector<int> touched_nets;
            for (int i = 0; i < move_count; ++i) {
              const int move_idx = moves[i].idx;
              if (move_idx < 0
                  || move_idx
                         >= static_cast<int>(low_residual_cell_nets.size())) {
                continue;
              }
              for (const LowResidualIncidentNet& incident :
                   low_residual_cell_nets[move_idx]) {
                if (std::find(touched_nets.begin(),
                              touched_nets.end(),
                              incident.net_idx)
                    == touched_nets.end()) {
                  touched_nets.push_back(incident.net_idx);
                }
              }
            }

            for (const int net_idx : touched_nets) {
              const LowResidualNetSummary& summary
                  = low_residual_net_summaries[net_idx];
              if (!summary.valid) {
                continue;
              }
              int moving_min_x_count = 0;
              int moving_max_x_count = 0;
              int moving_min_y_count = 0;
              int moving_max_y_count = 0;
              bool insertion_changes_bbox = false;
              for (const Pin* pin : summary.edge->getPins()) {
                if (pin == nullptr || pin->getNode() == nullptr) {
                  continue;
                }
                const int pin_cell_idx = low_residual_cell_idx(pin->getNode());
                bool moving_pin = false;
                for (int i = 0; i < move_count; ++i) {
                  if (moves[i].idx == pin_cell_idx) {
                    moving_pin = true;
                    break;
                  }
                }
                if (!moving_pin) {
                  continue;
                }
                const auto [old_pin_x, old_pin_y]
                    = low_residual_pin_xy(pin, -1, -1, -1, -1, -1, -1);
                const auto [new_pin_x, new_pin_y]
                    = low_residual_chain_pin_xy(pin, moves, move_count);
                moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
                moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
                moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
                moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
                insertion_changes_bbox
                    = insertion_changes_bbox || new_pin_x < summary.min_x
                      || new_pin_x > summary.max_x || new_pin_y < summary.min_y
                      || new_pin_y > summary.max_y;
              }
              const bool removal_changes_bbox
                  = (moving_min_x_count > 0
                     && moving_min_x_count >= summary.min_x_count)
                    || (moving_max_x_count > 0
                        && moving_max_x_count >= summary.max_x_count)
                    || (moving_min_y_count > 0
                        && moving_min_y_count >= summary.min_y_count)
                    || (moving_max_y_count > 0
                        && moving_max_y_count >= summary.max_y_count);
              if (!removal_changes_bbox && !insertion_changes_bbox) {
                continue;
              }
              result.bbox_active = true;
              ++low_residual_target_release_full_net_scans;
              const int64_t new_hpwl
                  = low_residual_full_net_hpwl_chain(
                      net_idx, moves, move_count);
              result.hpwl_delta_sites
                  += static_cast<double>(new_hpwl - summary.hpwl_dbu)
                     / static_cast<double>(std::max(1, site_width.v));
            }
            if (!result.bbox_active) {
              ++low_residual_target_release_exact_calls_avoided;
            }
            return result;
          };

      auto low_residual_refresh_nets = [&](const int idx_a,
                                           const int idx_b) {
        std::vector<int> touched_nets;
        auto add_touched_net = [&](const int net_idx) {
          if (std::find(touched_nets.begin(), touched_nets.end(), net_idx)
              == touched_nets.end()) {
            touched_nets.push_back(net_idx);
          }
        };
        if (idx_a >= 0 && idx_a < static_cast<int>(low_residual_cell_nets.size())) {
          for (const LowResidualIncidentNet& incident :
               low_residual_cell_nets[idx_a]) {
            add_touched_net(incident.net_idx);
          }
        }
        if (idx_b >= 0 && idx_b < static_cast<int>(low_residual_cell_nets.size())) {
          for (const LowResidualIncidentNet& incident :
               low_residual_cell_nets[idx_b]) {
            add_touched_net(incident.net_idx);
          }
        }
        for (const int net_idx : touched_nets) {
          low_residual_recompute_net(net_idx);
        }
      };

      auto low_residual_refresh_chain_nets =
          [&](const std::array<LowResidualChainMove,
                               kLowResidualChainMaxCells>& moves,
              const int move_count) {
            std::vector<int> touched_nets;
            auto add_touched_net = [&](const int net_idx) {
              if (std::find(touched_nets.begin(),
                            touched_nets.end(),
                            net_idx)
                  == touched_nets.end()) {
                touched_nets.push_back(net_idx);
              }
            };
            for (int i = 0; i < move_count; ++i) {
              const int move_idx = moves[i].idx;
              if (move_idx < 0
                  || move_idx
                         >= static_cast<int>(low_residual_cell_nets.size())) {
                continue;
              }
              for (const LowResidualIncidentNet& incident :
                   low_residual_cell_nets[move_idx]) {
                add_touched_net(incident.net_idx);
              }
            }
            for (const int net_idx : touched_nets) {
              low_residual_recompute_net(net_idx);
            }
          };

      struct LowResidualFrontierCell
      {
        int idx = -1;
        double priority = 0.0;
        double observed_gain_density = 0.0;
      };
      std::vector<LowResidualFrontierCell> frontier;
      frontier.reserve(std::min(static_cast<int>(cells.size()),
                                kLowResidualRefinementFrontierCap));
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        const LegalmPlaceCell& item = cells[idx];
        const int row = current_rows[idx];
        const int site = current_sites[idx];
        if (row < 0 || site < 0 || low_residual_cell_nets[idx].empty()) {
          continue;
        }
        const int disp = displacement_sites(item, row, site);
        const bool row_escape = row != item.desired_row;
        const double cheap_hpwl_delta = hpwl_delta_sites(item, row, site);
        const bool tail_frontier = disp > max_disp_threshold_sites / 2;
        const bool bbox_frontier = low_residual_cell_has_bbox_term(idx);
        if (!bbox_frontier && !row_escape && !tail_frontier) {
          ++low_residual_refinement_frontier_pruned_no_bbox_terms;
          continue;
        }

        double observed_hpwl_gain = 0.0;
        if (bbox_frontier) {
          LowResidualDelta desired_delta = low_residual_move_delta(
              idx, item.desired_row, item.desired_site, false);
          observed_hpwl_gain
              = std::max(observed_hpwl_gain, -desired_delta.hpwl_delta_sites);
          LowResidualDelta original_delta = low_residual_move_delta(
              idx, item.original_row, item.original_site, false);
          observed_hpwl_gain
              = std::max(observed_hpwl_gain, -original_delta.hpwl_delta_sites);
        }
        observed_hpwl_gain = std::max(0.0, observed_hpwl_gain);
        const double observed_gain_density
            = observed_hpwl_gain
              / static_cast<double>(std::max<size_t>(
                  1, low_residual_cell_nets[idx].size()));
        const bool hpwl_frontier
            = observed_hpwl_gain > 0.0 || cheap_hpwl_delta > 0.0;
        if (!row_escape && !tail_frontier && !hpwl_frontier) {
          continue;
        }
        if (!row_escape && !tail_frontier && observed_hpwl_gain <= 0.05
            && cheap_hpwl_delta <= 0.25) {
          ++low_residual_refinement_frontier_pruned_low_density;
          continue;
        }
        const double priority
            = 96.0 * observed_gain_density + 2.0 * observed_hpwl_gain
              + static_cast<double>(disp)
              + 2.0 * static_cast<double>(
                          std::max(0, disp - max_disp_threshold_sites))
              + (row_escape ? static_cast<double>(max_disp_threshold_sites)
                            : 0.0)
              + std::max(0.0, cheap_hpwl_delta);
        frontier.push_back({idx, priority, observed_gain_density});
      }
      low_residual_refinement_frontier_before_cap
          = static_cast<int>(frontier.size());
      std::stable_sort(frontier.begin(),
                       frontier.end(),
                       [](const LowResidualFrontierCell& lhs,
                          const LowResidualFrontierCell& rhs) {
                         if (lhs.priority != rhs.priority) {
                           return lhs.priority > rhs.priority;
                         }
                         if (lhs.observed_gain_density
                             != rhs.observed_gain_density) {
                           return lhs.observed_gain_density
                                  > rhs.observed_gain_density;
                         }
                         return lhs.idx < rhs.idx;
                       });
      if (static_cast<int>(frontier.size())
          > kLowResidualRefinementFrontierCap) {
        frontier.resize(kLowResidualRefinementFrontierCap);
      }
      low_residual_refinement_frontier
          = static_cast<int>(frontier.size());

      std::vector<int> low_residual_site_owner(
          static_cast<size_t>(row_count) * static_cast<size_t>(site_count),
          -1);
      auto set_low_residual_owner = [&](const int idx,
                                        const int row,
                                        const int site,
                                        const int owner) {
        if (idx < 0 || row < 0 || site < 0) {
          return;
        }
        const LegalmPlaceCell& owner_item = cells[idx];
        for (int dy = 0; dy < owner_item.height_rows; ++dy) {
          const int y = row + dy;
          if (y < 0 || y >= row_count) {
            continue;
          }
          for (int dx = 0; dx < owner_item.width_sites; ++dx) {
            const int x = site + dx;
            if (x < 0 || x >= site_count) {
              continue;
            }
            low_residual_site_owner[static_cast<size_t>(y)
                                        * static_cast<size_t>(site_count)
                                    + static_cast<size_t>(x)]
                = owner;
          }
        }
      };
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        set_low_residual_owner(
            idx, current_rows[idx], current_sites[idx], idx);
      }

      struct LowResidualCandidate
      {
        int row = -1;
        int site = -1;
        bool current_net_anchor = false;
      };
      struct LowResidualSwapCandidate
      {
        int other_idx = -1;
        int row = -1;
        int site = -1;
        bool current_net_anchor = false;
      };

      for (const LowResidualFrontierCell& frontier_cell : frontier) {
        const int idx = frontier_cell.idx;
        LegalmPlaceCell& item = cells[idx];
        const int old_row = current_rows[idx];
        const int old_site = current_sites[idx];
        if (old_row < 0 || old_site < 0) {
          continue;
        }

        std::array<LowResidualCandidate, kLowResidualRefinementCandidateCap>
            candidates{};
        int candidate_count = 0;
        std::array<LowResidualSwapCandidate,
                   kLowResidualRefinementCandidateCap>
            swap_candidates{};
        int swap_candidate_count = 0;
        auto add_candidate = [&](const int row,
                                 const int site,
                                 const bool current_net_anchor = false) {
          if (row < 0 || row + item.height_rows > row_count || site < 0
              || site + item.width_sites > site_count) {
            return;
          }
          for (int i = 0; i < candidate_count; ++i) {
            if (candidates[i].row == row && candidates[i].site == site) {
              candidates[i].current_net_anchor
                  = candidates[i].current_net_anchor || current_net_anchor;
              return;
            }
          }
          if (candidate_count
              >= static_cast<int>(kLowResidualRefinementCandidateCap)) {
            return;
          }
          candidates[candidate_count++] = {row, site, current_net_anchor};
        };
        auto add_swap_candidate = [&](const int row,
                                      const int site,
                                      const bool current_net_anchor = false) {
          if (item.height_rows != 1 || row < 0 || row >= row_count || site < 0
              || site >= site_count) {
            return;
          }
          const int owner
              = low_residual_site_owner[static_cast<size_t>(row)
                                            * static_cast<size_t>(site_count)
                                        + static_cast<size_t>(site)];
          if (owner < 0 || owner == idx
              || owner >= static_cast<int>(cells.size())) {
            return;
          }
          const LegalmPlaceCell& other = cells[owner];
          if (other.height_rows != item.height_rows
              || other.width_sites != item.width_sites
              || other.group != item.group || current_rows[owner] != row
              || current_sites[owner] != site) {
            return;
          }
          for (int i = 0; i < swap_candidate_count; ++i) {
            if (swap_candidates[i].other_idx == owner) {
              swap_candidates[i].current_net_anchor
                  = swap_candidates[i].current_net_anchor
                    || current_net_anchor;
              return;
            }
          }
          if (swap_candidate_count
              >= static_cast<int>(kLowResidualRefinementCandidateCap)) {
            return;
          }
          swap_candidates[swap_candidate_count++]
              = {owner, row, site, current_net_anchor};
        };
        auto add_interval_candidates = [&](const int row,
                                           const int target_site,
                                           const bool current_net_anchor,
                                           const int probe_limit,
                                           const int endpoint_probe_limit) {
          if (row < 0 || row >= row_count) {
            return;
          }
          const auto& intervals = free_intervals[row];
          auto it = std::lower_bound(
              intervals.begin(),
              intervals.end(),
              target_site,
              [](const Interval& interval, const int target) {
                return interval.second <= target;
              });
          auto add_from_interval = [&](const Interval& interval,
                                       const bool include_endpoints) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              return;
            }
            const int clamped = std::clamp(target_site,
                                           interval.first,
                                           interval.second - item.width_sites);
            add_candidate(row, clamped, current_net_anchor);
            if (include_endpoints) {
              add_candidate(row, interval.first, current_net_anchor);
              add_candidate(
                  row, interval.second - item.width_sites, current_net_anchor);
            }
          };
          int forward_samples = 0;
          for (auto fwd = it; fwd != intervals.end()
                            && forward_samples < probe_limit;
               ++fwd, ++forward_samples) {
            add_from_interval(*fwd, forward_samples < endpoint_probe_limit);
          }
          auto back = it;
          int backward_samples = 0;
          while (back != intervals.begin() && backward_samples < probe_limit) {
            --back;
            add_from_interval(*back, backward_samples < endpoint_probe_limit);
            ++backward_samples;
          }
        };

        release_slot(
            old_row, old_site, item.width_sites, item.height_rows, item.group);
        add_candidate(old_row, old_site);

        struct LowResidualCurrentNetAnchor
        {
          int row = -1;
          int site = -1;
          double score = -std::numeric_limits<double>::infinity();
          int displacement = std::numeric_limits<int>::max();
          bool safe_alternate = false;
        };
        std::array<LowResidualCurrentNetAnchor,
                   kLowResidualCurrentNetAnchorCap>
            current_net_anchors{};
        int current_net_anchor_count = 0;
        const int old_anchor_disp = displacement_sites(item, old_row, old_site);
        const int old_target_deviation
            = std::abs(old_row - item.desired_row) * row_equiv_sites
              + std::abs(old_site - item.desired_site);
        auto grid_from_center = [&](const int64_t center_x,
                                    const int64_t center_y) {
          const int64_t left
              = center_x - static_cast<int64_t>(item.cell->getWidth().v) / 2;
          const int64_t bottom
              = center_y - static_cast<int64_t>(item.cell->getHeight().v) / 2;
          const int left_dbu = static_cast<int>(std::clamp<int64_t>(
              left,
              std::numeric_limits<int>::min(),
              std::numeric_limits<int>::max()));
          const int bottom_dbu = static_cast<int>(std::clamp<int64_t>(
              bottom,
              std::numeric_limits<int>::min(),
              std::numeric_limits<int>::max()));
          const GridPt grid
              = legalGridPt(item.cell, {DbuX{left_dbu}, DbuY{bottom_dbu}});
          return GridPt{GridX{std::clamp(
                            grid.x.v,
                            0,
                            std::max(0, site_count - item.width_sites))},
                        GridY{std::clamp(grid.y.v, 0, row_count - 1)}};
        };
        auto insert_current_net_anchor = [&](const int row,
                                             const int site,
                                             const double gain_sites) {
          if (row < 0 || row + item.height_rows > row_count || site < 0
              || site + item.width_sites > site_count
              || gain_sites < kLowResidualCurrentNetAnchorMinGainSites) {
            return false;
          }
          if (std::abs(row - old_row) > local_vertical_radius
              && std::abs(row - item.desired_row) > local_vertical_radius) {
            return false;
          }
          const int displacement = displacement_sites(item, row, site);
          const int extra_disp = std::max(0, displacement - old_anchor_disp);
          const int target_deviation
              = std::abs(row - item.desired_row) * row_equiv_sites
                + std::abs(site - item.desired_site);
          const int target_loss
              = std::max(0, target_deviation - old_target_deviation);
          const int target_relief
              = std::max(0, old_target_deviation - target_deviation);
          if (extra_disp > 0
              && gain_sites
                     < 1.15 * static_cast<double>(extra_disp)
                           + 0.08 * static_cast<double>(target_loss) + 2.0) {
            ++low_residual_current_net_anchor_prefilter_rejects;
            return false;
          }
          if (old_target_deviation <= 1 && extra_disp > 0
              && target_relief <= 0) {
            ++low_residual_current_net_anchor_target_rejects;
            return false;
          }
          const int tail = std::max(0, displacement - max_disp_threshold_sites);
          const bool safe_alternate = extra_disp == 0 || target_relief > 0;
          const double score
              = gain_sites
                    / (1.0 + 0.72 * static_cast<double>(extra_disp)
                       + 0.08 * static_cast<double>(target_loss)
                       + 0.10 * static_cast<double>(tail))
                + 0.04 * static_cast<double>(target_relief);
          if (score <= 0.0) {
            ++low_residual_current_net_anchor_prefilter_rejects;
            return false;
          }
          for (int i = 0; i < current_net_anchor_count; ++i) {
            if (current_net_anchors[i].row == row
                && current_net_anchors[i].site == site) {
              if (score > current_net_anchors[i].score) {
                current_net_anchors[i].score = score;
                current_net_anchors[i].displacement = displacement;
                current_net_anchors[i].safe_alternate = safe_alternate;
              }
              return true;
            }
          }
          LowResidualCurrentNetAnchor candidate{
              row, site, score, displacement, safe_alternate};
          int insert_at = current_net_anchor_count;
          for (int i = 0; i < current_net_anchor_count; ++i) {
            if (candidate.score > current_net_anchors[i].score
                || (candidate.score == current_net_anchors[i].score
                    && candidate.displacement
                           < current_net_anchors[i].displacement)) {
              insert_at = i;
              break;
            }
          }
          if (!candidate.safe_alternate && current_net_anchor_count > 0
              && insert_at > 0) {
            return false;
          }
          if (insert_at >= kLowResidualCurrentNetAnchorCap) {
            return false;
          }
          const int limit = std::min(current_net_anchor_count,
                                     kLowResidualCurrentNetAnchorCap - 1);
          for (int i = limit; i > insert_at; --i) {
            current_net_anchors[i] = current_net_anchors[i - 1];
          }
          current_net_anchors[insert_at] = candidate;
          current_net_anchor_count = std::min(
              current_net_anchor_count + 1, kLowResidualCurrentNetAnchorCap);
          if (safe_alternate && insert_at > 0) {
            ++low_residual_current_net_anchor_safe_alternates;
          }
          return true;
        };
        const auto [old_center_x, old_center_y]
            = current_cell_center(idx, old_row, old_site);
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[idx]) {
          if (incident.net_idx < 0
              || incident.net_idx
                     >= static_cast<int>(low_residual_net_summaries.size())) {
            continue;
          }
          const LowResidualNetSummary& summary
              = low_residual_net_summaries[incident.net_idx];
          if (!summary.valid || summary.edge == nullptr) {
            continue;
          }
          for (const auto& pin_offset : incident.pin_offsets) {
            const int64_t pin_x = old_center_x + pin_offset.first;
            const int64_t pin_y = old_center_y + pin_offset.second;
            const int64_t other_min_x
                = (pin_x == summary.min_x && summary.min_x_count <= 1)
                      ? summary.second_min_x
                      : summary.min_x;
            const int64_t other_max_x
                = (pin_x == summary.max_x && summary.max_x_count <= 1)
                      ? summary.second_max_x
                      : summary.max_x;
            const int64_t other_min_y
                = (pin_y == summary.min_y && summary.min_y_count <= 1)
                      ? summary.second_min_y
                      : summary.min_y;
            const int64_t other_max_y
                = (pin_y == summary.max_y && summary.max_y_count <= 1)
                      ? summary.second_max_y
                      : summary.max_y;
            const int64_t target_pin_x
                = std::clamp(pin_x, other_min_x, other_max_x);
            const int64_t target_pin_y
                = std::clamp(pin_y, other_min_y, other_max_y);
            const int64_t delta_x = std::llabs(target_pin_x - pin_x);
            const int64_t delta_y = std::llabs(target_pin_y - pin_y);
            if (delta_x == 0 && delta_y == 0) {
              continue;
            }
            ++low_residual_current_net_anchor_raw_terms;
            const double gain_sites
                = static_cast<double>(delta_x + delta_y)
                  / static_cast<double>(std::max(1, site_width.v));
            const int64_t target_center_x = target_pin_x - pin_offset.first;
            const int64_t target_center_y = target_pin_y - pin_offset.second;
            const GridPt projected
                = grid_from_center(target_center_x, target_center_y);
            bool active_term = insert_current_net_anchor(
                projected.y.v, projected.x.v, gain_sites);
            const GridPt shadow = grid_from_center(
                (old_center_x + 3 * target_center_x) / 4,
                (old_center_y + 3 * target_center_y) / 4);
            active_term
                = insert_current_net_anchor(
                      shadow.y.v, shadow.x.v, 0.75 * gain_sites)
                  || active_term;
            if (active_term) {
              ++low_residual_current_net_anchor_terms;
            }
          }
        }
        low_residual_current_net_anchor_candidates
            += current_net_anchor_count;
        for (int anchor_idx = 0; anchor_idx < current_net_anchor_count;
             ++anchor_idx) {
          const LowResidualCurrentNetAnchor& anchor
              = current_net_anchors[anchor_idx];
          const bool primary_anchor = anchor_idx == 0;
          const int anchor_probe_limit
              = primary_anchor ? kLowResidualRefinementIntervalProbeLimit : 1;
          const int endpoint_probe_limit = 0;
          add_interval_candidates(anchor.row,
                                  anchor.site,
                                  true,
                                  anchor_probe_limit,
                                  endpoint_probe_limit);
          add_swap_candidate(anchor.row, anchor.site, true);
        }

        std::array<int, 12> row_anchors{};
        int row_anchor_count = 0;
        auto add_row_anchor = [&](const int row) {
          if (row < 0 || row + item.height_rows > row_count) {
            return;
          }
          for (int i = 0; i < row_anchor_count; ++i) {
            if (row_anchors[i] == row) {
              return;
            }
          }
          if (row_anchor_count < static_cast<int>(row_anchors.size())) {
            row_anchors[row_anchor_count++] = row;
          }
        };
        add_row_anchor(old_row);
        add_row_anchor(item.desired_row);
        add_row_anchor(item.original_row);
        for (int delta = 1; delta <= local_vertical_radius; ++delta) {
          add_row_anchor(old_row + delta * item.vertical_step_rows);
          add_row_anchor(old_row - delta * item.vertical_step_rows);
          add_row_anchor(item.desired_row + delta * item.vertical_step_rows);
          add_row_anchor(item.desired_row - delta * item.vertical_step_rows);
        }

        const std::array<int, 4> site_anchors{
            old_site,
            item.desired_site,
            item.original_site,
            (item.desired_site + item.original_site) / 2};
        for (int i = 0; i < row_anchor_count; ++i) {
          const int row = row_anchors[i];
          for (const int site_anchor : site_anchors) {
            add_interval_candidates(row,
                                    site_anchor,
                                    false,
                                    kLowResidualRefinementIntervalProbeLimit,
                                    kLowResidualRefinementIntervalProbeLimit);
            add_swap_candidate(row, site_anchor);
          }
        }
        for (int dy = -local_vertical_radius; dy <= local_vertical_radius;
             ++dy) {
          const int row = old_row + dy * item.vertical_step_rows;
          if (row < 0 || row + item.height_rows > row_count) {
            continue;
          }
          for (int dx = -local_horizontal_steps; dx <= local_horizontal_steps;
               ++dx) {
            add_candidate(row, old_site + dx);
            add_swap_candidate(row, old_site + dx);
          }
        }

        const int old_disp = displacement_sites(item, old_row, old_site);
        const double old_disp_cost = displacement_cost(item, old_row, old_site);
        double best_score_delta = 0.0;
        double best_hpwl_delta = 0.0;
        int best_disp_delta = 0;
        int best_row = old_row;
        int best_site = old_site;
        int best_swap_idx = -1;
        int best_chain_count = 0;
        bool best_current_net_anchor = false;
        std::array<LowResidualChainMove, kLowResidualChainMaxCells>
            best_chain_moves{};
        ++low_residual_refinement_attempted;

        for (int candidate_idx = 0; candidate_idx < candidate_count;
             ++candidate_idx) {
          const int row = candidates[candidate_idx].row;
          const int site = candidates[candidate_idx].site;
          if (row == old_row && site == old_site) {
            continue;
          }
          ++low_residual_refinement_candidate_evals;
          if (!fits_all_rows(
                  row, site, item.width_sites, item.height_rows, item.group)) {
            ++low_residual_refinement_legal_rejects;
            continue;
          }
          if (!row_site_compatible(item, row, site)) {
            ++low_residual_refinement_static_rejects;
            continue;
          }
          const int new_disp = displacement_sites(item, row, site);
          const int disp_delta = new_disp - old_disp;
          const LowResidualDelta hpwl_delta_result
              = low_residual_move_delta(idx, row, site, true);
          if (candidates[candidate_idx].current_net_anchor) {
            ++low_residual_current_net_anchor_scored;
          }
          const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
          if (!hpwl_delta_result.bbox_active && disp_delta >= 0) {
            ++low_residual_refinement_no_bbox_candidate_skips;
            continue;
          }
          const double disp_cost_delta
              = displacement_cost(item, row, site) - old_disp_cost;
          const bool hpwl_improves
              = hpwl_delta <= -kLowResidualExactHpwlMinGainSites;
          const bool disp_recovers
              = disp_delta < 0
                && hpwl_delta <= kLowResidualExactHpwlMinGainSites;
          if (!hpwl_improves && !disp_recovers) {
            continue;
          }
          if (new_disp > old_disp + displacement_slack
              && hpwl_delta
                     > -4.0 * static_cast<double>(
                                  std::max(1, new_disp - old_disp))) {
            ++low_residual_refinement_tail_rejects;
            continue;
          }
          const double hpwl_weight
              = candidates[candidate_idx].current_net_anchor
                    ? kLowResidualCurrentNetAnchorHpwlWeight
                    : kLowResidualExactHpwlWeight;
          const double score_delta = disp_cost_delta + hpwl_weight * hpwl_delta;
          if (score_delta < best_score_delta - 0.25
              || (std::abs(score_delta - best_score_delta) <= 0.25
                  && (hpwl_delta < best_hpwl_delta
                      || (hpwl_delta == best_hpwl_delta
                          && disp_delta < best_disp_delta)))) {
            best_score_delta = score_delta;
            best_hpwl_delta = hpwl_delta;
            best_disp_delta = disp_delta;
            best_row = row;
            best_site = site;
            best_swap_idx = -1;
            best_chain_count = 0;
            best_current_net_anchor
                = candidates[candidate_idx].current_net_anchor;
          }
        }

        for (int candidate_idx = 0; candidate_idx < swap_candidate_count;
             ++candidate_idx) {
          const int other_idx = swap_candidates[candidate_idx].other_idx;
          if (other_idx < 0 || other_idx >= static_cast<int>(cells.size())) {
            continue;
          }
          const LegalmPlaceCell& other = cells[other_idx];
          const int other_row = current_rows[other_idx];
          const int other_site = current_sites[other_idx];
          if (other_row < 0 || other_site < 0
              || other_row != swap_candidates[candidate_idx].row
              || other_site != swap_candidates[candidate_idx].site) {
            continue;
          }
          ++low_residual_refinement_swap_evals;
          if (!row_site_compatible(item, other_row, other_site)
              || !row_site_compatible(other, old_row, old_site)) {
            ++low_residual_refinement_static_rejects;
            continue;
          }
          const int old_other_disp
              = displacement_sites(other, other_row, other_site);
          const double old_other_disp_cost
              = displacement_cost(other, other_row, other_site);
          const int new_disp = displacement_sites(item, other_row, other_site);
          const int new_other_disp
              = displacement_sites(other, old_row, old_site);
          const int disp_delta
              = (new_disp + new_other_disp) - (old_disp + old_other_disp);
          const LowResidualDelta hpwl_delta_result = low_residual_swap_delta(
              idx, other_row, other_site, other_idx, old_row, old_site, true);
          if (swap_candidates[candidate_idx].current_net_anchor) {
            ++low_residual_current_net_anchor_scored;
          }
          const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
          if (!hpwl_delta_result.bbox_active && disp_delta >= 0) {
            ++low_residual_refinement_no_bbox_candidate_skips;
            continue;
          }
          const double disp_cost_delta
              = displacement_cost(item, other_row, other_site)
                + displacement_cost(other, old_row, old_site) - old_disp_cost
                - old_other_disp_cost;
          const bool hpwl_improves
              = hpwl_delta <= -kLowResidualExactHpwlMinGainSites;
          const bool disp_recovers
              = disp_delta < 0
                && hpwl_delta <= kLowResidualExactHpwlMinGainSites;
          if (!hpwl_improves && !disp_recovers) {
            continue;
          }
          if (std::max(new_disp, new_other_disp)
                  > std::max(old_disp, old_other_disp) + displacement_slack
              && hpwl_delta
                     > -4.0 * static_cast<double>(
                                  std::max(1, std::max(0, disp_delta)))) {
            ++low_residual_refinement_tail_rejects;
            continue;
          }
          const double hpwl_weight
              = swap_candidates[candidate_idx].current_net_anchor
                    ? kLowResidualCurrentNetAnchorHpwlWeight
                    : kLowResidualExactHpwlWeight;
          const double score_delta = disp_cost_delta + hpwl_weight * hpwl_delta;
          if (score_delta < best_score_delta - 0.25
              || (std::abs(score_delta - best_score_delta) <= 0.25
                  && (hpwl_delta < best_hpwl_delta
                      || (hpwl_delta == best_hpwl_delta
                          && disp_delta < best_disp_delta)))) {
            best_score_delta = score_delta;
            best_hpwl_delta = hpwl_delta;
            best_disp_delta = disp_delta;
            best_row = other_row;
            best_site = other_site;
            best_swap_idx = other_idx;
            best_chain_count = 0;
            best_current_net_anchor
                = swap_candidates[candidate_idx].current_net_anchor;
          }
        }

        auto evaluate_chain_target = [&](const int target_site) {
          if (item.height_rows != 1 || item.width_sites <= 0
              || old_row < 0 || old_row >= row_count || target_site < 0
              || target_site + item.width_sites > site_count
              || target_site == old_site) {
            return;
          }
          const int width = item.width_sites;
          const int delta_site = target_site - old_site;
          if (std::abs(delta_site) > kLowResidualChainMaxSpanSites
              || delta_site % width != 0) {
            return;
          }
          const int direction = delta_site > 0 ? 1 : -1;
          const int chain_steps = std::abs(delta_site) / width;
          if (chain_steps <= 0
              || chain_steps + 1 > kLowResidualChainMaxCells) {
            return;
          }
          ++low_residual_chain_candidates;
          if (!row_site_compatible(item, old_row, target_site)) {
            ++low_residual_chain_static_rejects;
            return;
          }

          std::array<LowResidualChainMove, kLowResidualChainMaxCells> moves{};
          moves[0] = {idx, old_row, target_site};
          int move_count = 1;
          int old_disp_sum = old_disp;
          int new_disp_sum = displacement_sites(item, old_row, target_site);
          double old_disp_cost_sum = old_disp_cost;
          double new_disp_cost_sum
              = displacement_cost(item, old_row, target_site);
          int max_old_disp = old_disp;
          int max_new_disp = new_disp_sum;

          for (int step = 1; step <= chain_steps; ++step) {
            const int from_site = old_site + direction * step * width;
            const int to_site = from_site - direction * width;
            if (from_site < 0 || from_site >= site_count || to_site < 0
                || to_site + width > site_count) {
              ++low_residual_chain_legal_rejects;
              return;
            }
            const int owner
                = low_residual_site_owner[static_cast<size_t>(old_row)
                                              * static_cast<size_t>(site_count)
                                          + static_cast<size_t>(from_site)];
            if (owner < 0 || owner == idx
                || owner >= static_cast<int>(cells.size())) {
              ++low_residual_chain_legal_rejects;
              return;
            }
            const LegalmPlaceCell& shifted = cells[owner];
            if (shifted.height_rows != 1 || shifted.width_sites != width
                || shifted.group != item.group || current_rows[owner] != old_row
                || current_sites[owner] != from_site) {
              ++low_residual_chain_legal_rejects;
              return;
            }
            for (int i = 1; i < move_count; ++i) {
              if (moves[i].idx == owner) {
                ++low_residual_chain_legal_rejects;
                return;
              }
            }
            if (!row_site_compatible(shifted, old_row, to_site)) {
              ++low_residual_chain_static_rejects;
              return;
            }
            moves[move_count++] = {owner, old_row, to_site};
            const int old_shifted_disp
                = displacement_sites(shifted, old_row, from_site);
            const int new_shifted_disp
                = displacement_sites(shifted, old_row, to_site);
            old_disp_sum += old_shifted_disp;
            new_disp_sum += new_shifted_disp;
            old_disp_cost_sum += displacement_cost(shifted, old_row, from_site);
            new_disp_cost_sum += displacement_cost(shifted, old_row, to_site);
            max_old_disp = std::max(max_old_disp, old_shifted_disp);
            max_new_disp = std::max(max_new_disp, new_shifted_disp);
          }

          const int disp_delta = new_disp_sum - old_disp_sum;
          const LowResidualDelta hpwl_delta_result
              = low_residual_chain_delta(moves, move_count, true);
          const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
          if (!hpwl_delta_result.bbox_active && disp_delta >= 0) {
            ++low_residual_chain_no_bbox_skips;
            return;
          }
          const double disp_cost_delta
              = new_disp_cost_sum - old_disp_cost_sum;
          const bool hpwl_improves
              = hpwl_delta <= -kLowResidualExactHpwlMinGainSites;
          const bool disp_recovers
              = disp_delta < 0
                && hpwl_delta <= kLowResidualExactHpwlMinGainSites;
          if (!hpwl_improves && !disp_recovers) {
            return;
          }
          if (max_new_disp > max_old_disp + displacement_slack
              && hpwl_delta
                     > -3.0 * static_cast<double>(
                                  std::max(1, max_new_disp - max_old_disp))) {
            ++low_residual_chain_tail_rejects;
            return;
          }
          const double score_delta
              = disp_cost_delta + kLowResidualChainHpwlWeight * hpwl_delta;
          if (score_delta < best_score_delta - 0.25
              || (std::abs(score_delta - best_score_delta) <= 0.25
                  && (hpwl_delta < best_hpwl_delta
                      || (hpwl_delta == best_hpwl_delta
                          && disp_delta < best_disp_delta)))) {
            best_score_delta = score_delta;
            best_hpwl_delta = hpwl_delta;
            best_disp_delta = disp_delta;
            best_row = old_row;
            best_site = target_site;
            best_swap_idx = -1;
            best_chain_count = move_count;
            best_current_net_anchor = false;
            best_chain_moves = moves;
          }
        };

        if (item.height_rows == 1 && item.width_sites > 0) {
          const double low_gain_density = frontier_cell.observed_gain_density;
          const bool skip_chain_region
              = low_gain_density <= 0.01
                && displacement_sites(item, old_row, old_site)
                       <= max_disp_threshold_sites / 2
                && old_row == item.desired_row;
          if (skip_chain_region) {
            ++low_residual_frontier_regions_skipped;
          } else {
            std::array<int, 16> chain_targets{};
            int chain_target_count = 0;
            auto add_chain_target = [&](const int site) {
              if (site < 0 || site + item.width_sites > site_count
                  || site == old_site) {
                return;
              }
              for (int i = 0; i < chain_target_count; ++i) {
                if (chain_targets[i] == site) {
                  return;
                }
              }
              if (chain_target_count
                  < static_cast<int>(chain_targets.size())) {
                chain_targets[chain_target_count++] = site;
              }
            };
            auto add_aligned_anchor = [&](const int anchor_site) {
              if (anchor_site == old_site || item.width_sites <= 0) {
                return;
              }
              const int direction = anchor_site > old_site ? 1 : -1;
              const int max_steps = std::max(
                  1,
                  std::min(kLowResidualChainMaxCells - 1,
                           kLowResidualChainMaxSpanSites
                               / std::max(1, item.width_sites)));
              int steps = std::abs(anchor_site - old_site)
                          / std::max(1, item.width_sites);
              steps = std::clamp(steps, 1, max_steps);
              add_chain_target(
                  old_site + direction * steps * item.width_sites);
            };
            add_aligned_anchor(item.desired_site);
            add_aligned_anchor(item.original_site);
            add_aligned_anchor((item.desired_site + item.original_site) / 2);
            const int max_chain_steps = std::max(
                1,
                std::min(kLowResidualChainMaxCells - 1,
                         kLowResidualChainMaxSpanSites
                             / std::max(1, item.width_sites)));
            for (int step = 1; step <= max_chain_steps; ++step) {
              add_chain_target(old_site + step * item.width_sites);
              add_chain_target(old_site - step * item.width_sites);
            }
            for (int target_idx = 0; target_idx < chain_target_count;
                 ++target_idx) {
              evaluate_chain_target(chain_targets[target_idx]);
            }
          }
        }

        if (best_chain_count > 0) {
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
          for (int i = 0; i < best_chain_count; ++i) {
            const int move_idx = best_chain_moves[i].idx;
            set_low_residual_owner(move_idx,
                                   current_rows[move_idx],
                                   current_sites[move_idx],
                                   -1);
          }
          for (int i = 0; i < best_chain_count; ++i) {
            const int move_idx = best_chain_moves[i].idx;
            current_rows[move_idx] = best_chain_moves[i].row;
            current_sites[move_idx] = best_chain_moves[i].site;
          }
          for (int i = 0; i < best_chain_count; ++i) {
            const int move_idx = best_chain_moves[i].idx;
            set_low_residual_owner(move_idx,
                                   current_rows[move_idx],
                                   current_sites[move_idx],
                                   move_idx);
            if (stage3_changed[move_idx] == 0) {
              stage3_changed[move_idx] = 1;
              ++stage3_changed_cells;
            }
          }
          low_residual_refresh_chain_nets(best_chain_moves, best_chain_count);
          low_residual_refinement_moved += best_chain_count;
          low_residual_chain_moved_cells += best_chain_count;
          ++low_residual_chain_moves;
          low_residual_refinement_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_refinement_disp_gain_sites += -best_disp_delta;
          low_residual_chain_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_chain_disp_gain_sites += -best_disp_delta;
        } else if (best_swap_idx >= 0) {
          const int other_idx = best_swap_idx;
          const int other_row = current_rows[other_idx];
          const int other_site = current_sites[other_idx];
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
          current_rows[idx] = other_row;
          current_sites[idx] = other_site;
          current_rows[other_idx] = old_row;
          current_sites[other_idx] = old_site;
          set_low_residual_owner(idx, old_row, old_site, -1);
          set_low_residual_owner(other_idx, other_row, other_site, -1);
          set_low_residual_owner(idx, other_row, other_site, idx);
          set_low_residual_owner(other_idx, old_row, old_site, other_idx);
          low_residual_refresh_nets(idx, other_idx);
          if (stage3_changed[idx] == 0) {
            stage3_changed[idx] = 1;
            ++stage3_changed_cells;
          }
          if (stage3_changed[other_idx] == 0) {
            stage3_changed[other_idx] = 1;
            ++stage3_changed_cells;
          }
          low_residual_refinement_moved += 2;
          ++low_residual_refinement_swap_moves;
          if (other_row != old_row) {
            low_residual_refinement_row_moves += 2;
          }
          low_residual_refinement_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_refinement_disp_gain_sites += -best_disp_delta;
          if (best_current_net_anchor) {
            ++low_residual_current_net_anchor_moves;
            low_residual_current_net_anchor_hpwl_gain_sites += -best_hpwl_delta;
            low_residual_current_net_anchor_disp_gain_sites += -best_disp_delta;
          }
        } else if (best_row != old_row || best_site != old_site) {
          reserve_slot(best_row,
                       best_site,
                       item.width_sites,
                       item.height_rows,
                       item.group);
          current_rows[idx] = best_row;
          current_sites[idx] = best_site;
          set_low_residual_owner(idx, old_row, old_site, -1);
          set_low_residual_owner(idx, best_row, best_site, idx);
          low_residual_refresh_nets(idx, -1);
          if (stage3_changed[idx] == 0) {
            stage3_changed[idx] = 1;
            ++stage3_changed_cells;
          }
          ++low_residual_refinement_moved;
          ++low_residual_refinement_free_moves;
          if (best_row != old_row) {
            ++low_residual_refinement_row_moves;
          }
          low_residual_refinement_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_refinement_disp_gain_sites += -best_disp_delta;
          if (best_current_net_anchor) {
            ++low_residual_current_net_anchor_moves;
            low_residual_current_net_anchor_hpwl_gain_sites += -best_hpwl_delta;
            low_residual_current_net_anchor_disp_gain_sites += -best_disp_delta;
          }
        } else {
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
        }
      }

      struct LowResidualTargetCorrectionCell
      {
        int idx = -1;
        int target_deviation = 0;
        int displacement = 0;
        double priority = 0.0;
      };
      std::vector<LowResidualTargetCorrectionCell> target_correction_frontier;
      target_correction_frontier.reserve(
          std::min(static_cast<int>(cells.size()),
                   kLowResidualTargetCorrectionFrontierCap));
      auto target_deviation_sites = [&](const LegalmPlaceCell& cell,
                                        const int row,
                                        const int site) {
        if (row < 0 || site < 0) {
          return std::numeric_limits<int>::max() / 4;
        }
        return std::abs(row - cell.desired_row) * row_equiv_sites
               + std::abs(site - cell.desired_site);
      };
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        const LegalmPlaceCell& item = cells[idx];
        const int row = current_rows[idx];
        const int site = current_sites[idx];
        if (row < 0 || site < 0 || item.height_rows != 1
            || item.width_sites <= 0 || low_residual_cell_nets[idx].empty()) {
          continue;
        }
        const int target_deviation = target_deviation_sites(item, row, site);
        if (target_deviation <= 0) {
          continue;
        }
        const int displacement = displacement_sites(item, row, site);
        if (target_deviation <= 1
            && displacement <= max_disp_threshold_sites / 2) {
          continue;
        }
        const double priority
            = 3.0 * static_cast<double>(target_deviation)
              + static_cast<double>(displacement);
        target_correction_frontier.push_back(
            {idx, target_deviation, displacement, priority});
      }
      std::stable_sort(
          target_correction_frontier.begin(),
          target_correction_frontier.end(),
          [](const LowResidualTargetCorrectionCell& lhs,
             const LowResidualTargetCorrectionCell& rhs) {
            if (lhs.priority != rhs.priority) {
              return lhs.priority > rhs.priority;
            }
            if (lhs.target_deviation != rhs.target_deviation) {
              return lhs.target_deviation > rhs.target_deviation;
            }
            return lhs.idx < rhs.idx;
          });
      if (static_cast<int>(target_correction_frontier.size())
          > kLowResidualTargetCorrectionFrontierCap) {
        target_correction_frontier.resize(
            kLowResidualTargetCorrectionFrontierCap);
      }
      low_residual_target_correction_frontier
          = static_cast<int>(target_correction_frontier.size());

      struct LowResidualTargetCorrectionProbe
      {
        int row = -1;
        int site = -1;
        bool nearby = false;
        int target_gain = 0;
        int disp_gain = 0;
        double priority = -std::numeric_limits<double>::infinity();
      };

      auto low_residual_target_move_hpwl_delta = [&](const int move_idx,
                                                     const int row,
                                                     const int site) {
        double hpwl_delta_sites = 0.0;
        if (move_idx < 0
            || move_idx >= static_cast<int>(low_residual_cell_nets.size())) {
          return hpwl_delta_sites;
        }
        const auto [old_center_x, old_center_y]
            = current_cell_center(move_idx, -1, -1);
        const auto [new_center_x, new_center_y]
            = current_cell_center(move_idx, row, site);
        for (const LowResidualIncidentNet& incident :
             low_residual_cell_nets[move_idx]) {
          const LowResidualNetSummary& summary
              = low_residual_net_summaries[incident.net_idx];
          if (!summary.valid) {
            continue;
          }
          int moving_min_x_count = 0;
          int moving_max_x_count = 0;
          int moving_min_y_count = 0;
          int moving_max_y_count = 0;
          for (const auto& offset : incident.pin_offsets) {
            const int64_t old_pin_x = old_center_x + offset.first;
            const int64_t old_pin_y = old_center_y + offset.second;
            moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
            moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
            moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
            moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
          }
          int64_t new_min_x
              = (moving_min_x_count > 0
                 && moving_min_x_count >= summary.min_x_count)
                    ? summary.second_min_x
                    : summary.min_x;
          int64_t new_max_x
              = (moving_max_x_count > 0
                 && moving_max_x_count >= summary.max_x_count)
                    ? summary.second_max_x
                    : summary.max_x;
          int64_t new_min_y
              = (moving_min_y_count > 0
                 && moving_min_y_count >= summary.min_y_count)
                    ? summary.second_min_y
                    : summary.min_y;
          int64_t new_max_y
              = (moving_max_y_count > 0
                 && moving_max_y_count >= summary.max_y_count)
                    ? summary.second_max_y
                    : summary.max_y;
          for (const auto& offset : incident.pin_offsets) {
            const int64_t new_pin_x = new_center_x + offset.first;
            const int64_t new_pin_y = new_center_y + offset.second;
            new_min_x = std::min(new_min_x, new_pin_x);
            new_max_x = std::max(new_max_x, new_pin_x);
            new_min_y = std::min(new_min_y, new_pin_y);
            new_max_y = std::max(new_max_y, new_pin_y);
          }
          const int64_t new_hpwl = (new_max_x - new_min_x)
                                   + (new_max_y - new_min_y);
          hpwl_delta_sites
              += static_cast<double>(new_hpwl - summary.hpwl_dbu)
                 / static_cast<double>(std::max(1, site_width.v));
        }
        return hpwl_delta_sites;
      };

      for (const LowResidualTargetCorrectionCell& frontier_cell :
           target_correction_frontier) {
        const int idx = frontier_cell.idx;
        if (idx < 0 || idx >= static_cast<int>(cells.size())) {
          continue;
        }
        LegalmPlaceCell& item = cells[idx];
        const int old_row = current_rows[idx];
        const int old_site = current_sites[idx];
        if (old_row < 0 || old_site < 0 || item.height_rows != 1
            || item.width_sites <= 0) {
          continue;
        }
        release_slot(
            old_row, old_site, item.width_sites, item.height_rows, item.group);
        const int old_disp = displacement_sites(item, old_row, old_site);
        const double old_disp_cost = displacement_cost(item, old_row, old_site);
        const int old_target_deviation
            = target_deviation_sites(item, old_row, old_site);

        std::array<LowResidualTargetCorrectionProbe,
                   kLowResidualTargetCorrectionProbeCap>
            probes{};
        int probe_count = 0;
        auto add_target_probe = [&](const int row,
                                    const int site,
                                    const bool nearby = false,
                                    const int target_gain_override
                                    = std::numeric_limits<int>::min(),
                                    const int disp_gain_override
                                    = std::numeric_limits<int>::min()) {
          if (row < 0 || row + item.height_rows > row_count || site < 0
              || site + item.width_sites > site_count
              || (row == old_row && site == old_site)) {
            return;
          }
          const int target_gain
              = target_gain_override == std::numeric_limits<int>::min()
                    ? old_target_deviation
                          - target_deviation_sites(item, row, site)
                    : target_gain_override;
          const int disp_gain
              = disp_gain_override == std::numeric_limits<int>::min()
                    ? old_disp - displacement_sites(item, row, site)
                    : disp_gain_override;
          const double priority = 2.0 * static_cast<double>(target_gain)
                                  + 0.45 * static_cast<double>(disp_gain)
                                  + (nearby ? 0.15 : 0.50);
          for (int i = 0; i < probe_count; ++i) {
            if (probes[i].row == row && probes[i].site == site) {
              probes[i].nearby = probes[i].nearby || nearby;
              if (priority > probes[i].priority) {
                probes[i].target_gain = target_gain;
                probes[i].disp_gain = disp_gain;
                probes[i].priority = priority;
              }
              return;
            }
          }
          int insert_at = probe_count;
          for (int i = 0; i < probe_count; ++i) {
            if (priority > probes[i].priority
                || (priority == probes[i].priority
                    && target_gain > probes[i].target_gain)) {
              insert_at = i;
              break;
            }
          }
          if (probe_count
              >= static_cast<int>(kLowResidualTargetCorrectionProbeCap)
              && insert_at
                     >= static_cast<int>(kLowResidualTargetCorrectionProbeCap)) {
            return;
          }
          const int limit = std::min(
              probe_count,
              static_cast<int>(kLowResidualTargetCorrectionProbeCap) - 1);
          for (int i = limit; i > insert_at; --i) {
            probes[i] = probes[i - 1];
          }
          probes[insert_at]
              = {row, site, nearby, target_gain, disp_gain, priority};
          probe_count = std::min(
              probe_count + 1,
              static_cast<int>(kLowResidualTargetCorrectionProbeCap));
          if (nearby) {
            ++low_residual_target_correction_nearby_probes;
          }
        };
        add_target_probe(item.desired_row, item.desired_site);
        add_target_probe(item.original_row, item.original_site);
        auto add_target_interval_probe = [&](const int row,
                                             const int target_site) {
          if (row < 0 || row >= row_count) {
            return;
          }
          const auto& intervals = free_intervals[row];
          auto it = std::lower_bound(
              intervals.begin(),
              intervals.end(),
              target_site,
              [](const Interval& interval, const int target) {
                return interval.second <= target;
              });
          auto add_from_interval = [&](const Interval& interval) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              return;
            }
            const int clamped = std::clamp(target_site,
                                           interval.first,
                                           interval.second - item.width_sites);
            if (displacement_sites(item, row, clamped) > old_disp
                || target_deviation_sites(item, row, clamped)
                       > old_target_deviation) {
              ++low_residual_target_correction_prefilter_rejects;
              return;
            }
            add_target_probe(row, clamped, true);
          };
          if (it != intervals.end()) {
            add_from_interval(*it);
          }
          if (it != intervals.begin()) {
            auto back = it;
            --back;
            add_from_interval(*back);
          }
        };
        auto add_target_swap_probe = [&](const int row, const int site) {
          if (row < 0 || row >= row_count || site < 0
              || site >= site_count) {
            return;
          }
          const int owner
              = low_residual_site_owner[static_cast<size_t>(row)
                                            * static_cast<size_t>(site_count)
                                        + static_cast<size_t>(site)];
          if (owner < 0 || owner == idx
              || owner >= static_cast<int>(cells.size())) {
            return;
          }
          const LegalmPlaceCell& other = cells[owner];
          if (current_rows[owner] != row || current_sites[owner] != site
              || other.height_rows != item.height_rows
              || other.width_sites != item.width_sites
              || other.group != item.group) {
            return;
          }
          const int old_other_disp = displacement_sites(other, row, site);
          const int disp_delta
              = displacement_sites(item, row, site)
                + displacement_sites(other, old_row, old_site) - old_disp
                - old_other_disp;
          const int target_delta
              = target_deviation_sites(item, row, site)
                + target_deviation_sites(other, old_row, old_site)
                - old_target_deviation - target_deviation_sites(other, row, site);
          if (disp_delta > 0 || target_delta > 0) {
            ++low_residual_target_correction_prefilter_rejects;
            return;
          }
          add_target_probe(row, site, true, -target_delta, -disp_delta);
        };
        auto add_target_corridor_probes = [&](const int row,
                                              const int site) {
          add_target_interval_probe(row, site);
          const int width = std::max(1, item.width_sites);
          add_target_swap_probe(row, site - width);
          add_target_swap_probe(row, site + width);
          add_target_swap_probe(row, site - 2 * width);
          add_target_swap_probe(row, site + 2 * width);
        };
        add_target_corridor_probes(item.desired_row, item.desired_site);
        add_target_corridor_probes(item.original_row, item.original_site);

        double best_score_delta = 0.0;
        double best_hpwl_delta = 0.0;
        int best_disp_delta = 0;
        int best_target_delta = 0;
        int best_row = old_row;
        int best_site = old_site;
        int best_swap_idx = -1;
        bool best_is_free_move = false;

        for (int probe_idx = 0; probe_idx < probe_count; ++probe_idx) {
          const int row = probes[probe_idx].row;
          const int site = probes[probe_idx].site;
          ++low_residual_target_correction_candidates;
          if (!row_site_compatible(item, row, site)) {
            ++low_residual_target_correction_static_rejects;
            continue;
          }
          const int owner
              = low_residual_site_owner[static_cast<size_t>(row)
                                            * static_cast<size_t>(site_count)
                                        + static_cast<size_t>(site)];
          const int new_disp = displacement_sites(item, row, site);
          const int new_target_deviation
              = target_deviation_sites(item, row, site);
          if (owner < 0 || owner == idx) {
            if (owner == idx) {
              ++low_residual_target_correction_self_overlap_probes;
            }
            if (!fits_all_rows(
                    row, site, item.width_sites, item.height_rows, item.group)) {
              ++low_residual_target_correction_legal_rejects;
              continue;
            }
            const int disp_delta = new_disp - old_disp;
            const int target_delta
                = new_target_deviation - old_target_deviation;
            if (disp_delta > 0 || target_delta > 0) {
              ++low_residual_target_correction_disp_rejects;
              continue;
            }
            if (low_residual_target_correction_scored
                >= kLowResidualTargetCorrectionExactScoreCap) {
              ++low_residual_target_correction_exact_cap_rejects;
              continue;
            }
            if (probes[probe_idx].nearby) {
              ++low_residual_target_correction_potential_evals;
              const double cheap_hpwl_delta
                  = low_residual_target_move_hpwl_delta(idx, row, site);
              const double cheap_hpwl_gain = std::max(0.0, -cheap_hpwl_delta);
              if (cheap_hpwl_gain
                      < kLowResidualTargetCorrectionWeakGainSites
                  && -target_delta <= 1 && -disp_delta <= 0) {
                ++low_residual_target_correction_potential_rejects;
                continue;
              }
            } else {
              ++low_residual_target_correction_potential_skips;
            }
            const LowResidualDelta hpwl_delta_result
                = low_residual_move_delta(idx, row, site, true);
            ++low_residual_target_correction_scored;
            const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
            if (!hpwl_delta_result.bbox_active
                || hpwl_delta > -kLowResidualExactHpwlMinGainSites) {
              ++low_residual_target_correction_hpwl_rejects;
              continue;
            }
            const double disp_cost_delta
                = displacement_cost(item, row, site) - old_disp_cost;
            const double score_delta
                = hpwl_delta + 0.20 * disp_cost_delta
                  + 0.04 * static_cast<double>(target_delta);
            if (score_delta < best_score_delta - 0.25
                || (std::abs(score_delta - best_score_delta) <= 0.25
                    && (hpwl_delta < best_hpwl_delta
                        || (hpwl_delta == best_hpwl_delta
                            && disp_delta < best_disp_delta)))) {
              best_score_delta = score_delta;
              best_hpwl_delta = hpwl_delta;
              best_disp_delta = disp_delta;
              best_target_delta = target_delta;
              best_row = row;
              best_site = site;
              best_swap_idx = -1;
              best_is_free_move = true;
            }
            continue;
          }

          if (owner == idx || owner >= static_cast<int>(cells.size())) {
            ++low_residual_target_correction_legal_rejects;
            continue;
          }
          const LegalmPlaceCell& other = cells[owner];
          const int other_row = current_rows[owner];
          const int other_site = current_sites[owner];
          if (other_row != row || other_site != site
              || other.height_rows != item.height_rows
              || other.width_sites != item.width_sites
              || other.group != item.group) {
            ++low_residual_target_correction_legal_rejects;
            continue;
          }
          if (!row_site_compatible(other, old_row, old_site)) {
            ++low_residual_target_correction_static_rejects;
            continue;
          }
          const int old_other_disp
              = displacement_sites(other, other_row, other_site);
          const double old_other_disp_cost
              = displacement_cost(other, other_row, other_site);
          const int new_other_disp
              = displacement_sites(other, old_row, old_site);
          const int disp_delta
              = (new_disp + new_other_disp) - (old_disp + old_other_disp);
          const int old_target_sum
              = old_target_deviation
                + target_deviation_sites(other, other_row, other_site);
          const int new_target_sum
              = new_target_deviation
                + target_deviation_sites(other, old_row, old_site);
          const int target_delta = new_target_sum - old_target_sum;
          if (disp_delta > 0 || target_delta > 0) {
            ++low_residual_target_correction_disp_rejects;
            continue;
          }
          if (low_residual_target_correction_scored
              >= kLowResidualTargetCorrectionExactScoreCap) {
            ++low_residual_target_correction_exact_cap_rejects;
            continue;
          }
          if (probes[probe_idx].nearby) {
            ++low_residual_target_correction_potential_evals;
            const double cheap_hpwl_delta
                = low_residual_target_move_hpwl_delta(idx, row, site)
                  + low_residual_target_move_hpwl_delta(
                      owner, old_row, old_site);
            const double cheap_hpwl_gain = std::max(0.0, -cheap_hpwl_delta);
            if (cheap_hpwl_gain < kLowResidualTargetCorrectionWeakGainSites
                && -target_delta <= 1 && -disp_delta <= 0) {
              ++low_residual_target_correction_potential_rejects;
              continue;
            }
          } else {
            ++low_residual_target_correction_potential_skips;
          }
          const LowResidualDelta hpwl_delta_result = low_residual_swap_delta(
              idx, row, site, owner, old_row, old_site, true);
          ++low_residual_target_correction_scored;
          const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
          if (!hpwl_delta_result.bbox_active
              || hpwl_delta > -kLowResidualExactHpwlMinGainSites) {
            ++low_residual_target_correction_hpwl_rejects;
            continue;
          }
          const double disp_cost_delta
              = displacement_cost(item, row, site)
                + displacement_cost(other, old_row, old_site) - old_disp_cost
                - old_other_disp_cost;
          const double score_delta
              = hpwl_delta + 0.20 * disp_cost_delta
                + 0.04 * static_cast<double>(target_delta);
          if (score_delta < best_score_delta - 0.25
              || (std::abs(score_delta - best_score_delta) <= 0.25
                  && (hpwl_delta < best_hpwl_delta
                      || (hpwl_delta == best_hpwl_delta
                          && disp_delta < best_disp_delta)))) {
            best_score_delta = score_delta;
            best_hpwl_delta = hpwl_delta;
            best_disp_delta = disp_delta;
            best_target_delta = target_delta;
            best_row = row;
            best_site = site;
            best_swap_idx = owner;
            best_is_free_move = false;
          }
        }

        if (best_swap_idx >= 0) {
          const int other_idx = best_swap_idx;
          const int other_row = current_rows[other_idx];
          const int other_site = current_sites[other_idx];
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
          current_rows[idx] = best_row;
          current_sites[idx] = best_site;
          current_rows[other_idx] = old_row;
          current_sites[other_idx] = old_site;
          set_low_residual_owner(idx, old_row, old_site, -1);
          set_low_residual_owner(other_idx, other_row, other_site, -1);
          set_low_residual_owner(idx, best_row, best_site, idx);
          set_low_residual_owner(other_idx, old_row, old_site, other_idx);
          low_residual_refresh_nets(idx, other_idx);
          if (stage3_changed[idx] == 0) {
            stage3_changed[idx] = 1;
            ++stage3_changed_cells;
          }
          if (stage3_changed[other_idx] == 0) {
            stage3_changed[other_idx] = 1;
            ++stage3_changed_cells;
          }
          ++low_residual_target_correction_moves;
          ++low_residual_target_correction_swap_moves;
          low_residual_target_correction_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_target_correction_disp_gain_sites += -best_disp_delta;
          low_residual_target_correction_target_gain_sites
              += -best_target_delta;
        } else if (best_is_free_move
                   && (best_row != old_row || best_site != old_site)) {
          reserve_slot(best_row,
                       best_site,
                       item.width_sites,
                       item.height_rows,
                       item.group);
          current_rows[idx] = best_row;
          current_sites[idx] = best_site;
          set_low_residual_owner(idx, old_row, old_site, -1);
          set_low_residual_owner(idx, best_row, best_site, idx);
          low_residual_refresh_nets(idx, -1);
          if (stage3_changed[idx] == 0) {
            stage3_changed[idx] = 1;
            ++stage3_changed_cells;
          }
          ++low_residual_target_correction_moves;
          ++low_residual_target_correction_free_moves;
          low_residual_target_correction_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_target_correction_disp_gain_sites += -best_disp_delta;
          low_residual_target_correction_target_gain_sites
              += -best_target_delta;
        } else {
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
        }
      }

      struct LowResidualTargetReleaseCell
      {
        int idx = -1;
        int target_deviation = 0;
        int displacement = 0;
        double priority = 0.0;
      };
      std::vector<LowResidualTargetReleaseCell> target_release_frontier;
      target_release_frontier.reserve(
          std::min(static_cast<int>(cells.size()),
                   kLowResidualTargetReleaseFrontierCap));
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        const LegalmPlaceCell& item = cells[idx];
        const int row = current_rows[idx];
        const int site = current_sites[idx];
        if (row < 0 || site < 0 || item.height_rows != 1
            || item.width_sites <= 0 || low_residual_cell_nets[idx].empty()) {
          continue;
        }
        const int target_deviation = target_deviation_sites(item, row, site);
        if (target_deviation <= 1) {
          continue;
        }
        const int displacement = displacement_sites(item, row, site);
        const bool bbox_frontier = low_residual_cell_has_bbox_term(idx);
        if (!bbox_frontier && displacement <= max_disp_threshold_sites / 2) {
          continue;
        }
        const double priority
            = 4.0 * static_cast<double>(target_deviation)
              + static_cast<double>(displacement)
              + (bbox_frontier ? 8.0 : 0.0);
        target_release_frontier.push_back(
            {idx, target_deviation, displacement, priority});
      }
      std::stable_sort(
          target_release_frontier.begin(),
          target_release_frontier.end(),
          [](const LowResidualTargetReleaseCell& lhs,
             const LowResidualTargetReleaseCell& rhs) {
            if (lhs.priority != rhs.priority) {
              return lhs.priority > rhs.priority;
            }
            if (lhs.target_deviation != rhs.target_deviation) {
              return lhs.target_deviation > rhs.target_deviation;
            }
            return lhs.idx < rhs.idx;
          });
      if (static_cast<int>(target_release_frontier.size())
          > kLowResidualTargetReleaseFrontierCap) {
        target_release_frontier.resize(kLowResidualTargetReleaseFrontierCap);
      }
      low_residual_target_release_frontier
          = static_cast<int>(target_release_frontier.size());

      struct LowResidualTargetReleaseOwnerProbe
      {
        int row = -1;
        int site = -1;
        int disp_delta = 0;
        int target_delta = 0;
        double disp_cost_delta = 0.0;
        double priority = -std::numeric_limits<double>::infinity();
      };

      for (const LowResidualTargetReleaseCell& frontier_cell :
           target_release_frontier) {
        const int idx = frontier_cell.idx;
        if (idx < 0 || idx >= static_cast<int>(cells.size())) {
          continue;
        }
        LegalmPlaceCell& item = cells[idx];
        const int old_row = current_rows[idx];
        const int old_site = current_sites[idx];
        const int target_row = item.desired_row;
        const int target_site = item.desired_site;
        if (old_row < 0 || old_site < 0 || item.height_rows != 1
            || item.width_sites <= 0 || target_row < 0
            || target_row >= row_count || target_site < 0
            || target_site + item.width_sites > site_count
            || (target_row == old_row && target_site == old_site)) {
          continue;
        }
        if (!row_site_compatible(item, target_row, target_site)) {
          ++low_residual_target_release_static_rejects;
          continue;
        }

        int blocker = -1;
        bool multi_blocker = false;
        for (int dx = 0; dx < item.width_sites; ++dx) {
          const int owner
              = low_residual_site_owner[static_cast<size_t>(target_row)
                                            * static_cast<size_t>(site_count)
                                        + static_cast<size_t>(target_site + dx)];
          if (owner < 0 || owner == idx) {
            continue;
          }
          if (blocker < 0) {
            blocker = owner;
          } else if (blocker != owner) {
            multi_blocker = true;
            break;
          }
        }
        if (multi_blocker) {
          ++low_residual_target_release_multi_blocker_rejects;
          continue;
        }
        if (blocker < 0 || blocker >= static_cast<int>(cells.size())) {
          continue;
        }
        LegalmPlaceCell& other = cells[blocker];
        const int other_row = current_rows[blocker];
        const int other_site = current_sites[blocker];
        if (other_row < 0 || other_site < 0 || other.height_rows != 1
            || other.width_sites <= 0 || other.group != item.group) {
          ++low_residual_target_release_static_rejects;
          continue;
        }
        ++low_residual_target_release_blockers;

        release_slot(
            old_row, old_site, item.width_sites, item.height_rows, item.group);
        release_slot(other_row,
                     other_site,
                     other.width_sites,
                     other.height_rows,
                     other.group);
        auto restore_target_release_slots = [&]() {
          reserve_slot(other_row,
                       other_site,
                       other.width_sites,
                       other.height_rows,
                       other.group);
          reserve_slot(old_row,
                       old_site,
                       item.width_sites,
                       item.height_rows,
                       item.group);
        };

        if (!fits_all_rows(target_row,
                           target_site,
                           item.width_sites,
                           item.height_rows,
                           item.group)) {
          ++low_residual_target_release_legal_rejects;
          restore_target_release_slots();
          continue;
        }

        const int old_item_disp = displacement_sites(item, old_row, old_site);
        const int new_item_disp
            = displacement_sites(item, target_row, target_site);
        const double old_item_disp_cost
            = displacement_cost(item, old_row, old_site);
        const double new_item_disp_cost
            = displacement_cost(item, target_row, target_site);
        const int old_other_disp
            = displacement_sites(other, other_row, other_site);
        const double old_other_disp_cost
            = displacement_cost(other, other_row, other_site);
        const int old_target_sum
            = target_deviation_sites(item, old_row, old_site)
              + target_deviation_sites(other, other_row, other_site);
        const int new_item_target
            = target_deviation_sites(item, target_row, target_site);

        std::array<LowResidualTargetReleaseOwnerProbe,
                   kLowResidualTargetReleaseOwnerProbeCap>
            owner_probes{};
        int owner_probe_count = 0;
        auto overlaps_target_slot = [&](const int row,
                                        const int site,
                                        const int width) {
          if (row != target_row) {
            return false;
          }
          return site < target_site + item.width_sites
                 && target_site < site + width;
        };
        auto add_owner_probe = [&](const int row, const int site) {
          if (row < 0 || row + other.height_rows > row_count || site < 0
              || site + other.width_sites > site_count
              || overlaps_target_slot(row, site, other.width_sites)) {
            return;
          }
          if (!row_site_compatible(other, row, site)) {
            ++low_residual_target_release_static_rejects;
            return;
          }
          if (!fits_all_rows(row,
                             site,
                             other.width_sites,
                             other.height_rows,
                             other.group)) {
            ++low_residual_target_release_legal_rejects;
            return;
          }
          const int new_other_disp = displacement_sites(other, row, site);
          const int disp_delta = (new_item_disp + new_other_disp)
                                 - (old_item_disp + old_other_disp);
          const int target_delta
              = new_item_target + target_deviation_sites(other, row, site)
                - old_target_sum;
          const double disp_cost_delta
              = new_item_disp_cost + displacement_cost(other, row, site)
                - old_item_disp_cost - old_other_disp_cost;
          if (disp_delta > 0 || target_delta > 0) {
            ++low_residual_target_release_disp_rejects;
            return;
          }
          const double priority
              = -3.0 * static_cast<double>(target_delta)
                - static_cast<double>(disp_delta)
                - 0.10 * static_cast<double>(new_other_disp);
          for (int i = 0; i < owner_probe_count; ++i) {
            if (owner_probes[i].row == row && owner_probes[i].site == site) {
              if (priority > owner_probes[i].priority) {
                owner_probes[i] = {row,
                                   site,
                                   disp_delta,
                                   target_delta,
                                   disp_cost_delta,
                                   priority};
              }
              return;
            }
          }
          int insert_at = owner_probe_count;
          for (int i = 0; i < owner_probe_count; ++i) {
            if (priority > owner_probes[i].priority) {
              insert_at = i;
              break;
            }
          }
          if (owner_probe_count
                  >= kLowResidualTargetReleaseOwnerProbeCap
              && insert_at >= kLowResidualTargetReleaseOwnerProbeCap) {
            return;
          }
          const int limit = std::min(
              owner_probe_count,
              kLowResidualTargetReleaseOwnerProbeCap - 1);
          for (int i = limit; i > insert_at; --i) {
            owner_probes[i] = owner_probes[i - 1];
          }
          owner_probes[insert_at] = {row,
                                     site,
                                     disp_delta,
                                     target_delta,
                                     disp_cost_delta,
                                     priority};
          owner_probe_count = std::min(
              owner_probe_count + 1,
              kLowResidualTargetReleaseOwnerProbeCap);
        };
        auto add_owner_interval_probe = [&](const int row,
                                            const int target_site_hint) {
          if (row < 0 || row >= row_count) {
            return;
          }
          const auto& intervals = free_intervals[row];
          auto it = std::lower_bound(
              intervals.begin(),
              intervals.end(),
              target_site_hint,
              [](const Interval& interval, const int target) {
                return interval.second <= target;
              });
          auto add_from_interval = [&](const Interval& interval) {
            if (interval.group != other.group
                || interval.second - interval.first < other.width_sites) {
              return;
            }
            const int clamped
                = std::clamp(target_site_hint,
                             interval.first,
                             interval.second - other.width_sites);
            add_owner_probe(row, clamped);
          };
          int forward_samples = 0;
          for (auto fwd = it; fwd != intervals.end()
                            && forward_samples < 2;
               ++fwd, ++forward_samples) {
            add_from_interval(*fwd);
          }
          auto back = it;
          int backward_samples = 0;
          while (back != intervals.begin() && backward_samples < 2) {
            --back;
            add_from_interval(*back);
            ++backward_samples;
          }
        };

        add_owner_interval_probe(old_row, old_site);
        add_owner_interval_probe(other.desired_row, other.desired_site);
        add_owner_interval_probe(other.original_row, other.original_site);
        add_owner_interval_probe(
            other.desired_row,
            (other.desired_site + other.original_site) / 2);
        if (old_row >= 0 && item.width_sites >= other.width_sites) {
          const int old_slot_max_site
              = old_site + item.width_sites - other.width_sites;
          auto add_old_slot_direct_probe = [&](const int site) {
            add_owner_probe(old_row,
                            std::clamp(site, old_site, old_slot_max_site));
            ++low_residual_target_release_old_slot_probes;
          };
          add_old_slot_direct_probe(other.desired_site);
          add_old_slot_direct_probe(other.original_site);
          add_old_slot_direct_probe(old_slot_max_site);
        }
        add_owner_interval_probe(other_row, other_site);
        ++low_residual_target_release_neighbor_probes;

        double best_score_delta = 0.0;
        double best_hpwl_delta = 0.0;
        int best_disp_delta = 0;
        int best_target_delta = 0;
        int best_owner_row = other_row;
        int best_owner_site = other_site;
        bool best_found = false;
        std::array<LowResidualChainMove, kLowResidualChainMaxCells>
            best_moves{};

        for (int probe_idx = 0; probe_idx < owner_probe_count; ++probe_idx) {
          ++low_residual_target_release_candidates;
          if (low_residual_target_release_scored
              >= kLowResidualTargetReleaseExactScoreCap) {
            ++low_residual_target_release_exact_cap_rejects;
            continue;
          }
          std::array<LowResidualChainMove, kLowResidualChainMaxCells> moves{};
          moves[0] = {idx, target_row, target_site};
          moves[1] = {blocker, owner_probes[probe_idx].row,
                      owner_probes[probe_idx].site};
          ++low_residual_target_release_scored;
          const LowResidualDelta hpwl_delta_result
              = low_residual_target_release_delta(moves, 2);
          const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
          if (!hpwl_delta_result.bbox_active
              || hpwl_delta > -kLowResidualExactHpwlMinGainSites) {
            ++low_residual_target_release_hpwl_rejects;
            continue;
          }
          const int old_max_disp = std::max(old_item_disp, old_other_disp);
          const int new_max_disp = std::max(
              new_item_disp,
              displacement_sites(other,
                                 owner_probes[probe_idx].row,
                                 owner_probes[probe_idx].site));
          if (new_max_disp > old_max_disp + displacement_slack
              && hpwl_delta
                     > -3.0 * static_cast<double>(
                                  std::max(1, new_max_disp - old_max_disp))) {
            ++low_residual_target_release_disp_rejects;
            continue;
          }
          const double score_delta
              = owner_probes[probe_idx].disp_cost_delta
                + kLowResidualTargetReleaseHpwlWeight * hpwl_delta
                + 0.04
                      * static_cast<double>(
                          owner_probes[probe_idx].target_delta);
          if (score_delta < best_score_delta - 0.25
              || (std::abs(score_delta - best_score_delta) <= 0.25
                  && (hpwl_delta < best_hpwl_delta
                      || (hpwl_delta == best_hpwl_delta
                          && owner_probes[probe_idx].disp_delta
                                 < best_disp_delta)))) {
            best_score_delta = score_delta;
            best_hpwl_delta = hpwl_delta;
            best_disp_delta = owner_probes[probe_idx].disp_delta;
            best_target_delta = owner_probes[probe_idx].target_delta;
            best_owner_row = owner_probes[probe_idx].row;
            best_owner_site = owner_probes[probe_idx].site;
            best_found = true;
            best_moves = moves;
          }
        }

        if (best_found) {
          reserve_slot(target_row,
                       target_site,
                       item.width_sites,
                       item.height_rows,
                       item.group);
          reserve_slot(best_owner_row,
                       best_owner_site,
                       other.width_sites,
                       other.height_rows,
                       other.group);
          current_rows[idx] = target_row;
          current_sites[idx] = target_site;
          current_rows[blocker] = best_owner_row;
          current_sites[blocker] = best_owner_site;
          set_low_residual_owner(idx, old_row, old_site, -1);
          set_low_residual_owner(blocker, other_row, other_site, -1);
          set_low_residual_owner(idx, target_row, target_site, idx);
          set_low_residual_owner(blocker, best_owner_row, best_owner_site, blocker);
          low_residual_refresh_chain_nets(best_moves, 2);
          if (stage3_changed[idx] == 0) {
            stage3_changed[idx] = 1;
            ++stage3_changed_cells;
          }
          if (stage3_changed[blocker] == 0) {
            stage3_changed[blocker] = 1;
            ++stage3_changed_cells;
          }
          ++low_residual_target_release_moves;
          low_residual_target_release_hpwl_gain_sites += -best_hpwl_delta;
          low_residual_target_release_disp_gain_sites += -best_disp_delta;
          low_residual_target_release_target_gain_sites += -best_target_delta;
        } else {
          restore_target_release_slots();
        }
      }
    }

    if (!low_residual_policy
        && row_escape_count
               > std::max(512, static_cast<int>(cells.size() / 4))) {
      high_pressure_tail_refinement_enabled = 1;
      const int tail_frontier_threshold
          = std::max(max_disp_threshold_sites, 2 * row_equiv_sites);
      const int tail_displacement_slack
          = std::max(1, max_disp_threshold_sites / 6);

      struct HighPressureTailNetSummary
      {
        const Edge* edge = nullptr;
        bool valid = false;
        int64_t min_x = 0;
        int64_t max_x = 0;
        int64_t min_y = 0;
        int64_t max_y = 0;
        int min_x_count = 0;
        int max_x_count = 0;
        int min_y_count = 0;
        int max_y_count = 0;
        int64_t hpwl_dbu = 0;
      };
      struct HighPressureTailIncidentNet
      {
        int net_idx = -1;
        std::vector<std::pair<int, int>> pin_offsets;
      };
      struct HighPressureTailMove
      {
        int idx = -1;
        int row = -1;
        int site = -1;
      };
      struct HighPressureTailDelta
      {
        double hpwl_delta_sites = 0.0;
        bool bbox_active = false;
      };

      std::vector<HighPressureTailNetSummary> tail_net_summaries;
      std::vector<std::vector<HighPressureTailIncidentNet>> tail_cell_nets(
          cells.size());
      std::unordered_map<const Edge*, int> tail_net_index;
      tail_net_index.reserve(
          static_cast<size_t>(std::max<int64_t>(1, total_hpwl_proxy_terms)));

      auto tail_cell_idx = [&](const Node* node) {
        if (node == nullptr) {
          return -1;
        }
        const int id = node->getId();
        if (id >= 0 && id < static_cast<int>(cell_index_by_node_id.size())) {
          return cell_index_by_node_id[id];
        }
        return -1;
      };
      auto tail_net_for_edge = [&](const Edge* edge) {
        const auto found = tail_net_index.find(edge);
        if (found != tail_net_index.end()) {
          return found->second;
        }
        const int net_idx = static_cast<int>(tail_net_summaries.size());
        tail_net_index.emplace(edge, net_idx);
        HighPressureTailNetSummary summary;
        summary.edge = edge;
        tail_net_summaries.push_back(summary);
        return net_idx;
      };
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        std::vector<HighPressureTailIncidentNet>& incidents
            = tail_cell_nets[idx];
        for (const Pin* cell_pin : cells[idx].cell->getPins()) {
          if (cell_pin == nullptr || cell_pin->getEdge() == nullptr) {
            continue;
          }
          const Edge* edge = cell_pin->getEdge();
          if (edge->getNumPins() <= 1
              || edge->getNumPins() >= kHighPressureTailNetPinLimit) {
            continue;
          }
          const int net_idx = tail_net_for_edge(edge);
          auto incident = std::find_if(
              incidents.begin(),
              incidents.end(),
              [&](const HighPressureTailIncidentNet& item) {
                return item.net_idx == net_idx;
              });
          if (incident == incidents.end()) {
            incidents.push_back({net_idx, {}});
            incident = std::prev(incidents.end());
          }
          incident->pin_offsets.push_back(
              {cell_pin->getOffsetX().v, cell_pin->getOffsetY().v});
        }
      }

      auto tail_pin_xy =
          [&](const Pin* pin,
              const std::array<HighPressureTailMove,
                               kHighPressureTailChainMaxCells>& moves,
              const int move_count) {
            const Node* node = pin != nullptr ? pin->getNode() : nullptr;
            const int idx = tail_cell_idx(node);
            std::pair<int64_t, int64_t> center{0, 0};
            if (idx >= 0) {
              bool overridden = false;
              for (int i = 0; i < move_count; ++i) {
                if (moves[i].idx == idx) {
                  center = current_cell_center(idx, moves[i].row, moves[i].site);
                  overridden = true;
                  break;
                }
              }
              if (!overridden) {
                if (current_rows[idx] >= 0 && current_sites[idx] >= 0) {
                  center = current_cell_center(idx, -1, -1);
                } else {
                  center = {node->getCenterX().v, node->getCenterY().v};
                }
              }
            } else if (node != nullptr) {
              center = {node->getCenterX().v, node->getCenterY().v};
            }
            return std::pair<int64_t, int64_t>{
                center.first + (pin != nullptr ? pin->getOffsetX().v : 0),
                center.second + (pin != nullptr ? pin->getOffsetY().v : 0)};
          };

      auto tail_recompute_net = [&](const int net_idx) {
        if (net_idx < 0
            || net_idx >= static_cast<int>(tail_net_summaries.size())) {
          return;
        }
        HighPressureTailNetSummary& summary = tail_net_summaries[net_idx];
        summary.valid = false;
        summary.min_x = std::numeric_limits<int64_t>::max();
        summary.max_x = std::numeric_limits<int64_t>::min();
        summary.min_y = std::numeric_limits<int64_t>::max();
        summary.max_y = std::numeric_limits<int64_t>::min();
        summary.min_x_count = 0;
        summary.max_x_count = 0;
        summary.min_y_count = 0;
        summary.max_y_count = 0;
        summary.hpwl_dbu = 0;
        if (summary.edge == nullptr) {
          return;
        }
        int pin_count = 0;
        std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
            no_moves{};
        for (const Pin* pin : summary.edge->getPins()) {
          if (pin == nullptr || pin->getNode() == nullptr) {
            continue;
          }
          const auto [pin_x, pin_y] = tail_pin_xy(pin, no_moves, 0);
          if (pin_count == 0 || pin_x < summary.min_x) {
            summary.min_x = pin_x;
            summary.min_x_count = 1;
          } else if (pin_x == summary.min_x) {
            ++summary.min_x_count;
          }
          if (pin_count == 0 || pin_x > summary.max_x) {
            summary.max_x = pin_x;
            summary.max_x_count = 1;
          } else if (pin_x == summary.max_x) {
            ++summary.max_x_count;
          }
          if (pin_count == 0 || pin_y < summary.min_y) {
            summary.min_y = pin_y;
            summary.min_y_count = 1;
          } else if (pin_y == summary.min_y) {
            ++summary.min_y_count;
          }
          if (pin_count == 0 || pin_y > summary.max_y) {
            summary.max_y = pin_y;
            summary.max_y_count = 1;
          } else if (pin_y == summary.max_y) {
            ++summary.max_y_count;
          }
          ++pin_count;
        }
        if (pin_count > 1) {
          summary.valid = true;
          summary.hpwl_dbu = (summary.max_x - summary.min_x)
                             + (summary.max_y - summary.min_y);
        }
      };
      for (int net_idx = 0;
           net_idx < static_cast<int>(tail_net_summaries.size());
           ++net_idx) {
        tail_recompute_net(net_idx);
      }

      auto tail_full_net_hpwl =
          [&](const int net_idx,
              const std::array<HighPressureTailMove,
                               kHighPressureTailChainMaxCells>& moves,
              const int move_count) {
            const HighPressureTailNetSummary& summary
                = tail_net_summaries[net_idx];
            int64_t min_x = std::numeric_limits<int64_t>::max();
            int64_t max_x = std::numeric_limits<int64_t>::min();
            int64_t min_y = std::numeric_limits<int64_t>::max();
            int64_t max_y = std::numeric_limits<int64_t>::min();
            int pin_count = 0;
            for (const Pin* pin : summary.edge->getPins()) {
              if (pin == nullptr || pin->getNode() == nullptr) {
                continue;
              }
              const auto [pin_x, pin_y] = tail_pin_xy(pin, moves, move_count);
              min_x = std::min(min_x, pin_x);
              max_x = std::max(max_x, pin_x);
              min_y = std::min(min_y, pin_y);
              max_y = std::max(max_y, pin_y);
              ++pin_count;
            }
            if (pin_count <= 1) {
              return int64_t{0};
            }
            return (max_x - min_x) + (max_y - min_y);
          };

      bool high_pressure_endpoint_delta_context = false;
      bool high_pressure_topmax_delta_context = false;
      auto tail_delta =
          [&](const std::array<HighPressureTailMove,
                               kHighPressureTailChainMaxCells>& moves,
              const int move_count,
              const bool count_runtime) {
            HighPressureTailDelta result;
            if (count_runtime) {
              ++high_pressure_tail_fast_delta_calls;
              if (high_pressure_endpoint_delta_context) {
                ++high_pressure_endpoint_delta_calls;
              }
              if (high_pressure_topmax_delta_context) {
                ++high_pressure_topmax_delta_calls;
              }
            }
            std::vector<int> touched_nets;
            for (int i = 0; i < move_count; ++i) {
              const int move_idx = moves[i].idx;
              if (move_idx < 0
                  || move_idx >= static_cast<int>(tail_cell_nets.size())) {
                continue;
              }
              for (const HighPressureTailIncidentNet& incident :
                   tail_cell_nets[move_idx]) {
                if (std::find(touched_nets.begin(),
                              touched_nets.end(),
                              incident.net_idx)
                    == touched_nets.end()) {
                  touched_nets.push_back(incident.net_idx);
                }
              }
            }

            for (const int net_idx : touched_nets) {
              const HighPressureTailNetSummary& summary
                  = tail_net_summaries[net_idx];
              if (!summary.valid) {
                continue;
              }
              int moving_min_x_count = 0;
              int moving_max_x_count = 0;
              int moving_min_y_count = 0;
              int moving_max_y_count = 0;
              bool insertion_changes_bbox = false;
              for (const Pin* pin : summary.edge->getPins()) {
                if (pin == nullptr || pin->getNode() == nullptr) {
                  continue;
                }
                const int pin_cell_idx = tail_cell_idx(pin->getNode());
                bool moving_pin = false;
                for (int i = 0; i < move_count; ++i) {
                  if (moves[i].idx == pin_cell_idx) {
                    moving_pin = true;
                    break;
                  }
                }
                if (!moving_pin) {
                  continue;
                }
                std::array<HighPressureTailMove,
                           kHighPressureTailChainMaxCells>
                    no_moves{};
                const auto [old_pin_x, old_pin_y]
                    = tail_pin_xy(pin, no_moves, 0);
                const auto [new_pin_x, new_pin_y]
                    = tail_pin_xy(pin, moves, move_count);
                moving_min_x_count += old_pin_x == summary.min_x ? 1 : 0;
                moving_max_x_count += old_pin_x == summary.max_x ? 1 : 0;
                moving_min_y_count += old_pin_y == summary.min_y ? 1 : 0;
                moving_max_y_count += old_pin_y == summary.max_y ? 1 : 0;
                insertion_changes_bbox
                    = insertion_changes_bbox || new_pin_x < summary.min_x
                      || new_pin_x > summary.max_x || new_pin_y < summary.min_y
                      || new_pin_y > summary.max_y;
              }
              const bool removal_changes_bbox
                  = (moving_min_x_count > 0
                     && moving_min_x_count >= summary.min_x_count)
                    || (moving_max_x_count > 0
                        && moving_max_x_count >= summary.max_x_count)
                    || (moving_min_y_count > 0
                        && moving_min_y_count >= summary.min_y_count)
                    || (moving_max_y_count > 0
                        && moving_max_y_count >= summary.max_y_count);
              if (!removal_changes_bbox && !insertion_changes_bbox) {
                continue;
              }
              result.bbox_active = true;
              if (count_runtime) {
                ++high_pressure_tail_full_net_scans;
                if (high_pressure_endpoint_delta_context) {
                  ++high_pressure_endpoint_full_net_scans;
                }
                if (high_pressure_topmax_delta_context) {
                  ++high_pressure_topmax_full_net_scans;
                }
              }
              const int64_t new_hpwl
                  = tail_full_net_hpwl(net_idx, moves, move_count);
              result.hpwl_delta_sites
                  += static_cast<double>(new_hpwl - summary.hpwl_dbu)
                     / static_cast<double>(std::max(1, site_width.v));
            }
            if (count_runtime && !result.bbox_active) {
              ++high_pressure_tail_exact_calls_avoided;
            }
            return result;
          };

      auto tail_refresh_nets =
          [&](const std::array<HighPressureTailMove,
                               kHighPressureTailChainMaxCells>& moves,
              const int move_count) {
            std::vector<int> touched_nets;
            auto add_touched_net = [&](const int net_idx) {
              if (std::find(touched_nets.begin(),
                            touched_nets.end(),
                            net_idx)
                  == touched_nets.end()) {
                touched_nets.push_back(net_idx);
              }
            };
            for (int i = 0; i < move_count; ++i) {
              const int move_idx = moves[i].idx;
              if (move_idx < 0
                  || move_idx >= static_cast<int>(tail_cell_nets.size())) {
                continue;
              }
              for (const HighPressureTailIncidentNet& incident :
                   tail_cell_nets[move_idx]) {
                add_touched_net(incident.net_idx);
              }
            }
            for (const int net_idx : touched_nets) {
              tail_recompute_net(net_idx);
            }
          };

      std::vector<int> tail_site_owner(
          static_cast<size_t>(row_count) * static_cast<size_t>(site_count),
          -1);
      auto set_tail_owner = [&](const int idx,
                                const int row,
                                const int site,
                                const int owner) {
        if (idx < 0 || row < 0 || site < 0) {
          return;
        }
        const LegalmPlaceCell& owner_item = cells[idx];
        for (int dy = 0; dy < owner_item.height_rows; ++dy) {
          const int y = row + dy;
          if (y < 0 || y >= row_count) {
            continue;
          }
          for (int dx = 0; dx < owner_item.width_sites; ++dx) {
            const int x = site + dx;
            if (x < 0 || x >= site_count) {
              continue;
            }
            tail_site_owner[static_cast<size_t>(y)
                                * static_cast<size_t>(site_count)
                            + static_cast<size_t>(x)]
                = owner;
          }
        }
      };
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        set_tail_owner(idx, current_rows[idx], current_sites[idx], idx);
      }

      struct HighPressureTailFrontierCell
      {
        int idx = -1;
        int displacement = 0;
        double priority = 0.0;
      };
      std::vector<HighPressureTailFrontierCell> tail_frontier;
      tail_frontier.reserve(
          std::min(static_cast<int>(cells.size()),
                   kHighPressureTailFrontierCap));
      for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
        const int row = current_rows[idx];
        const int site = current_sites[idx];
        if (row < 0 || site < 0 || row == cells[idx].desired_row
            || tail_cell_nets[idx].empty()) {
          continue;
        }
        const int disp = displacement_sites(cells[idx], row, site);
        if (disp <= tail_frontier_threshold) {
          continue;
        }
        const double cheap_hpwl_penalty
            = std::max(0.0, hpwl_delta_sites(cells[idx], row, site));
        const double priority
            = static_cast<double>(disp)
              + 2.0 * static_cast<double>(
                          std::max(0, disp - 2 * max_disp_threshold_sites))
              + cheap_hpwl_penalty;
        tail_frontier.push_back({idx, disp, priority});
      }
      high_pressure_tail_frontier_before_cap
          = static_cast<int>(tail_frontier.size());
      std::stable_sort(tail_frontier.begin(),
                       tail_frontier.end(),
                       [](const HighPressureTailFrontierCell& lhs,
                          const HighPressureTailFrontierCell& rhs) {
                         if (lhs.priority != rhs.priority) {
                           return lhs.priority > rhs.priority;
                         }
                         if (lhs.displacement != rhs.displacement) {
                           return lhs.displacement > rhs.displacement;
                         }
                         return lhs.idx < rhs.idx;
                       });
      if (static_cast<int>(tail_frontier.size())
          > kHighPressureTailFrontierCap) {
        tail_frontier.resize(kHighPressureTailFrontierCap);
      }
      high_pressure_tail_frontier = static_cast<int>(tail_frontier.size());

      auto high_pressure_tail_bin = [&](const int disp) {
        if (disp > 2 * max_disp_threshold_sites) {
          return 2;
        }
        if (disp > max_disp_threshold_sites) {
          return 1;
        }
        return 0;
      };
      auto endpoint_p99 = [](std::vector<int> values) {
        if (values.empty()) {
          return 0;
        }
        std::sort(values.begin(), values.end());
        const size_t index = std::min(
            values.size() - 1,
            static_cast<size_t>(
                std::llround(0.99 * static_cast<double>(values.size() - 1))));
        return values[index];
      };
      std::vector<unsigned char> endpoint_tail_reservoir(cells.size(), 0);
      std::vector<int> endpoint_tail_indices;
      if (!tail_frontier.empty()) {
        std::vector<HighPressureTailFrontierCell> endpoint_candidates
            = tail_frontier;
        std::stable_sort(endpoint_candidates.begin(),
                         endpoint_candidates.end(),
                         [](const HighPressureTailFrontierCell& lhs,
                            const HighPressureTailFrontierCell& rhs) {
                           if (lhs.displacement != rhs.displacement) {
                             return lhs.displacement > rhs.displacement;
                           }
                           if (lhs.priority != rhs.priority) {
                             return lhs.priority > rhs.priority;
                           }
                           return lhs.idx < rhs.idx;
                         });
        for (const HighPressureTailFrontierCell& candidate :
             endpoint_candidates) {
          if (candidate.displacement <= 2 * max_disp_threshold_sites
              || endpoint_tail_indices.size()
                     >= static_cast<size_t>(kHighPressureEndpointReservoirCap)) {
            break;
          }
          endpoint_tail_reservoir[candidate.idx] = 1;
          endpoint_tail_indices.push_back(candidate.idx);
        }
      }
      high_pressure_endpoint_reservoir_size
          = static_cast<int>(endpoint_tail_indices.size());
      high_pressure_endpoint_owner_count = high_pressure_endpoint_reservoir_size;
      std::vector<int> endpoint_before_disps;
      endpoint_before_disps.reserve(endpoint_tail_indices.size());
      for (const int endpoint_idx : endpoint_tail_indices) {
        const int disp = displacement_sites(cells[endpoint_idx],
                                            current_rows[endpoint_idx],
                                            current_sites[endpoint_idx]);
        endpoint_before_disps.push_back(disp);
        high_pressure_endpoint_before_max_disp
            = std::max(high_pressure_endpoint_before_max_disp, disp);
        ++high_pressure_endpoint_before_tail_bins[high_pressure_tail_bin(disp)];
      }
      high_pressure_endpoint_before_p99_disp
          = endpoint_p99(endpoint_before_disps);

      std::vector<unsigned char> topmax_tail_reservoir(cells.size(), 0);
      std::vector<unsigned char> actual_topmax_tail_reservoir(cells.size(), 0);
      std::vector<int> topmax_tail_indices;
      if (high_pressure_endpoint_before_max_disp > 0) {
        for (const int endpoint_idx : endpoint_tail_indices) {
          const int disp = displacement_sites(cells[endpoint_idx],
                                              current_rows[endpoint_idx],
                                              current_sites[endpoint_idx]);
          if (disp == high_pressure_endpoint_before_max_disp) {
            ++high_pressure_topmax_actual_owner_count;
            actual_topmax_tail_reservoir[endpoint_idx] = 1;
          }
          if (topmax_tail_indices.size()
              >= static_cast<size_t>(kHighPressureTopMaxOwnerCap)) {
            continue;
          }
          topmax_tail_reservoir[endpoint_idx] = 1;
          topmax_tail_indices.push_back(endpoint_idx);
          high_pressure_topmax_before_max_disp
              = std::max(high_pressure_topmax_before_max_disp, disp);
          ++high_pressure_topmax_before_tail_bins[high_pressure_tail_bin(disp)];
        }
      }
      high_pressure_topmax_owner_count
          = static_cast<int>(topmax_tail_indices.size());
      std::vector<int> topmax_before_disps;
      topmax_before_disps.reserve(topmax_tail_indices.size());
      for (const int topmax_idx : topmax_tail_indices) {
        topmax_before_disps.push_back(
            displacement_sites(cells[topmax_idx],
                               current_rows[topmax_idx],
                               current_sites[topmax_idx]));
      }
      high_pressure_topmax_before_p99_disp
          = endpoint_p99(topmax_before_disps);

      enum HighPressureTailMoveKind
      {
        kTailMoveNone = 0,
        kTailMoveInterval = 1,
        kTailMoveSwap = 2,
        kTailMoveChain = 3,
        kTailMoveGapChain = 4,
        kTailMoveTopMaxRelease = 5
      };

      for (const HighPressureTailFrontierCell& frontier_cell :
           tail_frontier) {
        const int idx = frontier_cell.idx;
        LegalmPlaceCell& item = cells[idx];
        const bool endpoint_tail_owner
            = idx >= 0 && idx < static_cast<int>(endpoint_tail_reservoir.size())
              && endpoint_tail_reservoir[idx] != 0;
        const bool topmax_tail_owner
            = idx >= 0 && idx < static_cast<int>(topmax_tail_reservoir.size())
              && topmax_tail_reservoir[idx] != 0;
        const bool actual_topmax_tail_owner
            = idx >= 0
              && idx < static_cast<int>(actual_topmax_tail_reservoir.size())
              && actual_topmax_tail_reservoir[idx] != 0;
        const int old_row = current_rows[idx];
        const int old_site = current_sites[idx];
        if (old_row < 0 || old_site < 0) {
          continue;
        }
        ++high_pressure_tail_attempted;
        release_slot(
            old_row, old_site, item.width_sites, item.height_rows, item.group);
        set_tail_owner(idx, old_row, old_site, -1);

        const int old_disp = displacement_sites(item, old_row, old_site);
        double best_score_delta = 0.0;
        double best_hpwl_delta = 0.0;
        int best_disp_delta = 0;
        int best_old_tail_bin = 0;
        int best_new_tail_bin = 0;
        int best_kind = kTailMoveNone;
        bool best_from_relief_anchor = false;
        bool best_from_endpoint_hpwl_site_anchor = false;
        bool best_from_endpoint_hpwl_credit = false;
        bool best_from_endpoint_hpwl_row_anchor = false;
        int best_reserve_row = -1;
        int best_reserve_site = -1;
        int best_reserve_width = item.width_sites;
        int best_reserve_height = item.height_rows;
        Group* best_reserve_group = item.group;
        std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
            best_moves{};
        int best_move_count = 0;
        std::vector<int64_t> seen_gap_chain_targets;
        seen_gap_chain_targets.reserve(64);
        std::vector<int64_t> seen_topmax_release_targets;
        seen_topmax_release_targets.reserve(32);

        auto is_tail_chain_kind = [](const int kind) {
          return kind == kTailMoveChain || kind == kTailMoveGapChain;
        };
        bool evaluating_relief_anchor = false;
        bool evaluating_endpoint_hpwl_site_anchor = false;
        bool evaluating_endpoint_hpwl_row_anchor = false;
        auto record_tail_legal_reject = [&](const int kind) {
          ++high_pressure_tail_legal_rejects;
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_legal_rejects;
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_legal_rejects;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_legal_rejects;
          }
          if (is_tail_chain_kind(kind)) {
            ++high_pressure_tail_chain_legal_rejects;
          }
        };
        auto record_tail_static_reject = [&](const int kind) {
          ++high_pressure_tail_static_rejects;
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_static_rejects;
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_static_rejects;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_static_rejects;
          }
          if (is_tail_chain_kind(kind)) {
            ++high_pressure_tail_chain_static_rejects;
          }
        };
        auto record_tail_hpwl_reject = [&](const int kind) {
          ++high_pressure_tail_hpwl_rejects;
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_hpwl_rejects;
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_hpwl_rejects;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_hpwl_rejects;
          }
          if (is_tail_chain_kind(kind)) {
            ++high_pressure_tail_chain_hpwl_rejects;
          }
        };
        auto record_tail_disp_reject = [&](const int kind) {
          ++high_pressure_tail_disp_rejects;
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_disp_rejects;
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_disp_rejects;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_disp_rejects;
          }
          if (is_tail_chain_kind(kind)) {
            ++high_pressure_tail_chain_disp_rejects;
          }
        };
        auto record_tail_tail_reject = [&](const int kind) {
          ++high_pressure_tail_tail_rejects;
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_tail_rejects;
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_tail_rejects;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_tail_rejects;
          }
          if (is_tail_chain_kind(kind)) {
            ++high_pressure_tail_chain_tail_rejects;
          }
        };

        auto consider_tail_move =
            [&](const int kind,
                const std::array<HighPressureTailMove,
                                 kHighPressureTailChainMaxCells>& moves,
                const int move_count,
                const int reserve_row,
                const int reserve_site,
                const int reserve_width,
                const int reserve_height,
                Group* reserve_group) {
              if (move_count <= 0) {
                return;
              }
              if (evaluating_relief_anchor) {
                ++high_pressure_tail_relief_row_scored;
              }
              if (endpoint_tail_owner) {
                ++high_pressure_endpoint_scored;
              }
              if (topmax_tail_owner) {
                ++high_pressure_topmax_candidates;
              }
              if (actual_topmax_tail_owner) {
                ++high_pressure_topmax_actual_candidates;
              }
              int old_disp_sum = 0;
              int new_disp_sum = 0;
              double old_disp_cost_sum = 0.0;
              double new_disp_cost_sum = 0.0;
              int max_old_disp = 0;
              int max_new_disp = 0;
              int old_tail_bin_sum = 0;
              int new_tail_bin_sum = 0;
              int old_extreme_bin_count = 0;
              int new_extreme_bin_count = 0;
              int tail_bin_worsen_count = 0;
              int extreme_bin_worsen_count = 0;
              for (int i = 0; i < move_count; ++i) {
                const int move_idx = moves[i].idx;
                if (move_idx < 0
                    || move_idx >= static_cast<int>(cells.size())) {
                  record_tail_legal_reject(kind);
                  return;
                }
                const LegalmPlaceCell& moved = cells[move_idx];
                const int from_row = current_rows[move_idx];
                const int from_site = current_sites[move_idx];
                if (from_row < 0 || from_site < 0
                    || !row_site_compatible(
                        moved, moves[i].row, moves[i].site)) {
                  record_tail_static_reject(kind);
                  return;
                }
                const int old_moved_disp
                    = displacement_sites(moved, from_row, from_site);
                const int new_moved_disp
                    = displacement_sites(moved, moves[i].row, moves[i].site);
                old_disp_sum += old_moved_disp;
                new_disp_sum += new_moved_disp;
                old_disp_cost_sum
                    += displacement_cost(moved, from_row, from_site);
                new_disp_cost_sum
                    += displacement_cost(moved, moves[i].row, moves[i].site);
                max_old_disp = std::max(max_old_disp, old_moved_disp);
                max_new_disp = std::max(max_new_disp, new_moved_disp);
                const int old_moved_tail_bin
                    = high_pressure_tail_bin(old_moved_disp);
                const int new_moved_tail_bin
                    = high_pressure_tail_bin(new_moved_disp);
                old_tail_bin_sum += old_moved_tail_bin;
                new_tail_bin_sum += new_moved_tail_bin;
                old_extreme_bin_count += old_moved_tail_bin == 2 ? 1 : 0;
                new_extreme_bin_count += new_moved_tail_bin == 2 ? 1 : 0;
                if (new_moved_tail_bin > old_moved_tail_bin) {
                  ++tail_bin_worsen_count;
                }
                if (old_moved_tail_bin < 2 && new_moved_tail_bin == 2) {
                  ++extreme_bin_worsen_count;
                }
              }

              const int new_item_disp
                  = displacement_sites(item, moves[0].row, moves[0].site);
              const int old_item_tail_bin = high_pressure_tail_bin(old_disp);
              const int new_item_tail_bin
                  = high_pressure_tail_bin(new_item_disp);
              const int net_tail_bin_delta
                  = (new_tail_bin_sum + new_item_tail_bin)
                    - (old_tail_bin_sum + old_item_tail_bin);
              const int net_extreme_bin_delta
                  = (new_extreme_bin_count
                     + (new_item_tail_bin == 2 ? 1 : 0))
                    - (old_extreme_bin_count
                       + (old_item_tail_bin == 2 ? 1 : 0));
              const int disp_delta = new_disp_sum - old_disp_sum;
              const double disp_cost_delta
                  = new_disp_cost_sum - old_disp_cost_sum;
              const bool topmax_release_priority
                  = topmax_tail_owner && kind == kTailMoveTopMaxRelease;
              const int item_disp_gain = old_disp - new_item_disp;
              const double cheap_item_hpwl_delta
                  = endpoint_tail_owner
                        ? hpwl_delta_sites(item, moves[0].row, moves[0].site)
                        : 0.0;
              const bool endpoint_hpwl_credit_candidate
                  = endpoint_tail_owner && new_item_disp < old_disp
                    && cheap_item_hpwl_delta
                           <= -kHighPressureEndpointHpwlCreditMinGainSites;
              const double endpoint_hpwl_disp_credit
                  = endpoint_hpwl_credit_candidate
                        ? std::min(
                              kHighPressureEndpointHpwlCreditDispSlack,
                              kHighPressureEndpointHpwlCreditDispScale
                                  * -cheap_item_hpwl_delta)
                        : 0.0;
              const double disp_cost_limit
                  = topmax_release_priority
                        ? std::max(
                              0.25,
                              kHighPressureTopMaxReleaseDispSlack
                                  * static_cast<double>(
                                      std::max(0, item_disp_gain)))
                        : (endpoint_hpwl_credit_candidate
                               ? std::max(-0.25, endpoint_hpwl_disp_credit)
                               : -0.25);
              const bool endpoint_hpwl_credit_used
                  = endpoint_hpwl_credit_candidate && disp_cost_delta >= -0.25;
              if (endpoint_hpwl_credit_used) {
                ++high_pressure_endpoint_hpwl_credit_candidates;
              }
              if (new_item_disp >= old_disp
                  || disp_cost_delta >= disp_cost_limit) {
                if (endpoint_hpwl_credit_used) {
                  ++high_pressure_endpoint_hpwl_credit_rejects;
                }
                record_tail_disp_reject(kind);
                return;
              }
              if (max_new_disp > max_old_disp + tail_displacement_slack) {
                record_tail_tail_reject(kind);
                return;
              }
              if (evaluating_endpoint_hpwl_site_anchor
                  || evaluating_endpoint_hpwl_row_anchor) {
                if (evaluating_endpoint_hpwl_site_anchor) {
                  ++high_pressure_endpoint_hpwl_anchor_scored;
                }
                if (evaluating_endpoint_hpwl_row_anchor) {
                  ++high_pressure_endpoint_hpwl_row_anchor_scored;
                }
                const bool site_tail_reject = false;
                const bool row_tail_reject
                    = evaluating_endpoint_hpwl_row_anchor
                      && (tail_bin_worsen_count > 0
                          || new_tail_bin_sum > old_tail_bin_sum);
                if (site_tail_reject || row_tail_reject) {
                  if (evaluating_endpoint_hpwl_site_anchor) {
                    ++high_pressure_endpoint_hpwl_anchor_tail_rejects;
                  }
                  if (evaluating_endpoint_hpwl_row_anchor) {
                    ++high_pressure_endpoint_hpwl_row_anchor_tail_rejects;
                  }
                  record_tail_tail_reject(kind);
                  return;
                }
              }
              const bool previous_endpoint_delta_context
                  = high_pressure_endpoint_delta_context;
              const bool previous_topmax_delta_context
                  = high_pressure_topmax_delta_context;
              high_pressure_endpoint_delta_context = endpoint_tail_owner;
              high_pressure_topmax_delta_context = topmax_tail_owner;
              const HighPressureTailDelta hpwl_delta_result
                  = tail_delta(moves, move_count, true);
              high_pressure_endpoint_delta_context
                  = previous_endpoint_delta_context;
              high_pressure_topmax_delta_context
                  = previous_topmax_delta_context;
              const double hpwl_delta = hpwl_delta_result.hpwl_delta_sites;
              if (hpwl_delta > kHighPressureTailHpwlMaxLossSites) {
                if (endpoint_hpwl_credit_used) {
                  ++high_pressure_endpoint_hpwl_credit_rejects;
                }
                record_tail_hpwl_reject(kind);
                return;
              }
              if (endpoint_hpwl_credit_used
                  && hpwl_delta
                         > -kHighPressureEndpointHpwlCreditExactMinGainSites) {
                ++high_pressure_endpoint_hpwl_credit_rejects;
                record_tail_hpwl_reject(kind);
                return;
              }
              if (evaluating_endpoint_hpwl_site_anchor
                  && (tail_bin_worsen_count > 0
                      || new_tail_bin_sum > old_tail_bin_sum
                      || extreme_bin_worsen_count > 0)
                  && hpwl_delta
                         > -(kHighPressureEndpointHpwlSiteTailMinGainSites
                             + 20.0
                                   * static_cast<double>(
                                       std::max(0, net_tail_bin_delta))
                             + 20.0
                                   * static_cast<double>(
                                       std::max(0,
                                                net_extreme_bin_delta)))) {
                ++high_pressure_endpoint_hpwl_anchor_tail_rejects;
                record_tail_tail_reject(kind);
                return;
              }
              if (!hpwl_delta_result.bbox_active) {
                ++high_pressure_tail_no_bbox_accepts;
              }
              const int old_tail_bin = high_pressure_tail_bin(max_old_disp);
              const int new_tail_bin = high_pressure_tail_bin(max_new_disp);
              const double endpoint_max_relief
                  = endpoint_tail_owner
                        ? kHighPressureEndpointMaxReliefWeight
                              * static_cast<double>(
                                  std::max(0, max_old_disp - max_new_disp))
                        : 0.0;
              const double topmax_release_relief
                  = topmax_release_priority
                        ? kHighPressureTopMaxReleaseReliefWeight
                              * static_cast<double>(
                                  std::max(0, item_disp_gain))
                        : 0.0;
              const double score_delta
                  = disp_cost_delta + kHighPressureTailHpwlWeight * hpwl_delta
                    - kHighPressureTailBinReliefBonus
                          * static_cast<double>(
                              std::max(0, old_tail_bin - new_tail_bin))
                    - endpoint_max_relief - topmax_release_relief;
              if (score_delta < best_score_delta - 0.25
                  || (std::abs(score_delta - best_score_delta) <= 0.25
                      && (new_tail_bin < best_new_tail_bin
                          || (new_tail_bin == best_new_tail_bin
                              && (hpwl_delta < best_hpwl_delta
                                  || (hpwl_delta == best_hpwl_delta
                                      && disp_delta < best_disp_delta)))))) {
                best_score_delta = score_delta;
                best_hpwl_delta = hpwl_delta;
                best_disp_delta = disp_delta;
                best_old_tail_bin = old_tail_bin;
                best_new_tail_bin = new_tail_bin;
                best_kind = kind;
                best_from_relief_anchor = evaluating_relief_anchor;
                best_from_endpoint_hpwl_site_anchor
                    = evaluating_endpoint_hpwl_site_anchor;
                best_from_endpoint_hpwl_credit = endpoint_hpwl_credit_used;
                best_from_endpoint_hpwl_row_anchor
                    = evaluating_endpoint_hpwl_row_anchor;
                best_reserve_row = reserve_row;
                best_reserve_site = reserve_site;
                best_reserve_width = reserve_width;
                best_reserve_height = reserve_height;
                best_reserve_group = reserve_group;
                best_moves = moves;
                best_move_count = move_count;
              }
            };

        std::array<int, kHighPressureEndpointSiteAnchorCap> site_anchors{};
        std::array<unsigned char, kHighPressureEndpointSiteAnchorCap>
            site_anchor_is_endpoint_hpwl{};
        int site_anchor_count = 0;
        auto add_site_anchor = [&](const int site,
                                   const bool endpoint_hpwl_anchor = false) {
          if (site < 0 || site + item.width_sites > site_count) {
            return false;
          }
          for (int i = 0; i < site_anchor_count; ++i) {
            if (site_anchors[i] == site) {
              return false;
            }
          }
          if (site_anchor_count < static_cast<int>(site_anchors.size())) {
            site_anchors[site_anchor_count] = site;
            site_anchor_is_endpoint_hpwl[site_anchor_count]
                = endpoint_hpwl_anchor ? static_cast<unsigned char>(1)
                                       : static_cast<unsigned char>(0);
            ++site_anchor_count;
            return true;
          }
          return false;
        };
        add_site_anchor(item.original_site);
        add_site_anchor(item.desired_site);
        add_site_anchor((item.original_site + item.desired_site) / 2);
        add_site_anchor(old_site);
        struct EndpointHpwlRowAnchor
        {
          int row = -1;
          double gain_sites = 0.0;
          int displacement = std::numeric_limits<int>::max();
          int free_gap_score = 0;
          double rank_score = std::numeric_limits<double>::lowest();
        };
        std::array<EndpointHpwlRowAnchor,
                   kHighPressureEndpointHpwlRowAnchorCap>
            endpoint_hpwl_row_anchors{};
        int endpoint_hpwl_row_anchor_count = 0;
        auto row_from_center_y = [&](const int64_t center_y) {
          const int64_t grid_origin = grid_->gridYToDbu(GridY{0}).v;
          const int64_t row_pitch
              = row_count > 1
                    ? std::max<int64_t>(
                          1,
                          grid_->gridYToDbu(GridY{1}).v - grid_origin)
                    : std::max<int64_t>(1, item.cell->getHeight().v);
          const int64_t bottom
              = center_y - static_cast<int64_t>(item.cell->getHeight().v) / 2;
          const int row = static_cast<int>(std::llround(
              static_cast<double>(bottom - grid_origin)
              / static_cast<double>(row_pitch)));
          return std::clamp(row, 0, std::max(0, row_count - item.height_rows));
        };
        auto endpoint_row_free_gap_score = [&](const int row) {
          if (row < 0 || row >= row_count) {
            return 0;
          }
          int best_score = 0;
          for (const Interval& interval : free_intervals[row]) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              continue;
            }
            const int free_width = interval.second - interval.first;
            for (int site_anchor_idx = 0; site_anchor_idx < site_anchor_count;
                 ++site_anchor_idx) {
              const int anchor = site_anchors[site_anchor_idx];
              const int clamped = std::clamp(anchor,
                                             interval.first,
                                             interval.second - item.width_sites);
              if (!row_site_compatible(item, row, clamped)) {
                continue;
              }
              const int distance = std::abs(clamped - anchor);
              const int support
                  = std::min(free_width, max_disp_threshold_sites)
                    - std::min(distance, max_disp_threshold_sites);
              best_score = std::max(best_score, support);
            }
          }
          return best_score;
        };
        auto insert_endpoint_hpwl_row_anchor = [&](const int row,
                                                   const double gain_sites) {
          if (kHighPressureEndpointHpwlRowAnchorCap <= 0
              || !endpoint_tail_owner || row < 0
              || row + item.height_rows > row_count
              || gain_sites < kHighPressureEndpointHpwlAnchorMinGainSites) {
            return;
          }
          const int candidate_disp = displacement_sites(item, row, old_site);
          if (candidate_disp >= old_disp) {
            return;
          }
          if (high_pressure_tail_bin(candidate_disp)
              > high_pressure_tail_bin(old_disp)) {
            return;
          }
          const int free_gap_score = endpoint_row_free_gap_score(row);
          const bool strong_row_hpwl_gain
              = gain_sites
                >= std::max(2.0,
                            static_cast<double>(
                                std::max(1, max_disp_threshold_sites)) / 8.0);
          if (free_gap_score <= 0 && !strong_row_hpwl_gain) {
            return;
          }
          const double rank_score
              = gain_sites
                + 0.05
                      * static_cast<double>(
                          std::min(free_gap_score, max_disp_threshold_sites))
                - 0.02 * static_cast<double>(candidate_disp);
          for (int i = 0; i < endpoint_hpwl_row_anchor_count; ++i) {
            if (endpoint_hpwl_row_anchors[i].row == row) {
              if (rank_score > endpoint_hpwl_row_anchors[i].rank_score) {
                endpoint_hpwl_row_anchors[i].gain_sites = gain_sites;
                endpoint_hpwl_row_anchors[i].displacement = candidate_disp;
                endpoint_hpwl_row_anchors[i].free_gap_score = free_gap_score;
                endpoint_hpwl_row_anchors[i].rank_score = rank_score;
              }
              return;
            }
          }
          EndpointHpwlRowAnchor candidate{
              row, gain_sites, candidate_disp, free_gap_score, rank_score};
          int insert_at = endpoint_hpwl_row_anchor_count;
          for (int i = 0; i < endpoint_hpwl_row_anchor_count; ++i) {
            if (candidate.rank_score
                    > endpoint_hpwl_row_anchors[i].rank_score
                || (candidate.rank_score
                        == endpoint_hpwl_row_anchors[i].rank_score
                    && candidate.displacement
                           < endpoint_hpwl_row_anchors[i].displacement)) {
              insert_at = i;
              break;
            }
          }
          if (insert_at >= kHighPressureEndpointHpwlRowAnchorCap) {
            return;
          }
          const int limit = std::min(endpoint_hpwl_row_anchor_count,
                                     kHighPressureEndpointHpwlRowAnchorCap - 1);
          for (int i = limit; i > insert_at; --i) {
            endpoint_hpwl_row_anchors[i] = endpoint_hpwl_row_anchors[i - 1];
          }
          endpoint_hpwl_row_anchors[insert_at] = candidate;
          endpoint_hpwl_row_anchor_count = std::min(
              endpoint_hpwl_row_anchor_count + 1,
              kHighPressureEndpointHpwlRowAnchorCap);
        };
        auto site_from_center_x = [&](const int64_t center_x) {
          const int64_t grid_origin = gridToDbu(GridX{0}, site_width).v;
          const int64_t left
              = center_x - static_cast<int64_t>(item.cell->getWidth().v) / 2;
          const int site = static_cast<int>(std::llround(
              static_cast<double>(left - grid_origin)
              / static_cast<double>(std::max(1, site_width.v))));
          return std::clamp(site, 0, std::max(0, site_count - item.width_sites));
        };
        auto add_hpwl_projection_site_anchor = [&](const int64_t seed_center_x) {
          if (item.hpwl_terms.empty()) {
            return;
          }
          int64_t projected_sum = 0;
          int projected_count = 0;
          for (const LegalmHpwlTerm& term : item.hpwl_terms) {
            int64_t low = term.other_min_x - term.offset_x;
            int64_t high = term.other_max_x - term.offset_x;
            if (low > high) {
              std::swap(low, high);
            }
            projected_sum += std::clamp(seed_center_x, low, high);
            ++projected_count;
          }
          if (projected_count > 0) {
            add_site_anchor(site_from_center_x(projected_sum / projected_count));
          }
        };
        auto add_endpoint_current_hpwl_site_anchors = [&]() {
          if (!endpoint_tail_owner || tail_cell_nets[idx].empty()) {
            return;
          }
          struct EndpointHpwlAnchor
          {
            int site = -1;
            double gain_sites = 0.0;
            int displacement = std::numeric_limits<int>::max();
          };
          std::array<EndpointHpwlAnchor, kHighPressureEndpointHpwlAnchorCap>
              hpwl_anchors{};
          int hpwl_anchor_count = 0;
          auto insert_hpwl_anchor = [&](const int site,
                                        const double gain_sites) {
            if (site < 0 || site + item.width_sites > site_count
                || gain_sites < kHighPressureEndpointHpwlAnchorMinGainSites) {
              return;
            }
            const int candidate_disp = displacement_sites(item, old_row, site);
            for (int i = 0; i < hpwl_anchor_count; ++i) {
              if (hpwl_anchors[i].site == site) {
                if (gain_sites > hpwl_anchors[i].gain_sites) {
                  hpwl_anchors[i].gain_sites = gain_sites;
                  hpwl_anchors[i].displacement = candidate_disp;
                }
                return;
              }
            }
            EndpointHpwlAnchor candidate{site, gain_sites, candidate_disp};
            int insert_at = hpwl_anchor_count;
            for (int i = 0; i < hpwl_anchor_count; ++i) {
              if (candidate.gain_sites > hpwl_anchors[i].gain_sites
                  || (candidate.gain_sites == hpwl_anchors[i].gain_sites
                      && candidate.displacement
                             < hpwl_anchors[i].displacement)) {
                insert_at = i;
                break;
              }
            }
            if (insert_at >= kHighPressureEndpointHpwlAnchorCap) {
              return;
            }
            const int limit = std::min(
                hpwl_anchor_count, kHighPressureEndpointHpwlAnchorCap - 1);
            for (int i = limit; i > insert_at; --i) {
              hpwl_anchors[i] = hpwl_anchors[i - 1];
            }
            hpwl_anchors[insert_at] = candidate;
            hpwl_anchor_count = std::min(hpwl_anchor_count + 1,
                                         kHighPressureEndpointHpwlAnchorCap);
          };

          const auto [old_center_x, old_center_y]
              = current_cell_center(idx, -1, -1);
          (void) old_center_y;
          std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
              no_moves{};
          for (const HighPressureTailIncidentNet& incident :
               tail_cell_nets[idx]) {
            if (incident.net_idx < 0
                || incident.net_idx
                       >= static_cast<int>(tail_net_summaries.size())) {
              continue;
            }
            const HighPressureTailNetSummary& summary
                = tail_net_summaries[incident.net_idx];
            if (!summary.valid || summary.edge == nullptr) {
              continue;
            }
            int64_t other_min_x = std::numeric_limits<int64_t>::max();
            int64_t other_max_x = std::numeric_limits<int64_t>::min();
            int other_pin_count = 0;
            for (const Pin* pin : summary.edge->getPins()) {
              if (pin == nullptr || pin->getNode() == nullptr
                  || tail_cell_idx(pin->getNode()) == idx) {
                continue;
              }
              const auto [pin_x, pin_y] = tail_pin_xy(pin, no_moves, 0);
              (void) pin_y;
              other_min_x = std::min(other_min_x, pin_x);
              other_max_x = std::max(other_max_x, pin_x);
              ++other_pin_count;
            }
            if (other_pin_count <= 0 || other_min_x > other_max_x) {
              continue;
            }
            int64_t other_min_y = std::numeric_limits<int64_t>::max();
            int64_t other_max_y = std::numeric_limits<int64_t>::min();
            int other_y_pin_count = 0;
            for (const Pin* pin : summary.edge->getPins()) {
              if (pin == nullptr || pin->getNode() == nullptr
                  || tail_cell_idx(pin->getNode()) == idx) {
                continue;
              }
              const auto [pin_x, pin_y] = tail_pin_xy(pin, no_moves, 0);
              (void) pin_x;
              other_min_y = std::min(other_min_y, pin_y);
              other_max_y = std::max(other_max_y, pin_y);
              ++other_y_pin_count;
            }
            for (const auto& pin_offset : incident.pin_offsets) {
              const int64_t old_pin_x
                  = old_center_x + static_cast<int64_t>(pin_offset.first);
              int64_t target_pin_x = old_pin_x;
              bool x_anchor_active = false;
              if (old_pin_x < other_min_x) {
                target_pin_x = other_min_x;
                x_anchor_active = true;
              } else if (old_pin_x > other_max_x) {
                target_pin_x = other_max_x;
                x_anchor_active = true;
              }
              if (x_anchor_active) {
                const int64_t pin_delta
                    = target_pin_x > old_pin_x ? target_pin_x - old_pin_x
                                               : old_pin_x - target_pin_x;
                const double gain_sites
                    = static_cast<double>(pin_delta)
                      / static_cast<double>(std::max(1, site_width.v));
                const int64_t target_center_x
                    = target_pin_x - static_cast<int64_t>(pin_offset.first);
                insert_hpwl_anchor(site_from_center_x(target_center_x),
                                   gain_sites);
                insert_hpwl_anchor(
                    site_from_center_x(
                        (old_center_x + 3 * target_center_x) / 4),
                    0.75 * gain_sites);
                insert_hpwl_anchor(
                    site_from_center_x((old_center_x + target_center_x) / 2),
                    0.5 * gain_sites);
                ++high_pressure_endpoint_hpwl_anchor_terms;
              }
              if (other_y_pin_count > 0 && other_min_y <= other_max_y) {
                const int64_t old_pin_y
                    = old_center_y + static_cast<int64_t>(pin_offset.second);
                int64_t target_pin_y = old_pin_y;
                bool y_anchor_active = false;
                if (old_pin_y < other_min_y) {
                  target_pin_y = other_min_y;
                  y_anchor_active = true;
                } else if (old_pin_y > other_max_y) {
                  target_pin_y = other_max_y;
                  y_anchor_active = true;
                }
                if (!y_anchor_active) {
                  continue;
                }
                const int64_t y_delta
                    = target_pin_y > old_pin_y ? target_pin_y - old_pin_y
                                               : old_pin_y - target_pin_y;
                const double y_gain_sites
                    = static_cast<double>(y_delta)
                      / static_cast<double>(std::max(1, site_width.v));
                const int64_t target_center_y
                    = target_pin_y - static_cast<int64_t>(pin_offset.second);
                insert_endpoint_hpwl_row_anchor(row_from_center_y(target_center_y),
                                                y_gain_sites);
                insert_endpoint_hpwl_row_anchor(
                    row_from_center_y((old_center_y + target_center_y) / 2),
                    0.5 * y_gain_sites);
                ++high_pressure_endpoint_hpwl_row_anchor_terms;
              }
            }
          }
          for (int i = 0; i < hpwl_anchor_count; ++i) {
            if (add_site_anchor(hpwl_anchors[i].site, true)) {
              ++high_pressure_endpoint_hpwl_anchor_sites;
            }
          }
        };
        if (endpoint_tail_owner) {
          const int64_t original_center_x
              = gridToDbu(GridX{item.original_site}, site_width).v
                + static_cast<int64_t>(item.cell->getWidth().v) / 2;
          const int64_t desired_center_x
              = gridToDbu(GridX{item.desired_site}, site_width).v
                + static_cast<int64_t>(item.cell->getWidth().v) / 2;
          const int64_t old_center_x
              = gridToDbu(GridX{old_site}, site_width).v
                + static_cast<int64_t>(item.cell->getWidth().v) / 2;
          add_hpwl_projection_site_anchor(original_center_x);
          add_hpwl_projection_site_anchor(desired_center_x);
          add_hpwl_projection_site_anchor(old_center_x);
          add_endpoint_current_hpwl_site_anchors();
          add_site_anchor((old_site + item.original_site) / 2);
        }

        std::array<int,
                   12 + kHighPressureEndpointExtraRowAnchorCap>
            row_anchors{};
        std::array<unsigned char,
                   12 + kHighPressureEndpointExtraRowAnchorCap>
            row_anchor_is_relief{};
        std::array<unsigned char,
                   12 + kHighPressureEndpointExtraRowAnchorCap>
            row_anchor_is_endpoint_hpwl{};
        int row_anchor_count = 0;
        auto add_row_anchor = [&](const int row,
                                  const bool relief_anchor,
                                  const bool endpoint_hpwl_anchor = false) {
          if (row < 0 || row + item.height_rows > row_count) {
            return;
          }
          for (int i = 0; i < row_anchor_count; ++i) {
            if (row_anchors[i] == row) {
              return;
            }
          }
          if (row_anchor_count < static_cast<int>(row_anchors.size())) {
            row_anchors[row_anchor_count] = row;
            row_anchor_is_relief[row_anchor_count]
                = relief_anchor ? static_cast<unsigned char>(1)
                                : static_cast<unsigned char>(0);
            row_anchor_is_endpoint_hpwl[row_anchor_count]
                = endpoint_hpwl_anchor ? static_cast<unsigned char>(1)
                                       : static_cast<unsigned char>(0);
            ++row_anchor_count;
            if (relief_anchor) {
              ++high_pressure_tail_relief_row_anchors;
            }
          }
        };
        add_row_anchor(item.original_row, false);
        add_row_anchor(item.desired_row, false);
        add_row_anchor((item.original_row + item.desired_row) / 2, false);
        add_row_anchor(old_row, false);
        add_row_anchor(item.original_row + item.vertical_step_rows, false);
        add_row_anchor(item.original_row - item.vertical_step_rows, false);
        add_row_anchor(item.desired_row + item.vertical_step_rows, false);
        add_row_anchor(item.desired_row - item.vertical_step_rows, false);
        if (endpoint_tail_owner) {
          for (int i = 0; i < endpoint_hpwl_row_anchor_count; ++i) {
            const int before = row_anchor_count;
            add_row_anchor(endpoint_hpwl_row_anchors[i].row, false, true);
            if (row_anchor_count > before) {
              ++high_pressure_endpoint_hpwl_row_anchor_rows;
              high_pressure_endpoint_hpwl_row_anchor_free_gap_score
                  += endpoint_hpwl_row_anchors[i].free_gap_score;
            }
          }
        }

        struct HighPressureTailReliefRowCandidate
        {
          int row = -1;
          int best_disp = std::numeric_limits<int>::max();
          int free_sites = 0;
          double score = std::numeric_limits<double>::max();
        };
        constexpr int kHighPressureTailReliefRowTotalCap
            = kHighPressureTailReliefRowAnchorCap
              + kHighPressureEndpointExtraRowAnchorCap;
        const int relief_row_cap
            = endpoint_tail_owner ? kHighPressureTailReliefRowTotalCap
                                  : kHighPressureTailReliefRowAnchorCap;
        std::array<HighPressureTailReliefRowCandidate,
                   kHighPressureTailReliefRowTotalCap>
            relief_rows{};
        int relief_row_count = 0;
        auto row_already_anchored = [&](const int row) {
          for (int i = 0; i < row_anchor_count; ++i) {
            if (row_anchors[i] == row) {
              return true;
            }
          }
          return false;
        };
        auto consider_relief_row_anchor = [&](const int row) {
          if (row < 0 || row >= row_count || row_already_anchored(row)) {
            return;
          }
          for (int i = 0; i < relief_row_count; ++i) {
            if (relief_rows[i].row == row) {
              return;
            }
          }
          int free_sites = 0;
          int best_disp = std::numeric_limits<int>::max();
          for (const Interval& interval : free_intervals[row]) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              continue;
            }
            free_sites += interval.second - interval.first;
            for (int site_anchor_idx = 0; site_anchor_idx < site_anchor_count;
                 ++site_anchor_idx) {
              const int site_anchor = site_anchors[site_anchor_idx];
              const int clamped = std::clamp(site_anchor,
                                             interval.first,
                                             interval.second - item.width_sites);
              if (!row_site_compatible(item, row, clamped)) {
                continue;
              }
              best_disp = std::min(
                  best_disp, displacement_sites(item, row, clamped));
            }
          }
          if (free_sites <= 0 || best_disp >= old_disp) {
            return;
          }
          const double score
              = static_cast<double>(best_disp)
                + 0.25
                      * static_cast<double>(std::abs(row - item.desired_row))
                - 0.05
                      * static_cast<double>(
                          std::min(free_sites, max_disp_threshold_sites));
          HighPressureTailReliefRowCandidate candidate{
              row, best_disp, free_sites, score};
          int insert_at = relief_row_count;
          for (int i = 0; i < relief_row_count; ++i) {
            if (candidate.score < relief_rows[i].score
                || (candidate.score == relief_rows[i].score
                    && candidate.best_disp < relief_rows[i].best_disp)) {
              insert_at = i;
              break;
            }
          }
          if (insert_at >= relief_row_cap) {
            return;
          }
          const int limit = std::min(relief_row_count, relief_row_cap - 1);
          for (int i = limit; i > insert_at; --i) {
            relief_rows[i] = relief_rows[i - 1];
          }
          relief_rows[insert_at] = candidate;
          relief_row_count = std::min(relief_row_count + 1, relief_row_cap);
        };
        auto scan_relief_rows = [&](const int center_row, const int radius) {
          for (int delta = -radius;
               delta <= radius;
               ++delta) {
            consider_relief_row_anchor(center_row + delta);
          }
        };
        scan_relief_rows(item.original_row, kHighPressureTailReliefRowSearch);
        scan_relief_rows(item.desired_row, kHighPressureTailReliefRowSearch);
        if (endpoint_tail_owner) {
          scan_relief_rows(item.original_row,
                           kHighPressureEndpointReliefRowSearch);
          scan_relief_rows(item.desired_row,
                           kHighPressureEndpointReliefRowSearch);
          scan_relief_rows(old_row, kHighPressureTailReliefRowSearch);
        }
        for (int i = 0; i < relief_row_count; ++i) {
          add_row_anchor(relief_rows[i].row, true);
        }

        auto evaluate_interval_target = [&](const int row, const int site) {
          if (high_pressure_tail_interval_candidates
              >= static_cast<int64_t>(kHighPressureTailFrontierCap)
                     * kHighPressureTailCandidateCap) {
            return;
          }
          if (row < 0 || row + item.height_rows > row_count || site < 0
              || site + item.width_sites > site_count
              || (row == old_row && site == old_site)) {
            return;
          }
          ++high_pressure_tail_interval_candidates;
          if (!fits_all_rows(
                  row, site, item.width_sites, item.height_rows, item.group)) {
            ++high_pressure_tail_legal_rejects;
            return;
          }
          std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
              moves{};
          moves[0] = {idx, row, site};
          consider_tail_move(kTailMoveInterval,
                             moves,
                             1,
                             row,
                             site,
                             item.width_sites,
                             item.height_rows,
                             item.group);
        };

        auto add_interval_targets = [&](const int row,
                                        const int target_site) {
          if (row < 0 || row >= row_count) {
            return;
          }
          const auto& intervals = free_intervals[row];
          auto it = std::lower_bound(
              intervals.begin(),
              intervals.end(),
              target_site,
              [](const Interval& interval, const int target) {
                return interval.second <= target;
              });
          auto add_from_interval = [&](const Interval& interval) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              return;
            }
            const int clamped = std::clamp(target_site,
                                           interval.first,
                                           interval.second - item.width_sites);
            evaluate_interval_target(row, clamped);
            evaluate_interval_target(row, interval.first);
            evaluate_interval_target(
                row, interval.second - item.width_sites);
          };
          int forward_samples = 0;
          for (auto fwd = it;
               fwd != intervals.end()
               && forward_samples < kHighPressureTailIntervalProbeLimit;
               ++fwd, ++forward_samples) {
            add_from_interval(*fwd);
          }
          auto back = it;
          int backward_samples = 0;
          while (back != intervals.begin()
                 && backward_samples < kHighPressureTailIntervalProbeLimit) {
            --back;
            add_from_interval(*back);
            ++backward_samples;
          }
        };

        auto evaluate_swap_probe = [&](const int row, const int probe_site) {
          if (item.height_rows != 1 || row < 0 || row >= row_count
              || probe_site < 0 || probe_site >= site_count) {
            return;
          }
          ++high_pressure_tail_swap_candidates;
          const int owner
              = tail_site_owner[static_cast<size_t>(row)
                                    * static_cast<size_t>(site_count)
                                + static_cast<size_t>(probe_site)];
          if (owner < 0 || owner == idx
              || owner >= static_cast<int>(cells.size())) {
            ++high_pressure_tail_legal_rejects;
            return;
          }
          const LegalmPlaceCell& other = cells[owner];
          const int other_row = current_rows[owner];
          const int other_site = current_sites[owner];
          if (other_row != row || other_site < 0
              || other.height_rows != item.height_rows
              || other.width_sites != item.width_sites
              || other.group != item.group) {
            ++high_pressure_tail_legal_rejects;
            return;
          }
          if (!row_site_compatible(item, other_row, other_site)
              || !row_site_compatible(other, old_row, old_site)) {
            ++high_pressure_tail_static_rejects;
            return;
          }
          std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
              moves{};
          moves[0] = {idx, other_row, other_site};
          moves[1] = {owner, old_row, old_site};
          consider_tail_move(kTailMoveSwap,
                             moves,
                             2,
                             old_row,
                             old_site,
                             item.width_sites,
                             item.height_rows,
                             item.group);
        };

        auto evaluate_chain_probe =
            [&](const int row, const int probe_site, const int free_site) {
              if (item.height_rows != 1 || item.width_sites <= 0 || row < 0
                  || row >= row_count || probe_site < 0
                  || probe_site >= site_count || free_site < 0
                  || free_site + item.width_sites > site_count) {
                return;
              }
              if (!interval_contains(free_intervals[row],
                                     free_site,
                                     item.width_sites,
                                     item.group)) {
                return;
              }
              const int target_owner
                  = tail_site_owner[static_cast<size_t>(row)
                                        * static_cast<size_t>(site_count)
                                    + static_cast<size_t>(probe_site)];
              if (target_owner < 0 || target_owner == idx
                  || target_owner >= static_cast<int>(cells.size())) {
                return;
              }
              const int target_site = current_sites[target_owner];
              if (target_site < 0 || target_site == free_site) {
                return;
              }
              const int delta_site = free_site - target_site;
              const int width = item.width_sites;
              if (std::abs(delta_site) > kHighPressureTailChainMaxSpanSites
                  || delta_site % width != 0) {
                return;
              }
              const int direction = delta_site > 0 ? 1 : -1;
              const int chain_steps = std::abs(delta_site) / width;
              if (chain_steps <= 0
                  || chain_steps + 1 > kHighPressureTailChainMaxCells) {
                return;
              }
              if (high_pressure_tail_chain_candidates
                  >= static_cast<int64_t>(kHighPressureTailFrontierCap)
                         * kHighPressureTailCandidateCap) {
                return;
              }
              ++high_pressure_tail_chain_candidates;
              if (!row_site_compatible(item, row, target_site)) {
                record_tail_static_reject(kTailMoveChain);
                return;
              }
              std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
                  moves{};
              moves[0] = {idx, row, target_site};
              int move_count = 1;
              for (int step = 0; step < chain_steps; ++step) {
                const int from_site = target_site + direction * step * width;
                const int to_site = from_site + direction * width;
                const int owner
                    = tail_site_owner[static_cast<size_t>(row)
                                          * static_cast<size_t>(site_count)
                                      + static_cast<size_t>(from_site)];
                if (owner < 0 || owner == idx
                    || owner >= static_cast<int>(cells.size())) {
                  record_tail_legal_reject(kTailMoveChain);
                  return;
                }
                const LegalmPlaceCell& shifted = cells[owner];
                if (shifted.height_rows != 1 || shifted.width_sites != width
                    || shifted.group != item.group
                    || current_rows[owner] != row
                    || current_sites[owner] != from_site) {
                  record_tail_legal_reject(kTailMoveChain);
                  return;
                }
                if (!row_site_compatible(shifted, row, to_site)) {
                  record_tail_static_reject(kTailMoveChain);
                  return;
                }
                moves[move_count++] = {owner, row, to_site};
              }
              consider_tail_move(kTailMoveChain,
                                 moves,
                                 move_count,
                                 row,
                                 free_site,
                                 item.width_sites,
                                 item.height_rows,
                                 item.group);
            };

        auto evaluate_gap_chain_probe =
            [&](const int row, const int item_site, const int free_site) {
              const int width = item.width_sites;
              if (item.height_rows != 1 || width <= 0 || row < 0
                  || row >= row_count || item_site < 0
                  || item_site + width > site_count || free_site < 0
                  || free_site + width > site_count || item_site == free_site
                  || std::abs(free_site - item_site)
                         > kHighPressureTailChainMaxSpanSites) {
                return;
              }
              if (!interval_contains(
                      free_intervals[row], free_site, width, item.group)) {
                return;
              }
              const int64_t target_key
                  = (static_cast<int64_t>(row) * site_count + item_site)
                        * site_count
                    + free_site;
              if (std::find(seen_gap_chain_targets.begin(),
                            seen_gap_chain_targets.end(),
                            target_key)
                  != seen_gap_chain_targets.end()) {
                ++high_pressure_tail_gap_chain_duplicate_skips;
                return;
              }
              seen_gap_chain_targets.push_back(target_key);
              if (!row_site_compatible(item, row, item_site)) {
                ++high_pressure_tail_gap_chain_prefilter_static_skips;
                return;
              }
              const int new_item_disp
                  = displacement_sites(item, row, item_site);
              if (new_item_disp >= old_disp) {
                ++high_pressure_tail_gap_chain_prefilter_disp_skips;
                return;
              }

              std::array<HighPressureTailMove, kHighPressureTailChainMaxCells>
                  moves{};
              moves[0] = {idx, row, item_site};
              int move_count = 1;
              int old_disp_sum = old_disp;
              int new_disp_sum = new_item_disp;
              double old_disp_cost_sum
                  = displacement_cost(item, old_row, old_site);
              double new_disp_cost_sum
                  = displacement_cost(item, row, item_site);
              int max_old_disp = old_disp;
              int max_new_disp = new_item_disp;
              auto append_shifted_cell = [&](const int from_site,
                                             const int to_site,
                                             int& next_cursor) {
                if (from_site < 0 || from_site >= site_count) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return false;
                }
                const int owner
                    = tail_site_owner[static_cast<size_t>(row)
                                          * static_cast<size_t>(site_count)
                                      + static_cast<size_t>(from_site)];
                if (owner < 0 || owner == idx
                    || owner >= static_cast<int>(cells.size())) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return false;
                }
                const LegalmPlaceCell& shifted = cells[owner];
                if (shifted.height_rows != 1 || shifted.group != item.group
                    || current_rows[owner] != row
                    || current_sites[owner] != from_site) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return false;
                }
                if (to_site < 0
                    || to_site + shifted.width_sites > site_count
                    || !row_site_compatible(shifted, row, to_site)) {
                  ++high_pressure_tail_gap_chain_prefilter_static_skips;
                  return false;
                }
                if (move_count >= kHighPressureTailChainMaxCells) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return false;
                }
                const int old_shifted_disp
                    = displacement_sites(shifted, row, from_site);
                const int new_shifted_disp
                    = displacement_sites(shifted, row, to_site);
                old_disp_sum += old_shifted_disp;
                new_disp_sum += new_shifted_disp;
                old_disp_cost_sum
                    += displacement_cost(shifted, row, from_site);
                new_disp_cost_sum += displacement_cost(shifted, row, to_site);
                max_old_disp = std::max(max_old_disp, old_shifted_disp);
                max_new_disp = std::max(max_new_disp, new_shifted_disp);
                moves[move_count++] = {owner, row, to_site};
                next_cursor = from_site + shifted.width_sites;
                return true;
              };

              if (free_site > item_site) {
                int cursor = item_site;
                while (cursor < free_site) {
                  int next_cursor = cursor;
                  if (!append_shifted_cell(cursor, cursor + width, next_cursor)) {
                    return;
                  }
                  cursor = next_cursor;
                }
                if (cursor != free_site) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return;
                }
              } else {
                int cursor = free_site + width;
                const int target_end = item_site + width;
                while (cursor < target_end) {
                  int next_cursor = cursor;
                  if (!append_shifted_cell(cursor, cursor - width, next_cursor)) {
                    return;
                  }
                  cursor = next_cursor;
                }
                if (cursor != target_end) {
                  ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                  return;
                }
              }
              if (move_count <= 1) {
                ++high_pressure_tail_gap_chain_prefilter_legal_skips;
                return;
              }
              if (new_disp_cost_sum - old_disp_cost_sum >= -0.25) {
                ++high_pressure_tail_gap_chain_prefilter_disp_skips;
                return;
              }
              if (max_new_disp > max_old_disp + tail_displacement_slack) {
                ++high_pressure_tail_gap_chain_prefilter_tail_skips;
                return;
              }
              if (high_pressure_tail_chain_candidates
                  >= static_cast<int64_t>(kHighPressureTailFrontierCap)
                         * kHighPressureTailCandidateCap) {
                return;
              }
              ++high_pressure_tail_chain_candidates;
              ++high_pressure_tail_gap_chain_candidates;
              consider_tail_move(kTailMoveGapChain,
                                 moves,
                                 move_count,
                                 row,
                                 free_site,
                                 width,
                                 item.height_rows,
                                 item.group);
            };

        auto add_gap_chain_targets = [&](const int row,
                                         const int target_site) {
          if (item.height_rows != 1 || row < 0 || row >= row_count) {
            return;
          }
          std::array<int, kHighPressureTailGapChainItemProbeLimit> item_sites{};
          int item_site_count = 0;
          auto add_item_site = [&](const int site) {
            if (site < 0 || site + item.width_sites > site_count) {
              return;
            }
            for (int i = 0; i < item_site_count; ++i) {
              if (item_sites[i] == site) {
                return;
              }
            }
            if (item_site_count
                < static_cast<int>(item_sites.size())) {
              item_sites[item_site_count++] = site;
            }
          };
          add_item_site(target_site);
          if (target_site >= 0 && target_site < site_count) {
            const int owner
                = tail_site_owner[static_cast<size_t>(row)
                                      * static_cast<size_t>(site_count)
                                  + static_cast<size_t>(target_site)];
            if (owner >= 0 && owner != idx
                && owner < static_cast<int>(cells.size())) {
              const int owner_site = current_sites[owner];
              if (owner_site >= 0) {
                add_item_site(owner_site);
                add_item_site(owner_site - item.width_sites);
                add_item_site(owner_site + cells[owner].width_sites);
              }
            }
          }

          auto evaluate_interval_free_sites = [&](const Interval& interval,
                                                  const int item_site) {
            if (interval.group != item.group
                || interval.second - interval.first < item.width_sites) {
              return;
            }
            const int clamped_free
                = std::clamp(item_site,
                             interval.first,
                             interval.second - item.width_sites);
            evaluate_gap_chain_probe(row, item_site, clamped_free);
            evaluate_gap_chain_probe(row, item_site, interval.first);
            evaluate_gap_chain_probe(
                row, item_site, interval.second - item.width_sites);
          };

          for (int item_site_idx = 0; item_site_idx < item_site_count;
               ++item_site_idx) {
            const int item_site = item_sites[item_site_idx];
            if (row == old_row) {
              evaluate_gap_chain_probe(row, item_site, old_site);
            }
            const auto& intervals = free_intervals[row];
            auto it = std::lower_bound(
                intervals.begin(),
                intervals.end(),
                item_site,
                [](const Interval& interval, const int target) {
                  return interval.second <= target;
                });
            int forward_samples = 0;
            for (auto fwd = it;
                 fwd != intervals.end()
                 && forward_samples < kHighPressureTailIntervalProbeLimit;
                 ++fwd, ++forward_samples) {
              evaluate_interval_free_sites(*fwd, item_site);
            }
            auto back = it;
            int backward_samples = 0;
            while (back != intervals.begin()
                   && backward_samples < kHighPressureTailIntervalProbeLimit) {
              --back;
              evaluate_interval_free_sites(*back, item_site);
              ++backward_samples;
            }
          }
        };

        auto evaluate_topmax_two_cell_release =
            [&](const int row, const int target_site) {
              if (!topmax_tail_owner || item.height_rows != 1 || row < 0
                  || row >= row_count || target_site < 0
                  || target_site + item.width_sites > site_count) {
                return;
              }
              auto record_topmax_direct_legal_reject = [&]() {
                ++high_pressure_topmax_legal_rejects;
                if (actual_topmax_tail_owner) {
                  ++high_pressure_topmax_actual_legal_rejects;
                }
              };
              auto record_topmax_direct_static_reject = [&]() {
                ++high_pressure_topmax_static_rejects;
                if (actual_topmax_tail_owner) {
                  ++high_pressure_topmax_actual_static_rejects;
                }
              };
              if (!row_site_compatible(item, row, target_site)) {
                record_topmax_direct_static_reject();
                return;
              }
              const int owner
                  = tail_site_owner[static_cast<size_t>(row)
                                        * static_cast<size_t>(site_count)
                                    + static_cast<size_t>(target_site)];
              if (owner < 0 || owner == idx
                  || owner >= static_cast<int>(cells.size())) {
                record_topmax_direct_legal_reject();
                return;
              }
              const LegalmPlaceCell& blocker = cells[owner];
              const int blocker_row = current_rows[owner];
              const int blocker_site = current_sites[owner];
              if (blocker_row != row || blocker_site < 0
                  || blocker.height_rows != 1 || blocker.group != item.group
                  || blocker.width_sites < item.width_sites) {
                record_topmax_direct_legal_reject();
                return;
              }
              const int release_site = std::clamp(
                  target_site,
                  blocker_site,
                  blocker_site + blocker.width_sites - item.width_sites);
              if (!row_site_compatible(item, row, release_site)) {
                record_topmax_direct_static_reject();
                return;
              }

              auto evaluate_free_site = [&](const int free_site) {
                if (high_pressure_topmax_two_cell_candidates
                    >= static_cast<int64_t>(
                           std::max(1, high_pressure_topmax_owner_count))
                           * kHighPressureTopMaxTwoCellCandidateCap) {
                  return;
                }
                if (free_site < 0
                    || free_site + blocker.width_sites > site_count
                    || !interval_contains(free_intervals[row],
                                          free_site,
                                          blocker.width_sites,
                                          blocker.group)) {
                  record_topmax_direct_legal_reject();
                  return;
                }
                const int64_t target_key
                    = ((static_cast<int64_t>(row) * site_count + release_site)
                       * site_count
                       + free_site)
                          * static_cast<int64_t>(cells.size())
                      + owner;
                if (std::find(seen_topmax_release_targets.begin(),
                              seen_topmax_release_targets.end(),
                              target_key)
                    != seen_topmax_release_targets.end()) {
                  return;
                }
                seen_topmax_release_targets.push_back(target_key);
                if (!row_site_compatible(blocker, row, free_site)) {
                  record_topmax_direct_static_reject();
                  return;
                }
                ++high_pressure_topmax_two_cell_candidates;
                if (actual_topmax_tail_owner) {
                  ++high_pressure_topmax_actual_two_cell_candidates;
                }
                std::array<HighPressureTailMove,
                           kHighPressureTailChainMaxCells>
                    moves{};
                moves[0] = {idx, row, release_site};
                moves[1] = {owner, row, free_site};
                consider_tail_move(kTailMoveTopMaxRelease,
                                   moves,
                                   2,
                                   row,
                                   free_site,
                                   blocker.width_sites,
                                   blocker.height_rows,
                                   blocker.group);
              };

              const auto& intervals = free_intervals[row];
              auto it = std::lower_bound(
                  intervals.begin(),
                  intervals.end(),
                  blocker_site,
                  [](const Interval& interval, const int target) {
                    return interval.second <= target;
                  });
              auto probe_interval = [&](const Interval& interval) {
                if (interval.group != blocker.group
                    || interval.second - interval.first
                           < blocker.width_sites) {
                  return;
                }
                const int clamped = std::clamp(
                    blocker_site,
                    interval.first,
                    interval.second - blocker.width_sites);
                evaluate_free_site(clamped);
                evaluate_free_site(interval.first);
                evaluate_free_site(interval.second - blocker.width_sites);
              };
              int forward_samples = 0;
              for (auto fwd = it;
                   fwd != intervals.end()
                   && forward_samples < kHighPressureTopMaxFreeProbeLimit;
                   ++fwd, ++forward_samples) {
                probe_interval(*fwd);
              }
              auto back = it;
              int backward_samples = 0;
              while (back != intervals.begin()
                     && backward_samples < kHighPressureTopMaxFreeProbeLimit) {
                --back;
                probe_interval(*back);
                ++backward_samples;
              }
            };

        for (int i = 0; i < row_anchor_count; ++i) {
          const int row = row_anchors[i];
          evaluating_relief_anchor = row_anchor_is_relief[i] != 0;
          evaluating_endpoint_hpwl_row_anchor
              = row_anchor_is_endpoint_hpwl[i] != 0;
          for (int site_anchor_idx = 0; site_anchor_idx < site_anchor_count;
               ++site_anchor_idx) {
            const int site_anchor = site_anchors[site_anchor_idx];
            evaluating_endpoint_hpwl_site_anchor
                = site_anchor_is_endpoint_hpwl[site_anchor_idx] != 0;
            add_interval_targets(row, site_anchor);
            add_gap_chain_targets(row, site_anchor);
            evaluate_swap_probe(row, site_anchor);
            evaluate_swap_probe(row, site_anchor - item.width_sites);
            evaluate_swap_probe(row, site_anchor + item.width_sites);
            if (topmax_tail_owner) {
              evaluate_topmax_two_cell_release(row, site_anchor);
              evaluate_topmax_two_cell_release(row,
                                               site_anchor - item.width_sites);
              evaluate_topmax_two_cell_release(row,
                                               site_anchor + item.width_sites);
            }
            if (item.height_rows == 1) {
              const int max_chain_steps = std::max(
                  1,
                  std::min(kHighPressureTailChainMaxCells - 1,
                           kHighPressureTailChainMaxSpanSites
                               / std::max(1, item.width_sites)));
              for (int step = 1; step <= max_chain_steps; ++step) {
                evaluate_chain_probe(row,
                                     site_anchor,
                                     site_anchor
                                         + step * item.width_sites);
                evaluate_chain_probe(row,
                                     site_anchor,
                                     site_anchor
                                         - step * item.width_sites);
              }
            }
          }
        }
        evaluating_relief_anchor = false;
        evaluating_endpoint_hpwl_site_anchor = false;
        evaluating_endpoint_hpwl_row_anchor = false;

        if (best_move_count > 0) {
          if (best_reserve_row >= 0 && best_reserve_site >= 0) {
            reserve_slot(best_reserve_row,
                         best_reserve_site,
                         best_reserve_width,
                         best_reserve_height,
                         best_reserve_group);
          }
          for (int i = 0; i < best_move_count; ++i) {
            const int move_idx = best_moves[i].idx;
            set_tail_owner(
                move_idx, current_rows[move_idx], current_sites[move_idx], -1);
          }
          for (int i = 0; i < best_move_count; ++i) {
            const int move_idx = best_moves[i].idx;
            current_rows[move_idx] = best_moves[i].row;
            current_sites[move_idx] = best_moves[i].site;
          }
          for (int i = 0; i < best_move_count; ++i) {
            const int move_idx = best_moves[i].idx;
            set_tail_owner(move_idx,
                           current_rows[move_idx],
                           current_sites[move_idx],
                           move_idx);
            if (stage3_changed[move_idx] == 0) {
              stage3_changed[move_idx] = 1;
              ++stage3_changed_cells;
            }
          }
          tail_refresh_nets(best_moves, best_move_count);
          ++high_pressure_tail_moved;
          high_pressure_tail_moved_cells += best_move_count;
          high_pressure_tail_hpwl_gain_sites
              += std::max(0.0, -best_hpwl_delta);
          high_pressure_tail_hpwl_loss_sites
              += std::max(0.0, best_hpwl_delta);
          high_pressure_tail_disp_gain_sites += -best_disp_delta;
          if (best_old_tail_bin >= 0 && best_old_tail_bin < 3
              && best_new_tail_bin >= 0 && best_new_tail_bin < 3) {
            ++high_pressure_tail_accept_old_tail_bins[best_old_tail_bin];
            ++high_pressure_tail_accept_new_tail_bins[best_new_tail_bin];
          }
          if (endpoint_tail_owner) {
            ++high_pressure_endpoint_moves;
            high_pressure_endpoint_hpwl_gain_sites
                += std::max(0.0, -best_hpwl_delta);
            high_pressure_endpoint_hpwl_loss_sites
                += std::max(0.0, best_hpwl_delta);
            high_pressure_endpoint_disp_gain_sites += -best_disp_delta;
            if (best_from_endpoint_hpwl_site_anchor) {
              ++high_pressure_endpoint_hpwl_anchor_moves;
              high_pressure_endpoint_hpwl_anchor_gain_sites
                  += std::max(0.0, -best_hpwl_delta);
              high_pressure_endpoint_hpwl_anchor_loss_sites
                  += std::max(0.0, best_hpwl_delta);
              high_pressure_endpoint_hpwl_anchor_disp_gain_sites
                  += -best_disp_delta;
            }
            if (best_from_endpoint_hpwl_row_anchor) {
              ++high_pressure_endpoint_hpwl_row_anchor_moves;
              high_pressure_endpoint_hpwl_row_anchor_gain_sites
                  += std::max(0.0, -best_hpwl_delta);
              high_pressure_endpoint_hpwl_row_anchor_loss_sites
                  += std::max(0.0, best_hpwl_delta);
              high_pressure_endpoint_hpwl_row_anchor_disp_gain_sites
                  += -best_disp_delta;
            }
            if (best_from_endpoint_hpwl_credit) {
              ++high_pressure_endpoint_hpwl_credit_moves;
              high_pressure_endpoint_hpwl_credit_gain_sites
                  += std::max(0.0, -best_hpwl_delta);
              high_pressure_endpoint_hpwl_credit_disp_gain_sites
                  += -best_disp_delta;
            }
          }
          if (topmax_tail_owner) {
            ++high_pressure_topmax_moves;
            high_pressure_topmax_hpwl_gain_sites
                += std::max(0.0, -best_hpwl_delta);
            high_pressure_topmax_hpwl_loss_sites
                += std::max(0.0, best_hpwl_delta);
            high_pressure_topmax_disp_gain_sites += -best_disp_delta;
          }
          if (actual_topmax_tail_owner) {
            ++high_pressure_topmax_actual_moves;
          }
          if (best_from_relief_anchor) {
            ++high_pressure_tail_relief_row_moves;
            high_pressure_tail_relief_row_hpwl_gain_sites
                += std::max(0.0, -best_hpwl_delta);
            high_pressure_tail_relief_row_disp_gain_sites += -best_disp_delta;
          }
          if (best_kind == kTailMoveInterval) {
            ++high_pressure_tail_interval_moves;
          } else if (best_kind == kTailMoveSwap) {
            ++high_pressure_tail_swap_moves;
          } else if (is_tail_chain_kind(best_kind)) {
            ++high_pressure_tail_chain_moves;
            if (best_kind == kTailMoveGapChain) {
              ++high_pressure_tail_gap_chain_moves;
            }
          } else if (best_kind == kTailMoveTopMaxRelease) {
            ++high_pressure_topmax_two_cell_moves;
          }
        } else {
          reserve_slot(
              old_row, old_site, item.width_sites, item.height_rows, item.group);
          set_tail_owner(idx, old_row, old_site, idx);
        }
      }

      std::vector<int> endpoint_after_disps;
      endpoint_after_disps.reserve(endpoint_tail_indices.size());
      for (const int endpoint_idx : endpoint_tail_indices) {
        const int disp = displacement_sites(cells[endpoint_idx],
                                            current_rows[endpoint_idx],
                                            current_sites[endpoint_idx]);
        endpoint_after_disps.push_back(disp);
        high_pressure_endpoint_after_max_disp
            = std::max(high_pressure_endpoint_after_max_disp, disp);
        ++high_pressure_endpoint_after_tail_bins[high_pressure_tail_bin(disp)];
      }
      high_pressure_endpoint_after_p99_disp = endpoint_p99(endpoint_after_disps);

      std::vector<int> topmax_after_disps;
      topmax_after_disps.reserve(topmax_tail_indices.size());
      for (const int topmax_idx : topmax_tail_indices) {
        const int disp = displacement_sites(cells[topmax_idx],
                                            current_rows[topmax_idx],
                                            current_sites[topmax_idx]);
        topmax_after_disps.push_back(disp);
        high_pressure_topmax_after_max_disp
            = std::max(high_pressure_topmax_after_max_disp, disp);
        ++high_pressure_topmax_after_tail_bins[high_pressure_tail_bin(disp)];
      }
      high_pressure_topmax_after_p99_disp
          = endpoint_p99(topmax_after_disps);
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

  auto displacement_percentile = [](std::vector<int> values,
                                    const double quantile) {
    if (values.empty()) {
      return 0;
    }
    std::sort(values.begin(), values.end());
    const double clamped_quantile = std::clamp(quantile, 0.0, 1.0);
    const size_t index = std::min(
        values.size() - 1,
        static_cast<size_t>(std::llround(
            clamped_quantile * static_cast<double>(values.size() - 1))));
    return values[index];
  };
  auto count_over_threshold = [](const std::vector<int>& values,
                                 const int threshold) {
    return static_cast<int>(
        std::count_if(values.begin(), values.end(), [&](const int value) {
          return value > threshold;
        }));
  };
  std::vector<int> placed_assignment_displacements;
  placed_assignment_displacements.reserve(placed_cells);
  std::vector<int> final_displacements;
  final_displacements.reserve(placed_cells);
  int64_t assignment_target_row_deviation_sum = 0;
  int64_t assignment_target_site_deviation_sum = 0;
  int assignment_target_row_deviation_max = 0;
  int assignment_target_site_deviation_max = 0;
  int assignment_exact_target_count = 0;
  for (int idx = 0; idx < static_cast<int>(cells.size()); ++idx) {
    if (current_rows[idx] < 0 || current_sites[idx] < 0) {
      continue;
    }
    const int row_deviation
        = std::abs(current_rows[idx] - cells[idx].desired_row);
    const int site_deviation
        = std::abs(current_sites[idx] - cells[idx].desired_site);
    assignment_target_row_deviation_sum += row_deviation;
    assignment_target_site_deviation_sum += site_deviation;
    assignment_target_row_deviation_max
        = std::max(assignment_target_row_deviation_max, row_deviation);
    assignment_target_site_deviation_max
        = std::max(assignment_target_site_deviation_max, site_deviation);
    if (row_deviation == 0 && site_deviation == 0) {
      ++assignment_exact_target_count;
    }
    placed_assignment_displacements.push_back(
        assignment_displacement_sites[idx]);
    final_displacements.push_back(
        displacement_sites(cells[idx], current_rows[idx], current_sites[idx]));
  }
  const int assignment_p90_disp_sites
      = displacement_percentile(placed_assignment_displacements, 0.90);
  const int assignment_p99_disp_sites
      = displacement_percentile(placed_assignment_displacements, 0.99);
  const int final_p90_disp_sites
      = displacement_percentile(final_displacements, 0.90);
  const int final_p99_disp_sites
      = displacement_percentile(final_displacements, 0.99);
  const int assignment_max_disp_sites
      = placed_assignment_displacements.empty()
            ? 0
            : *std::max_element(placed_assignment_displacements.begin(),
                                placed_assignment_displacements.end());
  const int final_max_disp_sites
      = final_displacements.empty()
            ? 0
            : *std::max_element(final_displacements.begin(),
                                final_displacements.end());
  const int assignment_tail_count = count_over_threshold(
      placed_assignment_displacements, max_disp_threshold_sites);
  const int assignment_extreme_tail_count = count_over_threshold(
      placed_assignment_displacements, 2 * max_disp_threshold_sites);
  const int final_tail_count
      = count_over_threshold(final_displacements, max_disp_threshold_sites);
  const int final_extreme_tail_count
      = count_over_threshold(final_displacements, 2 * max_disp_threshold_sites);

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
  logger_->metric("dpl_evolve__legalm_full__row_escape_budget",
                  row_escape_budget);
  logger_->metric("dpl_evolve__legalm_full__row_escape_budget_used",
                  row_escape_budget_used);
  logger_->metric("dpl_evolve__legalm_full__row_escape_budget_rejects",
                  row_escape_budget_rejects);
  logger_->metric(
      "dpl_evolve__legalm_full__row_escape_budget_pressure_overflows",
      row_escape_budget_pressure_overflows);
  logger_->metric("dpl_evolve__legalm_full__target_row_accepts",
                  assignment_target_row_accepts);
  logger_->metric("dpl_evolve__legalm_full__target_nearby_accepts",
                  assignment_target_nearby_accepts);
  logger_->metric("dpl_evolve__legalm_full__partition_escape_accepts",
                  assignment_pressure_escape_accepts);
  logger_->metric("dpl_evolve__legalm_full__hpwl_escape_accepts",
                  assignment_hpwl_escape_accepts);
  logger_->metric("dpl_evolve__legalm_full__hpwl_escape_candidates",
                  static_cast<double>(assignment_hpwl_escape_candidates));
  logger_->metric("dpl_evolve__legalm_full__partition_escape_candidates",
                  static_cast<double>(assignment_pressure_escape_candidates));
  logger_->metric("dpl_evolve__legalm_full__low_residual_escape_rejects",
                  static_cast<double>(assignment_low_residual_escape_rejects));
  logger_->metric("dpl_evolve__legalm_full__tail_guard_rejects",
                  static_cast<double>(assignment_tail_guard_rejects));
  logger_->metric("dpl_evolve__legalm_full__hpwl_threshold_rejects",
                  static_cast<double>(assignment_hpwl_threshold_rejects));
  logger_->metric("dpl_evolve__legalm_full__pressure_relief_sites",
                  static_cast<double>(assignment_pressure_relief_sites));
  logger_->metric("dpl_evolve__legalm_full__pressure_deficit_sites",
                  static_cast<double>(assignment_pressure_deficit_sites));
  logger_->metric("dpl_evolve__legalm_full__hpwl_over_pressure_accepts",
                  static_cast<double>(assignment_hpwl_over_pressure_accepts));
  logger_->metric("dpl_evolve__legalm_full__hpwl_over_pressure_rejects",
                  static_cast<double>(assignment_hpwl_over_pressure_rejects));
  logger_->metric("dpl_evolve__legalm_full__pressure_selected_over_hpwl",
                  static_cast<double>(assignment_pressure_selected_over_hpwl));
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
  logger_->metric("dpl_evolve__legalm_full__low_residual_policy",
                  low_residual_policy ? 1 : 0);
  logger_->metric(
      "dpl_evolve__legalm_full__low_residual_guided_hpwl_assignment",
      low_residual_guided_hpwl_assignment ? 1 : 0);
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_hpwl_projection_enabled",
      assignment_hpwl_projection_enabled ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__pressure_hpwl_escape_policy",
                  pressure_can_spend_hpwl_escape_budget ? 1 : 0);
  logger_->metric("dpl_evolve__legalm_full__target_row_neighborhood",
                  target_row_neighborhood);
  logger_->metric("dpl_evolve__legalm_full__low_residual_site_limit",
                  low_residual_site_limit);
  logger_->metric("dpl_evolve__legalm_full__low_residual_bin_limit",
                  low_residual_bin_limit);
  logger_->metric("dpl_evolve__legalm_full__hpwl_escape_threshold_sites",
                  base_hpwl_escape_threshold);
  logger_->metric("dpl_evolve__legalm_full__assignment_p90_disp_sites",
                  assignment_p90_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__assignment_p99_disp_sites",
                  assignment_p99_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__assignment_max_disp_sites",
                  assignment_max_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__assignment_tail_count",
                  assignment_tail_count);
  logger_->metric("dpl_evolve__legalm_full__assignment_extreme_tail_count",
                  assignment_extreme_tail_count);
  logger_->metric("dpl_evolve__legalm_full__final_p90_disp_sites",
                  final_p90_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__final_p99_disp_sites",
                  final_p99_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__final_max_disp_sites",
                  final_max_disp_sites);
  logger_->metric("dpl_evolve__legalm_full__final_tail_count",
                  final_tail_count);
  logger_->metric("dpl_evolve__legalm_full__final_extreme_tail_count",
                  final_extreme_tail_count);
  logger_->metric("dpl_evolve__legalm_full__paper_alpha_max",
                  kMaxDispTailWeight);
  logger_->metric("dpl_evolve__legalm_full__paper_ptech", kPtech);
  logger_->metric("dpl_evolve__legalm_full__hpwl_proxy_terms",
                  static_cast<double>(total_hpwl_proxy_terms));
  logger_->metric("dpl_evolve__legalm_full__hpwl_proxy_weight",
                  kHpwlRegressionPenaltyWeight);
  logger_->metric("dpl_evolve__legalm_full__hpwl_delta_weight",
                  kHpwlDeltaRewardWeight);
  logger_->metric("dpl_evolve__legalm_full__hpwl_proxy_max_penalty_sites",
                  max_hpwl_regression_penalty_sites);
  logger_->metric("dpl_evolve__legalm_full__extreme_disp_tail_weight",
                  kExtremeDispTailWeight);
  logger_->metric("dpl_evolve__legalm_full__pressure_relief_weight",
                  kPressureReliefRewardWeight);
  logger_->metric("dpl_evolve__legalm_full__pressure_deficit_weight",
                  kPressureDeficitPenaltyWeight);
  logger_->metric("dpl_evolve__legalm_full__pressure_hpwl_switch_base_scale",
                  kPressureHpwlSwitchBaseScale);
  logger_->metric("dpl_evolve__legalm_full__pressure_hpwl_extra_disp_scale",
                  kPressureHpwlExtraDispScale);
  logger_->metric("dpl_evolve__legalm_full__pressure_hpwl_relief_scale",
                  kPressureHpwlReliefScale);
  logger_->metric("dpl_evolve__legalm_full__pressure_hpwl_budget_scale",
                  kPressureHpwlBudgetScale);
  logger_->metric("dpl_evolve__legalm_full__assignment_row_probes",
                  static_cast<double>(assignment_row_probes));
  logger_->metric("dpl_evolve__legalm_full__assignment_anchor_row_probes",
                  static_cast<double>(assignment_anchor_row_probes));
  logger_->metric("dpl_evolve__legalm_full__assignment_interval_candidate_evals",
                  static_cast<double>(assignment_interval_candidate_evals));
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_interval_duplicate_rejects",
      static_cast<double>(assignment_interval_duplicate_rejects));
  logger_->metric("dpl_evolve__legalm_full__assignment_interval_cap_hits",
                  static_cast<double>(assignment_interval_cap_hits));
  logger_->metric("dpl_evolve__legalm_full__assignment_interval_probe_limit",
                  kIntervalNeighborProbeLimit);
  logger_->metric("dpl_evolve__legalm_full__assignment_interval_candidate_cap",
                  kMaxIntervalCandidateSites);
  logger_->metric("dpl_evolve__legalm_full__assignment_max_interval_candidates",
                  assignment_max_interval_candidates);
  logger_->metric("dpl_evolve__legalm_full__assignment_hpwl_projection_terms",
                  static_cast<double>(assignment_hpwl_projection_terms));
  logger_->metric("dpl_evolve__legalm_full__assignment_hpwl_projection_sites",
                  static_cast<double>(assignment_hpwl_projection_sites));
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_hpwl_projection_shadow_sites",
      static_cast<double>(assignment_hpwl_projection_shadow_sites));
  logger_->metric("dpl_evolve__legalm_full__assignment_hpwl_projection_accepts",
                  static_cast<double>(assignment_hpwl_projection_accepts));
  logger_->metric("dpl_evolve__legalm_full__assignment_target_exact_count",
                  assignment_exact_target_count);
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_target_avg_row_deviation",
      placed_cells == 0
          ? 0.0
          : static_cast<double>(assignment_target_row_deviation_sum)
                / static_cast<double>(placed_cells));
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_target_avg_site_deviation",
      placed_cells == 0
          ? 0.0
          : static_cast<double>(assignment_target_site_deviation_sum)
                / static_cast<double>(placed_cells));
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_target_max_row_deviation",
      assignment_target_row_deviation_max);
  logger_->metric(
      "dpl_evolve__legalm_full__assignment_target_max_site_deviation",
      assignment_target_site_deviation_max);
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
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_enabled",
      low_residual_refinement_enabled);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_frontier",
      low_residual_refinement_frontier);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_attempted",
      low_residual_refinement_attempted);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_moved",
      low_residual_refinement_moved);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_row_moves",
      low_residual_refinement_row_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_evals",
      low_residual_refinement_candidate_evals);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_swap_evals",
      low_residual_refinement_swap_evals);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_swap_moves",
      low_residual_refinement_swap_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_free_moves",
      low_residual_refinement_free_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_frontier_before_cap",
      low_residual_refinement_frontier_before_cap);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_pruned_no_bbox_terms",
      low_residual_refinement_frontier_pruned_no_bbox_terms);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_pruned_low_density",
      low_residual_refinement_frontier_pruned_low_density);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_legal_rejects",
      low_residual_refinement_legal_rejects);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_static_rejects",
      low_residual_refinement_static_rejects);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_tail_rejects",
      low_residual_refinement_tail_rejects);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_hpwl_gain_sites",
      low_residual_refinement_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_disp_gain_sites",
      static_cast<double>(low_residual_refinement_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_terms",
      static_cast<double>(low_residual_current_net_anchor_terms));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_raw_terms",
      static_cast<double>(low_residual_current_net_anchor_raw_terms));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_prefilter_rejects",
      static_cast<double>(
          low_residual_current_net_anchor_prefilter_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_target_rejects",
      static_cast<double>(low_residual_current_net_anchor_target_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_safe_alternates",
      static_cast<double>(
          low_residual_current_net_anchor_safe_alternates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_candidates",
      static_cast<double>(low_residual_current_net_anchor_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_scored",
      static_cast<double>(low_residual_current_net_anchor_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_moves",
      static_cast<double>(low_residual_current_net_anchor_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_hpwl_gain_sites",
      low_residual_current_net_anchor_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_current_net_anchor_disp_gain_sites",
      static_cast<double>(
          low_residual_current_net_anchor_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_frontier",
      static_cast<double>(low_residual_target_correction_frontier));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_candidates",
      static_cast<double>(low_residual_target_correction_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_nearby_probes",
      static_cast<double>(low_residual_target_correction_nearby_probes));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_self_overlap_probes",
      static_cast<double>(low_residual_target_correction_self_overlap_probes));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_prefilter_rejects",
      static_cast<double>(low_residual_target_correction_prefilter_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_potential_evals",
      static_cast<double>(low_residual_target_correction_potential_evals));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_potential_skips",
      static_cast<double>(low_residual_target_correction_potential_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_potential_rejects",
      static_cast<double>(low_residual_target_correction_potential_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_exact_cap_rejects",
      static_cast<double>(low_residual_target_correction_exact_cap_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_scored",
      static_cast<double>(low_residual_target_correction_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_moves",
      static_cast<double>(low_residual_target_correction_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_free_moves",
      static_cast<double>(low_residual_target_correction_free_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_swap_moves",
      static_cast<double>(low_residual_target_correction_swap_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_legal_rejects",
      static_cast<double>(low_residual_target_correction_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_static_rejects",
      static_cast<double>(low_residual_target_correction_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_disp_rejects",
      static_cast<double>(low_residual_target_correction_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_hpwl_rejects",
      static_cast<double>(low_residual_target_correction_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_hpwl_gain_sites",
      low_residual_target_correction_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_disp_gain_sites",
      static_cast<double>(low_residual_target_correction_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_correction_target_gain_sites",
      static_cast<double>(low_residual_target_correction_target_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_frontier",
      static_cast<double>(low_residual_target_release_frontier));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_blockers",
      static_cast<double>(low_residual_target_release_blockers));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_old_slot_probes",
      static_cast<double>(low_residual_target_release_old_slot_probes));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_neighbor_probes",
      static_cast<double>(low_residual_target_release_neighbor_probes));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_candidates",
      static_cast<double>(low_residual_target_release_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_scored",
      static_cast<double>(low_residual_target_release_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_moves",
      static_cast<double>(low_residual_target_release_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_multi_blocker_rejects",
      static_cast<double>(
          low_residual_target_release_multi_blocker_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_exact_cap_rejects",
      static_cast<double>(low_residual_target_release_exact_cap_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_legal_rejects",
      static_cast<double>(low_residual_target_release_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_static_rejects",
      static_cast<double>(low_residual_target_release_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_disp_rejects",
      static_cast<double>(low_residual_target_release_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_hpwl_rejects",
      static_cast<double>(low_residual_target_release_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_exact_calls_avoided",
      static_cast<double>(low_residual_target_release_exact_calls_avoided));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_full_net_scans",
      static_cast<double>(low_residual_target_release_full_net_scans));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_hpwl_gain_sites",
      low_residual_target_release_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_disp_gain_sites",
      static_cast<double>(low_residual_target_release_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_target_release_target_gain_sites",
      static_cast<double>(low_residual_target_release_target_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_exact_calls_avoided",
      low_residual_refinement_exact_calls_avoided);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_fast_delta_calls",
      low_residual_refinement_fast_delta_calls);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_full_net_scans",
      low_residual_refinement_full_net_scans);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_exact_refinement_no_bbox_candidate_skips",
      low_residual_refinement_no_bbox_candidate_skips);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_candidates",
      static_cast<double>(low_residual_chain_candidates));
  logger_->metric("dpl_evolve__legalm_stage3__low_residual_chain_moves",
                  static_cast<double>(low_residual_chain_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_moved_cells",
      static_cast<double>(low_residual_chain_moved_cells));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_legal_rejects",
      static_cast<double>(low_residual_chain_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_static_rejects",
      static_cast<double>(low_residual_chain_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_tail_rejects",
      static_cast<double>(low_residual_chain_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_no_bbox_skips",
      static_cast<double>(low_residual_chain_no_bbox_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_fast_delta_calls",
      static_cast<double>(low_residual_chain_fast_delta_calls));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_exact_calls_avoided",
      static_cast<double>(low_residual_chain_exact_calls_avoided));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_full_net_scans",
      static_cast<double>(low_residual_chain_full_net_scans));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_frontier_regions_skipped",
      static_cast<double>(low_residual_frontier_regions_skipped));
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_hpwl_gain_sites",
      low_residual_chain_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__low_residual_chain_disp_gain_sites",
      static_cast<double>(low_residual_chain_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_refinement_enabled",
      high_pressure_tail_refinement_enabled);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_frontier_before_cap",
      high_pressure_tail_frontier_before_cap);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_frontier",
      high_pressure_tail_frontier);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_attempted",
      high_pressure_tail_attempted);
  logger_->metric("dpl_evolve__legalm_stage3__high_pressure_tail_moved",
                  high_pressure_tail_moved);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_moved_cells",
      high_pressure_tail_moved_cells);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_interval_candidates",
      static_cast<double>(high_pressure_tail_interval_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_swap_candidates",
      static_cast<double>(high_pressure_tail_swap_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_candidates",
      static_cast<double>(high_pressure_tail_chain_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_candidates",
      static_cast<double>(high_pressure_tail_gap_chain_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_interval_moves",
      high_pressure_tail_interval_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_swap_moves",
      high_pressure_tail_swap_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_moves",
      high_pressure_tail_chain_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_moves",
      high_pressure_tail_gap_chain_moves);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_legal_rejects",
      static_cast<double>(high_pressure_tail_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_static_rejects",
      static_cast<double>(high_pressure_tail_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_hpwl_rejects",
      static_cast<double>(high_pressure_tail_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_disp_rejects",
      static_cast<double>(high_pressure_tail_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_tail_rejects",
      static_cast<double>(high_pressure_tail_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_legal_rejects",
      static_cast<double>(high_pressure_tail_chain_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_static_rejects",
      static_cast<double>(high_pressure_tail_chain_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_hpwl_rejects",
      static_cast<double>(high_pressure_tail_chain_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_disp_rejects",
      static_cast<double>(high_pressure_tail_chain_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_chain_tail_rejects",
      static_cast<double>(high_pressure_tail_chain_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_duplicate_skips",
      static_cast<double>(high_pressure_tail_gap_chain_duplicate_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_prefilter_legal_skips",
      static_cast<double>(high_pressure_tail_gap_chain_prefilter_legal_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_prefilter_static_skips",
      static_cast<double>(high_pressure_tail_gap_chain_prefilter_static_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_prefilter_disp_skips",
      static_cast<double>(high_pressure_tail_gap_chain_prefilter_disp_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_gap_chain_prefilter_tail_skips",
      static_cast<double>(high_pressure_tail_gap_chain_prefilter_tail_skips));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_relief_row_anchors",
      static_cast<double>(high_pressure_tail_relief_row_anchors));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_relief_row_scored",
      static_cast<double>(high_pressure_tail_relief_row_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_relief_row_moves",
      static_cast<double>(high_pressure_tail_relief_row_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_relief_row_hpwl_gain_sites",
      high_pressure_tail_relief_row_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_relief_row_disp_gain_sites",
      static_cast<double>(high_pressure_tail_relief_row_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_old_bin_0",
      static_cast<double>(high_pressure_tail_accept_old_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_old_bin_1",
      static_cast<double>(high_pressure_tail_accept_old_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_old_bin_2",
      static_cast<double>(high_pressure_tail_accept_old_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_new_bin_0",
      static_cast<double>(high_pressure_tail_accept_new_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_new_bin_1",
      static_cast<double>(high_pressure_tail_accept_new_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_accept_new_bin_2",
      static_cast<double>(high_pressure_tail_accept_new_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_reservoir_size",
      high_pressure_endpoint_reservoir_size);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_owner_count",
      high_pressure_endpoint_owner_count);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_before_max_disp",
      high_pressure_endpoint_before_max_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_after_max_disp",
      high_pressure_endpoint_after_max_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_before_p99_disp",
      high_pressure_endpoint_before_p99_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_after_p99_disp",
      high_pressure_endpoint_after_p99_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_scored",
      static_cast<double>(high_pressure_endpoint_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_moves",
      static_cast<double>(high_pressure_endpoint_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_legal_rejects",
      static_cast<double>(high_pressure_endpoint_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_static_rejects",
      static_cast<double>(high_pressure_endpoint_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_rejects",
      static_cast<double>(high_pressure_endpoint_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_disp_rejects",
      static_cast<double>(high_pressure_endpoint_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_tail_rejects",
      static_cast<double>(high_pressure_endpoint_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_delta_calls",
      static_cast<double>(high_pressure_endpoint_delta_calls));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_full_net_scans",
      static_cast<double>(high_pressure_endpoint_full_net_scans));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_sites",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_terms",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_terms));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_scored",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_tail_rejects",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_moves",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_rows",
      static_cast<double>(high_pressure_endpoint_hpwl_row_anchor_rows));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_terms",
      static_cast<double>(high_pressure_endpoint_hpwl_row_anchor_terms));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_free_gap_score",
      static_cast<double>(
          high_pressure_endpoint_hpwl_row_anchor_free_gap_score));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_scored",
      static_cast<double>(high_pressure_endpoint_hpwl_row_anchor_scored));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_tail_rejects",
      static_cast<double>(
          high_pressure_endpoint_hpwl_row_anchor_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_moves",
      static_cast<double>(high_pressure_endpoint_hpwl_row_anchor_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_credit_candidates",
      static_cast<double>(high_pressure_endpoint_hpwl_credit_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_credit_moves",
      static_cast<double>(high_pressure_endpoint_hpwl_credit_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_credit_rejects",
      static_cast<double>(high_pressure_endpoint_hpwl_credit_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_gain_sites",
      high_pressure_endpoint_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_loss_sites",
      high_pressure_endpoint_hpwl_loss_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_gain_sites",
      high_pressure_endpoint_hpwl_anchor_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_loss_sites",
      high_pressure_endpoint_hpwl_anchor_loss_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_gain_sites",
      high_pressure_endpoint_hpwl_row_anchor_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_loss_sites",
      high_pressure_endpoint_hpwl_row_anchor_loss_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_credit_gain_sites",
      high_pressure_endpoint_hpwl_credit_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_disp_gain_sites",
      static_cast<double>(high_pressure_endpoint_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_anchor_disp_gain_sites",
      static_cast<double>(high_pressure_endpoint_hpwl_anchor_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_row_anchor_disp_gain_sites",
      static_cast<double>(
          high_pressure_endpoint_hpwl_row_anchor_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_hpwl_credit_disp_gain_sites",
      static_cast<double>(high_pressure_endpoint_hpwl_credit_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_before_bin_0",
      static_cast<double>(high_pressure_endpoint_before_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_before_bin_1",
      static_cast<double>(high_pressure_endpoint_before_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_before_bin_2",
      static_cast<double>(high_pressure_endpoint_before_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_after_bin_0",
      static_cast<double>(high_pressure_endpoint_after_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_after_bin_1",
      static_cast<double>(high_pressure_endpoint_after_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_endpoint_after_bin_2",
      static_cast<double>(high_pressure_endpoint_after_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_owner_count",
      high_pressure_topmax_actual_owner_count);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_owner_count",
      high_pressure_topmax_owner_count);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_before_max_disp",
      high_pressure_topmax_before_max_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_after_max_disp",
      high_pressure_topmax_after_max_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_before_p99_disp",
      high_pressure_topmax_before_p99_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_after_p99_disp",
      high_pressure_topmax_after_p99_disp);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_candidates",
      static_cast<double>(high_pressure_topmax_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_two_cell_candidates",
      static_cast<double>(high_pressure_topmax_two_cell_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_moves",
      static_cast<double>(high_pressure_topmax_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_two_cell_moves",
      static_cast<double>(high_pressure_topmax_two_cell_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_candidates",
      static_cast<double>(high_pressure_topmax_actual_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_two_cell_candidates",
      static_cast<double>(high_pressure_topmax_actual_two_cell_candidates));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_moves",
      static_cast<double>(high_pressure_topmax_actual_moves));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_legal_rejects",
      static_cast<double>(high_pressure_topmax_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_static_rejects",
      static_cast<double>(high_pressure_topmax_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_hpwl_rejects",
      static_cast<double>(high_pressure_topmax_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_disp_rejects",
      static_cast<double>(high_pressure_topmax_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_tail_rejects",
      static_cast<double>(high_pressure_topmax_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_legal_rejects",
      static_cast<double>(high_pressure_topmax_actual_legal_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_static_rejects",
      static_cast<double>(high_pressure_topmax_actual_static_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_hpwl_rejects",
      static_cast<double>(high_pressure_topmax_actual_hpwl_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_disp_rejects",
      static_cast<double>(high_pressure_topmax_actual_disp_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_actual_tail_rejects",
      static_cast<double>(high_pressure_topmax_actual_tail_rejects));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_delta_calls",
      static_cast<double>(high_pressure_topmax_delta_calls));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_full_net_scans",
      static_cast<double>(high_pressure_topmax_full_net_scans));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_hpwl_gain_sites",
      high_pressure_topmax_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_hpwl_loss_sites",
      high_pressure_topmax_hpwl_loss_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_disp_gain_sites",
      static_cast<double>(high_pressure_topmax_disp_gain_sites));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_before_bin_0",
      static_cast<double>(high_pressure_topmax_before_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_before_bin_1",
      static_cast<double>(high_pressure_topmax_before_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_before_bin_2",
      static_cast<double>(high_pressure_topmax_before_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_after_bin_0",
      static_cast<double>(high_pressure_topmax_after_tail_bins[0]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_after_bin_1",
      static_cast<double>(high_pressure_topmax_after_tail_bins[1]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_topmax_after_bin_2",
      static_cast<double>(high_pressure_topmax_after_tail_bins[2]));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_no_bbox_accepts",
      static_cast<double>(high_pressure_tail_no_bbox_accepts));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_fast_delta_calls",
      static_cast<double>(high_pressure_tail_fast_delta_calls));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_exact_calls_avoided",
      static_cast<double>(high_pressure_tail_exact_calls_avoided));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_full_net_scans",
      static_cast<double>(high_pressure_tail_full_net_scans));
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_hpwl_gain_sites",
      high_pressure_tail_hpwl_gain_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_hpwl_loss_sites",
      high_pressure_tail_hpwl_loss_sites);
  logger_->metric(
      "dpl_evolve__legalm_stage3__high_pressure_tail_disp_gain_sites",
      static_cast<double>(high_pressure_tail_disp_gain_sites));
  logger_->info(DPL,
                1227,
                "LEGALM row escape policy low-residual {} (residual {} sites / "
                "{} bins, limits {} / {}), target radius {}, budget {} used {} "
                "(rejects {}, pressure overflows {}), target accepts {} + {}, "
                "partition escapes {}, HPWL escapes {}, tail rejects {}, "
                "threshold rejects {}.",
                low_residual_policy ? 1 : 0,
                legalm_stage2_final_overflow_sites_,
                legalm_stage2_final_overflow_bins_,
                low_residual_site_limit,
                low_residual_bin_limit,
                target_row_neighborhood,
                row_escape_budget,
                row_escape_budget_used,
                row_escape_budget_rejects,
                row_escape_budget_pressure_overflows,
                assignment_target_row_accepts,
                assignment_target_nearby_accepts,
                assignment_pressure_escape_accepts,
                assignment_hpwl_escape_accepts,
                assignment_tail_guard_rejects,
                assignment_hpwl_threshold_rejects);
  logger_->info(DPL,
                1228,
                "LEGALM displacement tail assignment p90/p99 {} / {} sites "
                "(max {}, tail {} extreme {}), final p90/p99 {} / {} sites "
                "(max {}, tail {} extreme {}).",
                assignment_p90_disp_sites,
                assignment_p99_disp_sites,
                assignment_max_disp_sites,
                assignment_tail_count,
                assignment_extreme_tail_count,
                final_p90_disp_sites,
                final_p99_disp_sites,
                final_max_disp_sites,
                final_tail_count,
                final_extreme_tail_count);
  logger_->info(DPL,
                1229,
                "LEGALM pressure/HPWL economics relief {} sites, deficit {} "
                "sites, HPWL-over-pressure accepts {}, rejects {}, pressure "
                "selected over HPWL {}.",
                assignment_pressure_relief_sites,
                assignment_pressure_deficit_sites,
                assignment_hpwl_over_pressure_accepts,
                assignment_hpwl_over_pressure_rejects,
                assignment_pressure_selected_over_hpwl);
  logger_->info(DPL,
                1239,
                "LEGALM assignment HPWL projection enabled {} (low-residual "
                "guided {}), terms {}, sites {} (shadows {}), accepted {}, "
                "exact-target {}, target deviation avg row/site {:.3f}/{:.3f} "
                "max row/site {} / {}.",
                assignment_hpwl_projection_enabled ? 1 : 0,
                low_residual_guided_hpwl_assignment ? 1 : 0,
                assignment_hpwl_projection_terms,
                assignment_hpwl_projection_sites,
                assignment_hpwl_projection_shadow_sites,
                assignment_hpwl_projection_accepts,
                assignment_exact_target_count,
                placed_cells == 0
                    ? 0.0
                    : static_cast<double>(assignment_target_row_deviation_sum)
                          / static_cast<double>(placed_cells),
                placed_cells == 0
                    ? 0.0
                    : static_cast<double>(assignment_target_site_deviation_sum)
                          / static_cast<double>(placed_cells),
                assignment_target_row_deviation_max,
                assignment_target_site_deviation_max);
  logger_->info(DPL,
                1230,
                "LEGALM low-residual exact HPWL refinement enabled {}, "
                "frontier {}, attempted {}, moved {} (row moves {}), evals {}, "
                "accepted free/swap {} / {}, swaps eval {}, "
                "legal/static/tail rejects {}/{}/{}, exact HPWL gain {:.2f} "
                "sites, displacement gain {} sites.",
                low_residual_refinement_enabled,
                low_residual_refinement_frontier,
                low_residual_refinement_attempted,
                low_residual_refinement_moved,
                low_residual_refinement_row_moves,
                low_residual_refinement_candidate_evals,
                low_residual_refinement_free_moves,
                low_residual_refinement_swap_moves,
                low_residual_refinement_swap_evals,
                low_residual_refinement_legal_rejects,
                low_residual_refinement_static_rejects,
                low_residual_refinement_tail_rejects,
                low_residual_refinement_hpwl_gain_sites,
                low_residual_refinement_disp_gain_sites);
  logger_->info(DPL,
                1231,
                "LEGALM low-residual refinement pruning before-cap {}, final "
                "{}, pruned no-bbox/low-density {} / {}, no-bbox candidate "
                "skips {}, fast delta calls {}, exact calls avoided {}, full "
                "net scans {}.",
                low_residual_refinement_frontier_before_cap,
                low_residual_refinement_frontier,
                low_residual_refinement_frontier_pruned_no_bbox_terms,
                low_residual_refinement_frontier_pruned_low_density,
                low_residual_refinement_no_bbox_candidate_skips,
                low_residual_refinement_fast_delta_calls,
                low_residual_refinement_exact_calls_avoided,
                low_residual_refinement_full_net_scans);
  logger_->info(DPL,
                1232,
                "LEGALM low-residual chain compaction candidates {}, accepted "
                "{} (cells {}), legal/static/tail/no-bbox rejects {}/{}/{}/{}, "
                "HPWL gain {:.2f} sites, displacement gain {} sites, frontier "
                "regions skipped {}, fast delta calls {}, exact calls avoided "
                "{}, full net scans {}.",
                low_residual_chain_candidates,
                low_residual_chain_moves,
                low_residual_chain_moved_cells,
                low_residual_chain_legal_rejects,
                low_residual_chain_static_rejects,
                low_residual_chain_tail_rejects,
                low_residual_chain_no_bbox_skips,
                low_residual_chain_hpwl_gain_sites,
                low_residual_chain_disp_gain_sites,
                low_residual_frontier_regions_skipped,
                low_residual_chain_fast_delta_calls,
                low_residual_chain_exact_calls_avoided,
                low_residual_chain_full_net_scans);
  logger_->info(DPL,
                1240,
                "LEGALM low-residual current-net anchors raw terms {}, active "
                "terms {}, prefilter rejects {}, target rejects {}, safe "
                "alternates {}, candidates {}, scored {}, moves {}, HPWL gain "
                "{:.2f} sites, displacement gain {} sites.",
                low_residual_current_net_anchor_raw_terms,
                low_residual_current_net_anchor_terms,
                low_residual_current_net_anchor_prefilter_rejects,
                low_residual_current_net_anchor_target_rejects,
                low_residual_current_net_anchor_safe_alternates,
                low_residual_current_net_anchor_candidates,
                low_residual_current_net_anchor_scored,
                low_residual_current_net_anchor_moves,
                low_residual_current_net_anchor_hpwl_gain_sites,
                low_residual_current_net_anchor_disp_gain_sites);
  logger_->info(DPL,
                1241,
                "LEGALM low-residual target correction frontier {}, "
                "candidates {} (nearby {}, self-overlap {}), scored {}, "
                "moves {} (free/swap {} / {}), "
                "potential eval/skip {} / {}, "
                "rejects prefilter/potential/exact-cap/legal/static/disp/hpwl "
                "{}/{}/{}/{}/{}/{}/{}, "
                "HPWL gain {:.2f} "
                "sites, displacement gain {} sites, target gain {} sites.",
                low_residual_target_correction_frontier,
                low_residual_target_correction_candidates,
                low_residual_target_correction_nearby_probes,
                low_residual_target_correction_self_overlap_probes,
                low_residual_target_correction_scored,
                low_residual_target_correction_moves,
                low_residual_target_correction_free_moves,
                low_residual_target_correction_swap_moves,
                low_residual_target_correction_potential_evals,
                low_residual_target_correction_potential_skips,
                low_residual_target_correction_prefilter_rejects,
                low_residual_target_correction_potential_rejects,
                low_residual_target_correction_exact_cap_rejects,
                low_residual_target_correction_legal_rejects,
                low_residual_target_correction_static_rejects,
                low_residual_target_correction_disp_rejects,
                low_residual_target_correction_hpwl_rejects,
                low_residual_target_correction_hpwl_gain_sites,
                low_residual_target_correction_disp_gain_sites,
                low_residual_target_correction_target_gain_sites);
  logger_->info(DPL,
                1242,
                "LEGALM low-residual target release frontier {}, blockers {}, "
                "old-slot/neighbor probes {} / {}, candidates {}, scored {}, "
                "moves {}, rejects "
                "multi/exact-cap/legal/static/disp/hpwl {}/{}/{}/{}/{}/{}, "
                "exact avoided/full-net scans {} / {}, HPWL gain {:.2f} "
                "sites, displacement gain {} sites, target gain {} sites.",
                low_residual_target_release_frontier,
                low_residual_target_release_blockers,
                low_residual_target_release_old_slot_probes,
                low_residual_target_release_neighbor_probes,
                low_residual_target_release_candidates,
                low_residual_target_release_scored,
                low_residual_target_release_moves,
                low_residual_target_release_multi_blocker_rejects,
                low_residual_target_release_exact_cap_rejects,
                low_residual_target_release_legal_rejects,
                low_residual_target_release_static_rejects,
                low_residual_target_release_disp_rejects,
                low_residual_target_release_hpwl_rejects,
                low_residual_target_release_exact_calls_avoided,
                low_residual_target_release_full_net_scans,
                low_residual_target_release_hpwl_gain_sites,
                low_residual_target_release_disp_gain_sites,
                low_residual_target_release_target_gain_sites);
  logger_->info(DPL,
                1233,
                "LEGALM high-pressure tail refinement enabled {}, frontier "
                "{}/{} attempted {}, moved {} (cells {}, interval/swap/chain "
                "{} / {} / {}), candidates interval/swap/chain {} / {} / {}, "
                "rejects legal/static/hpwl/disp/tail {}/{}/{}/{}/{}, exact "
                "HPWL gain/loss {:.2f}/{:.2f} sites, displacement gain {} "
                "sites, no-bbox accepts {}, fast delta calls {}, exact calls "
                "avoided {}, full net scans {}.",
                high_pressure_tail_refinement_enabled,
                high_pressure_tail_frontier,
                high_pressure_tail_frontier_before_cap,
                high_pressure_tail_attempted,
                high_pressure_tail_moved,
                high_pressure_tail_moved_cells,
                high_pressure_tail_interval_moves,
                high_pressure_tail_swap_moves,
                high_pressure_tail_chain_moves,
                high_pressure_tail_interval_candidates,
                high_pressure_tail_swap_candidates,
                high_pressure_tail_chain_candidates,
                high_pressure_tail_legal_rejects,
                high_pressure_tail_static_rejects,
                high_pressure_tail_hpwl_rejects,
                high_pressure_tail_disp_rejects,
                high_pressure_tail_tail_rejects,
                high_pressure_tail_hpwl_gain_sites,
                high_pressure_tail_hpwl_loss_sites,
                high_pressure_tail_disp_gain_sites,
                high_pressure_tail_no_bbox_accepts,
                high_pressure_tail_fast_delta_calls,
                high_pressure_tail_exact_calls_avoided,
                high_pressure_tail_full_net_scans);
  logger_->info(DPL,
                1234,
                "LEGALM high-pressure tail chain diagnostics gap candidates "
                "{}, gap moves {}, chain rejects legal/static/hpwl/disp/tail "
                "{}/{}/{}/{}/{}, gap prefilter duplicate/legal/static/disp/"
                "tail skips {}/{}/{}/{}/{}.",
                high_pressure_tail_gap_chain_candidates,
                high_pressure_tail_gap_chain_moves,
                high_pressure_tail_chain_legal_rejects,
                high_pressure_tail_chain_static_rejects,
                high_pressure_tail_chain_hpwl_rejects,
                high_pressure_tail_chain_disp_rejects,
                high_pressure_tail_chain_tail_rejects,
                high_pressure_tail_gap_chain_duplicate_skips,
                high_pressure_tail_gap_chain_prefilter_legal_skips,
                high_pressure_tail_gap_chain_prefilter_static_skips,
                high_pressure_tail_gap_chain_prefilter_disp_skips,
                high_pressure_tail_gap_chain_prefilter_tail_skips);
  logger_->info(DPL,
                1235,
                "LEGALM high-pressure tail relief-row diagnostics anchors {}, "
                "scored {}, moves {}, HPWL gain {:.2f} sites, displacement "
                "gain {} sites, accepted tail bins old normal/tail/extreme "
                "{}/{}/{} -> new {}/{}/{}.",
                high_pressure_tail_relief_row_anchors,
                high_pressure_tail_relief_row_scored,
                high_pressure_tail_relief_row_moves,
                high_pressure_tail_relief_row_hpwl_gain_sites,
                high_pressure_tail_relief_row_disp_gain_sites,
                high_pressure_tail_accept_old_tail_bins[0],
                high_pressure_tail_accept_old_tail_bins[1],
                high_pressure_tail_accept_old_tail_bins[2],
                high_pressure_tail_accept_new_tail_bins[0],
                high_pressure_tail_accept_new_tail_bins[1],
                high_pressure_tail_accept_new_tail_bins[2]);
  logger_->info(DPL,
                1236,
                "LEGALM high-pressure endpoint reservoir size {}, owners {}, "
                "moves {}, max/p99 {} / {} -> {} / {}, bins normal/tail/"
                "extreme {}/{}/{} -> {}/{}/{}, rejects legal/static/hpwl/"
                "disp/tail {}/{}/{}/{}/{}, HPWL gain/loss {:.2f}/{:.2f} "
                "sites, displacement gain {} sites, delta/full-net calls "
                "{} / {}.",
                high_pressure_endpoint_reservoir_size,
                high_pressure_endpoint_owner_count,
                high_pressure_endpoint_moves,
                high_pressure_endpoint_before_max_disp,
                high_pressure_endpoint_before_p99_disp,
                high_pressure_endpoint_after_max_disp,
                high_pressure_endpoint_after_p99_disp,
                high_pressure_endpoint_before_tail_bins[0],
                high_pressure_endpoint_before_tail_bins[1],
                high_pressure_endpoint_before_tail_bins[2],
                high_pressure_endpoint_after_tail_bins[0],
                high_pressure_endpoint_after_tail_bins[1],
                high_pressure_endpoint_after_tail_bins[2],
                high_pressure_endpoint_legal_rejects,
                high_pressure_endpoint_static_rejects,
                high_pressure_endpoint_hpwl_rejects,
                high_pressure_endpoint_disp_rejects,
                high_pressure_endpoint_tail_rejects,
                high_pressure_endpoint_hpwl_gain_sites,
                high_pressure_endpoint_hpwl_loss_sites,
                high_pressure_endpoint_disp_gain_sites,
                high_pressure_endpoint_delta_calls,
                high_pressure_endpoint_full_net_scans);
  logger_->info(DPL,
                1237,
                "LEGALM high-pressure top-max/p99 release owners {}/{}, "
                "candidates {} (two-cell {}), moves {} (two-cell {}), "
                "actual-max candidates {} (two-cell {}), moves {}, "
                "max/p99 {} / {} -> {} / {}, bins normal/tail/extreme "
                "{}/{}/{} -> {}/{}/{}, rejects legal/static/hpwl/disp/tail "
                "{}/{}/{}/{}/{}, actual rejects legal/static/hpwl/disp/tail "
                "{}/{}/{}/{}/{}, HPWL gain/loss {:.2f}/{:.2f} sites, "
                "displacement gain {} sites, delta/full-net calls {} / {}.",
                high_pressure_topmax_owner_count,
                high_pressure_topmax_actual_owner_count,
                high_pressure_topmax_candidates,
                high_pressure_topmax_two_cell_candidates,
                high_pressure_topmax_moves,
                high_pressure_topmax_two_cell_moves,
                high_pressure_topmax_actual_candidates,
                high_pressure_topmax_actual_two_cell_candidates,
                high_pressure_topmax_actual_moves,
                high_pressure_topmax_before_max_disp,
                high_pressure_topmax_before_p99_disp,
                high_pressure_topmax_after_max_disp,
                high_pressure_topmax_after_p99_disp,
                high_pressure_topmax_before_tail_bins[0],
                high_pressure_topmax_before_tail_bins[1],
                high_pressure_topmax_before_tail_bins[2],
                high_pressure_topmax_after_tail_bins[0],
                high_pressure_topmax_after_tail_bins[1],
                high_pressure_topmax_after_tail_bins[2],
                high_pressure_topmax_legal_rejects,
                high_pressure_topmax_static_rejects,
                high_pressure_topmax_hpwl_rejects,
                high_pressure_topmax_disp_rejects,
                high_pressure_topmax_tail_rejects,
                high_pressure_topmax_actual_legal_rejects,
                high_pressure_topmax_actual_static_rejects,
                high_pressure_topmax_actual_hpwl_rejects,
                high_pressure_topmax_actual_disp_rejects,
                high_pressure_topmax_actual_tail_rejects,
                high_pressure_topmax_hpwl_gain_sites,
                high_pressure_topmax_hpwl_loss_sites,
                high_pressure_topmax_disp_gain_sites,
                high_pressure_topmax_delta_calls,
                high_pressure_topmax_full_net_scans);
  logger_->info(DPL,
                1238,
                "LEGALM high-pressure endpoint HPWL anchors terms/sites {} / "
                "{}, site scored/tail-rejects/moves {} / {} / {}, site exact "
                "HPWL gain/loss {:.2f}/{:.2f} sites, site displacement gain "
                "{} sites, row terms/rows/free/scored/tail-rejects/moves {} / "
                "{} / {} / {} / {} / {}, row exact HPWL gain/loss {:.2f}/{:.2f} "
                "sites, row displacement gain {} sites, credit candidates/"
                "moves/rejects {} / {} / {}, exact HPWL gain {:.2f} sites, "
                "displacement gain {} sites.",
                high_pressure_endpoint_hpwl_anchor_terms,
                high_pressure_endpoint_hpwl_anchor_sites,
                high_pressure_endpoint_hpwl_anchor_scored,
                high_pressure_endpoint_hpwl_anchor_tail_rejects,
                high_pressure_endpoint_hpwl_anchor_moves,
                high_pressure_endpoint_hpwl_anchor_gain_sites,
                high_pressure_endpoint_hpwl_anchor_loss_sites,
                high_pressure_endpoint_hpwl_anchor_disp_gain_sites,
                high_pressure_endpoint_hpwl_row_anchor_terms,
                high_pressure_endpoint_hpwl_row_anchor_rows,
                high_pressure_endpoint_hpwl_row_anchor_free_gap_score,
                high_pressure_endpoint_hpwl_row_anchor_scored,
                high_pressure_endpoint_hpwl_row_anchor_tail_rejects,
                high_pressure_endpoint_hpwl_row_anchor_moves,
                high_pressure_endpoint_hpwl_row_anchor_gain_sites,
                high_pressure_endpoint_hpwl_row_anchor_loss_sites,
                high_pressure_endpoint_hpwl_row_anchor_disp_gain_sites,
                high_pressure_endpoint_hpwl_credit_candidates,
                high_pressure_endpoint_hpwl_credit_moves,
                high_pressure_endpoint_hpwl_credit_rejects,
                high_pressure_endpoint_hpwl_credit_gain_sites,
                high_pressure_endpoint_hpwl_credit_disp_gain_sites);
  logger_->info(DPL,
                1214,
                "LEGALM full legalization placed {} / {} cells self-legally "
                "(guided {}, row escapes {}, failures {}, stage3 moved {}, "
                "stage3 row moves {}, stage3 evals {}).",
                placed_cells,
                cells.size(),
                guided_placed,
                row_escape_count,
                failed_cells,
                stage3_moved,
                stage3_row_moves,
                stage3_candidate_evals);

  if (ok) {
    reportEvolvePlacementMetrics("legalm_full");
  }
  clearGuidedInitialLocations();
  return ok;
}

}  // namespace dpl_evolve
