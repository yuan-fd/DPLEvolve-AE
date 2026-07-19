// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_global.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <vector>

#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "detailed_generator.h"
#include "detailed_global_legacy.h"
#include "detailed_manager.h"
#include "detailed_mis.h"
#include "dpl_evolve/Opendp.h"
#include "infrastructure/Grid.h"
#include "infrastructure/Objects.h"
#include "infrastructure/network.h"
#include "objective/detailed_hpwl.h"
#include "util/journal.h"
#include "util/utility.h"
#include "utl/Logger.h"

namespace dpl_evolve {

using utl::DPL;

namespace {

struct StickyAcceptanceDecision
{
  double quality_score = 0.0;
  bool sticky_dominant = false;
  bool balanced = false;
  bool fragile_dominant = false;
  bool supported_fragile = false;
  bool high_delta_override = false;
  bool extreme_fragile = false;
};

StickyAcceptanceDecision classifyStickyDecision(
    const DetailedMgr::StickyMoveProfile& profile,
    const double hpwl_delta,
    const double site_width)
{
  StickyAcceptanceDecision decision;
  const double sticky_bonus = std::max(0.0, profile.quality_adjustment);
  const double fragility_penalty = std::max(0.0, -profile.quality_adjustment);
  decision.sticky_dominant = profile.sticky_nodes > profile.fragile_nodes;
  decision.fragile_dominant = profile.fragile_nodes > profile.sticky_nodes;
  decision.balanced = !decision.sticky_dominant && !decision.fragile_dominant;
  decision.supported_fragile
      = profile.supported_row_changes > profile.unsupported_row_changes
        || profile.protected_segments > 0
        || profile.sticky_neighbor_hits > profile.isolated_hits;
  decision.high_delta_override
      = hpwl_delta >= std::max(10.0 * site_width, 6.0 * fragility_penalty)
        && (hpwl_delta - fragility_penalty) >= (4.0 * site_width);
  decision.extreme_fragile
      = decision.fragile_dominant
        && profile.fragment_risk
               >= (profile.cohesion
                   + (decision.supported_fragile ? 2.6 : 1.9))
        && profile.isolated_hits >= profile.supported_row_changes;

  double capped_penalty = fragility_penalty;
  if (decision.fragile_dominant) {
    const double cap_ratio = decision.extreme_fragile
                                 ? 0.46
                                 : (decision.supported_fragile ? 0.18 : 0.28);
    capped_penalty = std::min(capped_penalty, cap_ratio * hpwl_delta);
    if (decision.high_delta_override) {
      capped_penalty *= decision.supported_fragile ? 0.35 : 0.55;
    }
  }
  decision.quality_score = hpwl_delta + sticky_bonus - capped_penalty;
  return decision;
}

struct SameSegmentAssignmentPlan
{
  std::vector<Node*> ordered_nodes;
  std::vector<DbuX> target_left;
  DbuX left_limit;
  DbuX right_limit;
  int seg_id = -1;
  double priority = 0.0;
};

struct SameSegmentAssignmentSearchStats
{
  int bundles = 0;
  int solves = 0;
};

constexpr int64_t kSameSegmentAssignmentInvalidCost
    = std::numeric_limits<int64_t>::max() / 4;

std::vector<std::pair<int, int>> collectSameSegmentBundleRanges(
    const int current_idx,
    const int anchor_idx,
    const int seg_size)
{
  constexpr int kMaxAssignmentCells = 4;
  constexpr int kMaxBundleStartsPerSize = 3;
  std::vector<std::pair<int, int>> bundles;
  if (seg_size < 2 || current_idx < 0 || current_idx >= seg_size) {
    return bundles;
  }

  auto add_bundle = [&](const int start, const int size) {
    if (size < 2 || size > kMaxAssignmentCells || start < 0
        || start + size > seg_size || current_idx < start
        || current_idx >= start + size) {
      return;
    }
    const std::pair<int, int> range{start, start + size - 1};
    if (std::find(bundles.begin(), bundles.end(), range) == bundles.end()) {
      bundles.push_back(range);
    }
  };

  for (int size = 2; size <= std::min(kMaxAssignmentCells, seg_size); ++size) {
    const int min_start = std::max(0, current_idx - size + 1);
    const int max_start = std::min(current_idx, seg_size - size);
    std::vector<std::pair<int, double>> ranked_starts;
    ranked_starts.reserve(std::max(0, max_start - min_start + 1));
    for (int start = min_start; start <= max_start; ++start) {
      const int end = start + size - 1;
      const int span_to_anchor
          = anchor_idx < start ? start - anchor_idx
                               : (anchor_idx > end ? anchor_idx - end : 0);
      const double center = 0.5 * static_cast<double>(start + end);
      const double score = (2.0 * span_to_anchor)
                           + std::abs(center - anchor_idx)
                           + (0.35 * std::abs(start - current_idx));
      ranked_starts.emplace_back(start, score);
    }
    std::stable_sort(ranked_starts.begin(),
                     ranked_starts.end(),
                     [](const std::pair<int, double>& lhs,
                        const std::pair<int, double>& rhs) {
                       if (lhs.second != rhs.second) {
                         return lhs.second < rhs.second;
                       }
                       return lhs.first < rhs.first;
                     });
    const int keep = std::min(kMaxBundleStartsPerSize,
                              static_cast<int>(ranked_starts.size()));
    for (int i = 0; i < keep; ++i) {
      add_bundle(ranked_starts[i].first, size);
    }
  }

  std::stable_sort(
      bundles.begin(),
      bundles.end(),
      [anchor_idx](const std::pair<int, int>& lhs,
                   const std::pair<int, int>& rhs) {
        const int lhs_span_to_anchor
            = anchor_idx < lhs.first ? lhs.first - anchor_idx
                                     : (anchor_idx > lhs.second
                                            ? anchor_idx - lhs.second
                                            : 0);
        const int rhs_span_to_anchor
            = anchor_idx < rhs.first ? rhs.first - anchor_idx
                                     : (anchor_idx > rhs.second
                                            ? anchor_idx - rhs.second
                                            : 0);
        if (lhs_span_to_anchor != rhs_span_to_anchor) {
          return lhs_span_to_anchor < rhs_span_to_anchor;
        }
        const int lhs_size = lhs.second - lhs.first;
        const int rhs_size = rhs.second - rhs.first;
        if (lhs_size != rhs_size) {
          return lhs_size < rhs_size;
        }
        return lhs.first < rhs.first;
      });
  return bundles;
}

bool sameAssignmentPlan(const SameSegmentAssignmentPlan& lhs,
                        const SameSegmentAssignmentPlan& rhs)
{
  if (lhs.seg_id != rhs.seg_id
      || lhs.ordered_nodes.size() != rhs.ordered_nodes.size()
      || lhs.target_left.size() != rhs.target_left.size()) {
    return false;
  }
  for (size_t i = 0; i < lhs.ordered_nodes.size(); ++i) {
    if (lhs.ordered_nodes[i] != rhs.ordered_nodes[i]
        || lhs.target_left[i] != rhs.target_left[i]) {
      return false;
    }
  }
  return lhs.left_limit == rhs.left_limit && lhs.right_limit == rhs.right_limit;
}

std::vector<SameSegmentAssignmentPlan> buildSameSegmentAssignmentPlans(
    DetailedMgr* mgr,
    Architecture* arch,
    Node* ndi,
    const int source_seg_id,
    const int target_left,
    const double live_criticality,
    SameSegmentAssignmentSearchStats* search_stats = nullptr)
{
  std::vector<SameSegmentAssignmentPlan> plans;
  if (mgr == nullptr || arch == nullptr || ndi == nullptr || source_seg_id < 0
      || source_seg_id >= mgr->getNumSegments()) {
    return plans;
  }

  const int hot_hits = mgr->getAcceptedHotSegmentHits(source_seg_id);
  const double hot_score = mgr->getAcceptedHotSegmentScore(source_seg_id);
  if (hot_hits <= 0 || hot_score <= 0.0) {
    return plans;
  }

  const auto& seg_cells = mgr->getCellsInSeg(source_seg_id);
  if (seg_cells.size() < 2) {
    return plans;
  }

  const auto current_it = std::find(seg_cells.begin(), seg_cells.end(), ndi);
  if (current_it == seg_cells.end()) {
    return plans;
  }
  const int current_idx = static_cast<int>(current_it - seg_cells.begin());
  const int width = ndi->getWidth().v;
  const DbuX target_query{target_left + (width / 2)};
  const auto anchor_it = std::lower_bound(
      seg_cells.begin(),
      seg_cells.end(),
      target_query,
      [](Node* const& node, const DbuX query) {
        return node->getCenterX() < query;
      });
  const int anchor_idx = anchor_it == seg_cells.end()
                             ? static_cast<int>(seg_cells.size()) - 1
                             : static_cast<int>(anchor_it - seg_cells.begin());
  const DetailedSeg* source_seg = mgr->getSegment(source_seg_id);
  if (source_seg == nullptr) {
    return plans;
  }
  const int site_width = std::max(1, arch->getRow(0)->getSiteWidth().v);
  const int row_id = source_seg->getRowId();
  const DbuY row_bottom = arch->getRow(row_id)->getBottom();
  const std::vector<std::pair<int, int>> bundle_ranges
      = collectSameSegmentBundleRanges(
          current_idx, anchor_idx, static_cast<int>(seg_cells.size()));
  if (search_stats != nullptr) {
    search_stats->bundles += static_cast<int>(bundle_ranges.size());
  }

  constexpr int kMaxSolutionsPerBundle = 2;
  constexpr size_t kMaxAssignmentPlans = 4;
  for (const auto& [bundle_start, bundle_end] : bundle_ranges) {
    std::vector<Node*> bundle_nodes(seg_cells.begin() + bundle_start,
                                    seg_cells.begin() + bundle_end + 1);
    if (bundle_nodes.size() < 2) {
      continue;
    }

    const Node* left_neighbor
        = bundle_start > 0 ? seg_cells[bundle_start - 1] : nullptr;
    const Node* right_neighbor
        = bundle_end + 1 < static_cast<int>(seg_cells.size())
              ? seg_cells[bundle_end + 1]
              : nullptr;
    const DbuX coarse_left_limit
        = left_neighbor == nullptr ? source_seg->getMinX()
                                   : left_neighbor->getRight();
    const DbuX coarse_right_limit
        = right_neighbor == nullptr ? source_seg->getMaxX()
                                    : right_neighbor->getLeft();
    if (coarse_right_limit <= coarse_left_limit) {
      continue;
    }

    const int current_local_idx = current_idx - bundle_start;
    const int desired_shift
        = target_left - bundle_nodes[current_local_idx]->getLeft().v;
    const int min_shift
        = coarse_left_limit.v - bundle_nodes.front()->getLeft().v;
    const int max_shift
        = coarse_right_limit.v - bundle_nodes.back()->getRight().v;
    const int clamped_shift = std::clamp(desired_shift, min_shift, max_shift);

    std::vector<DbuX> slot_left(bundle_nodes.size(), DbuX{0});
    std::vector<std::vector<int64_t>> cost_matrix(
        bundle_nodes.size(),
        std::vector<int64_t>(bundle_nodes.size(),
                             kSameSegmentAssignmentInvalidCost));
    int64_t baseline_cost = 0;
    bool valid_cost_matrix = true;
    for (size_t slot = 0; slot < bundle_nodes.size(); ++slot) {
      slot_left[slot] = bundle_nodes[slot]->getLeft() + DbuX{clamped_shift};
    }
    for (size_t row = 0; row < bundle_nodes.size() && valid_cost_matrix; ++row) {
      Node* node = bundle_nodes[row];
      const DbuX preferred_left
          = node == ndi ? DbuX{target_left}
                        : (node->getLeft() + DbuX{clamped_shift});
      for (size_t slot = 0; slot < bundle_nodes.size(); ++slot) {
        const uint64_t hpwl_cost
            = estimateNodeHpwlAt(node, slot_left[slot], row_bottom, 100);
        const int64_t preferred_penalty
            = std::abs((slot_left[slot] - preferred_left).v);
        const int64_t permutation_penalty
            = static_cast<int64_t>(std::abs(static_cast<int>(slot)
                                            - static_cast<int>(row)))
              * static_cast<int64_t>(site_width);
        if (hpwl_cost
                >= static_cast<uint64_t>(kSameSegmentAssignmentInvalidCost)
            || preferred_penalty
                   >= kSameSegmentAssignmentInvalidCost - permutation_penalty
            || static_cast<int64_t>(hpwl_cost)
                   >= kSameSegmentAssignmentInvalidCost - preferred_penalty
                                                  - permutation_penalty) {
          valid_cost_matrix = false;
          break;
        }
        cost_matrix[row][slot] = static_cast<int64_t>(hpwl_cost)
                                 + preferred_penalty + permutation_penalty;
      }
      if (!valid_cost_matrix
          || cost_matrix[row][row]
                 >= kSameSegmentAssignmentInvalidCost - baseline_cost) {
        valid_cost_matrix = false;
        break;
      }
      baseline_cost += cost_matrix[row][row];
    }
    if (!valid_cost_matrix) {
      continue;
    }

    std::vector<AssignmentSolution> solutions;
    if (!collectTopAssignmentSolutions(
            cost_matrix, kMaxSolutionsPerBundle, solutions)) {
      continue;
    }
    if (search_stats != nullptr) {
      ++search_stats->solves;
    }

    for (const AssignmentSolution& solution : solutions) {
      SameSegmentAssignmentPlan plan;
      plan.seg_id = source_seg_id;
      plan.ordered_nodes.assign(bundle_nodes.size(), nullptr);
      plan.target_left.assign(bundle_nodes.size(), DbuX{0});
      bool valid_solution = true;
      for (size_t row = 0; row < bundle_nodes.size(); ++row) {
        const int slot = solution.assignment[row];
        if (slot < 0 || slot >= static_cast<int>(bundle_nodes.size())
            || plan.ordered_nodes[slot] != nullptr) {
          valid_solution = false;
          break;
        }
        plan.ordered_nodes[slot] = bundle_nodes[row];
        plan.target_left[slot] = slot_left[slot];
      }
      if (!valid_solution || std::find(plan.ordered_nodes.begin(),
                                       plan.ordered_nodes.end(),
                                       nullptr)
                                 != plan.ordered_nodes.end()) {
        continue;
      }

      plan.left_limit
          = left_neighbor == nullptr
                ? source_seg->getMinX()
                      + arch->getCellSpacing(nullptr, plan.ordered_nodes.front())
                : left_neighbor->getRight()
                      + arch->getCellSpacing(left_neighbor,
                                             plan.ordered_nodes.front());
      plan.right_limit
          = right_neighbor == nullptr
                ? source_seg->getMaxX()
                      - arch->getCellSpacing(plan.ordered_nodes.back(), nullptr)
                : right_neighbor->getLeft()
                      - arch->getCellSpacing(plan.ordered_nodes.back(),
                                             right_neighbor);
      if (plan.right_limit <= plan.left_limit) {
        continue;
      }

      const double hit_bonus
          = 0.36 * static_cast<double>(std::min(5, hot_hits)) * site_width;
      const double score_bonus
          = 0.08 * std::min(12.0, hot_score / site_width) * site_width;
      const double approx_gain
          = static_cast<double>(baseline_cost - solution.cost);
      const double bundle_penalty
          = 0.15 * static_cast<double>(bundle_nodes.size() - 2) * site_width;
      plan.priority = -approx_gain - (0.12 * live_criticality) - hit_bonus
                      - score_bonus + bundle_penalty;
      if (std::find_if(plans.begin(),
                       plans.end(),
                       [&](const SameSegmentAssignmentPlan& existing) {
                         return sameAssignmentPlan(existing, plan);
                       })
          == plans.end()) {
        plans.push_back(std::move(plan));
      }
    }
  }

  std::stable_sort(plans.begin(),
                   plans.end(),
                   [](const SameSegmentAssignmentPlan& lhs,
                      const SameSegmentAssignmentPlan& rhs) {
                     if (lhs.priority != rhs.priority) {
                       return lhs.priority < rhs.priority;
                     }
                     return lhs.ordered_nodes.size() < rhs.ordered_nodes.size();
                   });
  if (plans.size() > kMaxAssignmentPlans) {
    plans.resize(kMaxAssignmentPlans);
  }
  return plans;
}

}  // namespace

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap(Architecture* arch, Network* network)
    : DetailedGenerator("global swap"),
      mgr_(nullptr),
      arch_(arch),
      network_(network),
      skipNetsLargerThanThis_(100),
      traversal_(0),
      attempts_(0),
      moves_(0),
      swaps_(0)
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap() : DetailedGlobalSwap(nullptr, nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr, const std::string& command)
{
  // A temporary interface to allow for a string which we will decode to create
  // the arguments.
  boost::char_separator<char> separators(" \r\t\n;");
  boost::tokenizer<boost::char_separator<char>> tokens(command, separators);
  std::vector<std::string> args;
  for (const auto& token : tokens) {
    args.push_back(token);
  }
  run(mgrPtr, args);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr,
                             std::vector<std::string>& args)
{
  // Two-pass budget-constrained congestion-aware optimization using
  // Journal-based state management

  mgr_ = mgrPtr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  swap_params_ = &mgr_->getGlobalSwapParams();
  const GlobalSwapParams& params = *swap_params_;

  int passes = params.passes;
  double tol = params.tolerance;
  tradeoff_ = params.tradeoff;

  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    } else if (args[i] == "-x" && i + 1 < args.size()) {
      tradeoff_ = std::atof(args[++i].c_str());
    }
  }
  passes = std::max(passes, 1);
  tol = std::max(tol, 0.01);
  tradeoff_ = std::max(0.0, std::min(1.0, tradeoff_));  // Clamp to [0.0, 1.0]

  uint64_t hpwl_x, hpwl_y;
  int64_t init_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  if (init_hpwl == 0) {
    return;
  }
  mgr_->clearStickyExactNodes();
  mgr_->clearAcceptedHotSegments();

  if (mgr_->hasFocusedSegments() || mgr_->getNumFocusedNodes() > 0) {
    focusedSourceEdgeSwap(std::min(2, passes), tol);
    return;
  }

  const int row_height = arch_->getRow(0)->getHeight().v;

  // Store original displacement limits for restoration later.
  // Note: DetailedMgr stores displacement limits in DBU, while
  // setMaxDisplacement expects values in "sites" (scaled by row height).
  // Convert to keep units consistent and avoid overflow.
  int orig_disp_x_dbu, orig_disp_y_dbu;
  mgr_->getMaxDisplacement(orig_disp_x_dbu, orig_disp_y_dbu);
  const int orig_disp_x_sites = std::max(1, orig_disp_x_dbu / row_height);
  const int orig_disp_y_sites = std::max(1, orig_disp_y_dbu / row_height);
  auto compute_stdcell_utilization = [&]() -> double {
    double placeable_area = 0.0;
    for (const auto* row : arch_->getRows()) {
      if (row == nullptr) {
        continue;
      }
      placeable_area += static_cast<double>(row->getNumSites())
                        * static_cast<double>(row->getSiteSpacing().v)
                        * static_cast<double>(row->getHeight().v);
    }
    if (placeable_area <= 0.0) {
      return 0.0;
    }

    double stdcell_area = 0.0;
    for (const auto& node_ptr : network_->getNodes()) {
      Node* node = node_ptr.get();
      if (node == nullptr || node->getType() != Node::Type::CELL) {
        continue;
      }
      if (!node->isStdCell() || node->isFixed()) {
        continue;
      }
      stdcell_area += static_cast<double>(node->getWidth().v)
                      * static_cast<double>(node->getHeight().v);
    }
    return stdcell_area / placeable_area;
  };

  const double stdcell_utilization = compute_stdcell_utilization();
  const float area_weight = static_cast<float>(params.area_weight);
  const float pin_weight = static_cast<float>(params.pin_weight);
  if (mgr_->getGrid() != nullptr) {
    mgr_->getGrid()->computeUtilizationMap(network_, area_weight, pin_weight);
  }

  struct DensityStats
  {
    int valid_pixels = 0;
    double mean = 0.0;
    double p50 = 0.0;
    double p95 = 0.0;
    double p99 = 0.0;
    double frac_gt_080 = 0.0;
    double frac_gt_090 = 0.0;
  };

  auto compute_density_stats = [&]() -> DensityStats {
    DensityStats stats;
    const Grid* grid = mgr_->getGrid();
    if (grid == nullptr) {
      return stats;
    }
    const int rows = grid->getRowCount().v;
    const int cols = grid->getRowSiteCount().v;
    if (rows <= 0 || cols <= 0) {
      return stats;
    }

    constexpr int kBins = 200;
    std::vector<int> hist(kBins, 0);
    int64_t valid = 0;
    int64_t count80 = 0;
    int64_t count90 = 0;
    double sum = 0.0;

    for (GridY y{0}; y < grid->getRowCount(); y++) {
      for (GridX x{0}; x < grid->getRowSiteCount(); x++) {
        const Pixel& pixel = grid->pixel(y, x);
        if (!pixel.is_valid) {
          continue;
        }
        const int idx = (y.v * cols) + x.v;
        float density = grid->getUtilizationDensity(idx);
        density = std::clamp(density, 0.0f, 1.0f);
        sum += density;
        if (density > 0.80f) {
          count80++;
        }
        if (density > 0.90f) {
          count90++;
        }
        const int bin = std::min(
            kBins - 1,
            static_cast<int>(density * static_cast<float>(kBins - 1)));
        hist[bin]++;
        valid++;
      }
    }

    if (valid <= 0) {
      return stats;
    }

    auto percentile_from_hist = [&](const double q) -> double {
      const int64_t target
          = static_cast<int64_t>(std::ceil(q * static_cast<double>(valid)));
      int64_t cum = 0;
      for (int b = 0; b < kBins; b++) {
        cum += hist[b];
        if (cum >= target) {
          return static_cast<double>(b) / static_cast<double>(kBins - 1);
        }
      }
      return 1.0;
    };

    stats.valid_pixels = static_cast<int>(valid);
    stats.mean = sum / static_cast<double>(valid);
    stats.p50 = percentile_from_hist(0.50);
    stats.p95 = percentile_from_hist(0.95);
    stats.p99 = percentile_from_hist(0.99);
    stats.frac_gt_080 = static_cast<double>(count80) / valid;
    stats.frac_gt_090 = static_cast<double>(count90) / valid;
    return stats;
  };

  const DensityStats density_stats = compute_density_stats();

  auto smoothstep
      = [](const double edge0, const double edge1, const double x) -> double {
    if (edge1 <= edge0) {
      return x < edge0 ? 0.0 : 1.0;
    }
    double t = (x - edge0) / (edge1 - edge0);
    t = std::clamp(t, 0.0, 1.0);
    return t * t * (3.0 - 2.0 * t);
  };

  const double util_score = smoothstep(0.70, 0.92, stdcell_utilization);
  const double p95_score = smoothstep(0.55, 0.82, density_stats.p95);
  const double frac90_score = smoothstep(0.01, 0.08, density_stats.frac_gt_090);
  const double hotspot_score = (0.80 * p95_score) + (0.20 * frac90_score);
  const double combined_score = util_score * hotspot_score;

  extra_dpl_intensity_ = std::clamp(combined_score, 0.0, 1.0);
  extra_dpl_alpha_ = extra_dpl_intensity_ * extra_dpl_intensity_;
  const double search_intensity_floor = 0.45;
  const double search_alpha_floor = 0.16;
  const double search_intensity
      = std::max(extra_dpl_intensity_, search_intensity_floor);
  const double search_alpha = std::max(extra_dpl_alpha_, search_alpha_floor);
  const int total_single_height_cells
      = static_cast<int>(mgr_->getSingleHeightCells().size());

  mgr_->getLogger()->info(
      DPL,
      906,
      "Starting exact source-edge global swap setup "
      "(stdcell_util={:.3f}, util_score={:.2f}, utilmap: mean={:.3f} "
      "p95={:.3f} p99={:.3f} frac>0.80={:.3f} frac>0.90={:.3f}, "
      "intensity={:.2f}, alpha={:.3f}, search_floor={:.2f}, "
      "search_intensity={:.2f}, search_alpha={:.3f})",
      stdcell_utilization,
      util_score,
      density_stats.mean,
      density_stats.p95,
      density_stats.p99,
      density_stats.frac_gt_080,
      density_stats.frac_gt_090,
      extra_dpl_intensity_,
      extra_dpl_alpha_,
      search_intensity_floor,
      search_intensity,
      search_alpha);

  const int exact_passes = std::clamp(std::max(1, passes / 5), 1, 2);
  const int budget_from_sampling = static_cast<int>(std::llround(
      params.sampling_moves * (22.0 + (6.0 * search_alpha))));
  const int budget_from_population
      = static_cast<int>(std::llround(0.28 * total_single_height_cells));
  const int source_cell_budget = std::min(
      total_single_height_cells,
      std::max(3500, std::max(budget_from_sampling, budget_from_population)));
  const int top_k = std::clamp(
      6 + static_cast<int>(std::llround(4.0 * search_alpha)), 6, 10);

  tradeoff_ = 0.0;
  allow_random_moves_ = false;
  mgr_->clearReorderFocusSegments();
  mgr_->clearFocusedSegments();

  mgr_->getLogger()->info(DPL,
                          907,
                          "Starting exact source-edge global swap "
                          "(passes={}, cell_budget={}, top_k={}, tol={:.4f})",
                          exact_passes,
                          source_cell_budget,
                          top_k,
                          tol);

  ExactPassStats aggregate_stats;
  int64_t last_hpwl = init_hpwl;
  int64_t curr_hpwl = init_hpwl;
  for (int p = 1; p <= exact_passes; p++) {
    last_hpwl = curr_hpwl;
    const ExactPassStats pass_stats = globalSwap(source_cell_budget, top_k);
    curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
    aggregate_stats.ranked_source_edge_cells = std::max(
        aggregate_stats.ranked_source_edge_cells,
        pass_stats.ranked_source_edge_cells);
    aggregate_stats.selected_source_edge_cells
        += pass_stats.selected_source_edge_cells;
    aggregate_stats.focused_node_seed_cells
        += pass_stats.focused_node_seed_cells;
    aggregate_stats.hot_segment_seed_cells
        += pass_stats.hot_segment_seed_cells;
    aggregate_stats.accepted_hot_segment_seed_cells
        += pass_stats.accepted_hot_segment_seed_cells;
    aggregate_stats.cells_considered += pass_stats.cells_considered;
    aggregate_stats.proposals += pass_stats.proposals;
    aggregate_stats.same_segment_assignment_bundles
        += pass_stats.same_segment_assignment_bundles;
    aggregate_stats.same_segment_assignment_solves
        += pass_stats.same_segment_assignment_solves;
    aggregate_stats.same_segment_assignment_candidates
        += pass_stats.same_segment_assignment_candidates;
    aggregate_stats.exact_scored += pass_stats.exact_scored;
    aggregate_stats.same_segment_assignment_exact_scored
        += pass_stats.same_segment_assignment_exact_scored;
    aggregate_stats.accepts += pass_stats.accepts;
    aggregate_stats.same_segment_assignment_accepts
        += pass_stats.same_segment_assignment_accepts;
    aggregate_stats.accepted_moves += pass_stats.accepted_moves;
    aggregate_stats.accepted_swaps += pass_stats.accepted_swaps;
    aggregate_stats.focus_segments_added += pass_stats.focus_segments_added;
    aggregate_stats.replay_failures += pass_stats.replay_failures;
    aggregate_stats.sticky_nodes += pass_stats.sticky_nodes;
    aggregate_stats.fragile_nodes += pass_stats.fragile_nodes;
    aggregate_stats.fragility_rejects += pass_stats.fragility_rejects;
    aggregate_stats.supported_row_changes += pass_stats.supported_row_changes;
    aggregate_stats.unsupported_row_changes
        += pass_stats.unsupported_row_changes;
    aggregate_stats.sticky_dominant_accepts
        += pass_stats.sticky_dominant_accepts;
    aggregate_stats.balanced_accepts += pass_stats.balanced_accepts;
    aggregate_stats.fragile_dominant_accepts
        += pass_stats.fragile_dominant_accepts;
    aggregate_stats.fragile_override_accepts
        += pass_stats.fragile_override_accepts;
    aggregate_stats.extreme_fragile_rejects
        += pass_stats.extreme_fragile_rejects;
    aggregate_stats.quota_fragile_rejects
        += pass_stats.quota_fragile_rejects;
    aggregate_stats.fragile_override_quota
        += pass_stats.fragile_override_quota;
    aggregate_stats.protected_segments += pass_stats.protected_segments;
    aggregate_stats.runtime_ms += pass_stats.runtime_ms;
    aggregate_stats.accepted_delta += pass_stats.accepted_delta;
    aggregate_stats.same_segment_assignment_accepted_delta
        += pass_stats.same_segment_assignment_accepted_delta;
    aggregate_stats.quality_adjustment += pass_stats.quality_adjustment;
    aggregate_stats.hpwl_change += pass_stats.hpwl_change;
    mgr_->getLogger()->info(DPL,
                            908,
                            "Exact source-edge pass {:d}; hpwl is {:.6e}, "
                            "runtime_ms={}, exact_scored={}, accepts={}, "
                            "accepted_delta={:.2f}, assignment_bundles={}, "
                            "assignment_solves={}, assignment_candidates={}, "
                            "assignment_accepts={}, assignment_delta={:.2f}, "
                            "sticky_nodes={}, fragile_nodes={}, accepted_hot_seeds={}, "
                            "quality_adjustment={:.2f}",
                            p,
                            static_cast<double>(curr_hpwl),
                            pass_stats.runtime_ms,
                            pass_stats.exact_scored,
                            pass_stats.accepts,
                            pass_stats.accepted_delta,
                            pass_stats.same_segment_assignment_bundles,
                            pass_stats.same_segment_assignment_solves,
                            pass_stats.same_segment_assignment_candidates,
                            pass_stats.same_segment_assignment_accepts,
                            pass_stats.same_segment_assignment_accepted_delta,
                            pass_stats.sticky_nodes,
                            pass_stats.fragile_nodes,
                            pass_stats.accepted_hot_segment_seed_cells,
                            pass_stats.quality_adjustment);
    const double pass_improvement
        = last_hpwl == 0
              ? 0.0
              : std::abs(curr_hpwl - last_hpwl)
                    / static_cast<double>(last_hpwl);
    const double accept_ratio = pass_stats.selected_source_edge_cells <= 0
                                    ? 0.0
                                    : pass_stats.accepts
                                          / static_cast<double>(
                                              pass_stats.selected_source_edge_cells);
    const bool continue_due_to_signal
        = p < exact_passes && pass_improvement <= tol
          && (pass_stats.accepted_delta
                  >= (0.5 * tol * static_cast<double>(last_hpwl))
              || accept_ratio >= 0.18);
    if (continue_due_to_signal) {
      mgr_->getLogger()->info(
          DPL,
          913,
          "Continuing exact source-edge passes after {:d} despite tolerance "
          "{:.4f} due to strong signal "
          "(pass_improvement={:.4f}, accept_ratio={:.3f}, accepted_delta={:.2f})",
          p,
          tol,
          pass_improvement,
          accept_ratio,
          pass_stats.accepted_delta);
      continue;
    }
    if (last_hpwl == 0 || pass_improvement <= tol) {
      mgr_->getLogger()->info(DPL,
                              909,
                              "Stopping exact source-edge passes after {:d} "
                              "due to tolerance {:.4f}",
                              p,
                              tol);
      break;
    }
  }

  const double final_improvement
      = (((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__search_intensity", search_intensity);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__search_alpha", search_alpha);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__cell_budget", source_cell_budget);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__top_k", top_k);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__passes",
                            exact_passes);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__ranked_source_edge_cells",
      aggregate_stats.ranked_source_edge_cells);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__selected_source_edge_cells",
      aggregate_stats.selected_source_edge_cells);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__focused_node_seed_cells",
      aggregate_stats.focused_node_seed_cells);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__hot_segment_seed_cells",
      aggregate_stats.hot_segment_seed_cells);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__accepted_hot_segment_seed_cells",
      aggregate_stats.accepted_hot_segment_seed_cells);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__cells_considered",
                            aggregate_stats.cells_considered);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__proposals",
                            aggregate_stats.proposals);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_bundles",
      aggregate_stats.same_segment_assignment_bundles);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_solves",
      aggregate_stats.same_segment_assignment_solves);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_candidates",
      aggregate_stats.same_segment_assignment_candidates);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__exact_scored",
                            aggregate_stats.exact_scored);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_exact_scored",
      aggregate_stats.same_segment_assignment_exact_scored);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__accepts",
                            aggregate_stats.accepts);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_accepts",
      aggregate_stats.same_segment_assignment_accepts);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__accepted_moves",
      aggregate_stats.accepted_moves);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__accepted_swaps",
      aggregate_stats.accepted_swaps);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__focus_segments_added",
      aggregate_stats.focus_segments_added);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__replay_failures",
      aggregate_stats.replay_failures);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__sticky_nodes",
                            aggregate_stats.sticky_nodes);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__fragile_nodes",
                            aggregate_stats.fragile_nodes);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__supported_row_changes",
      aggregate_stats.supported_row_changes);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__unsupported_row_changes",
      aggregate_stats.unsupported_row_changes);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__fragility_rejects",
      aggregate_stats.fragility_rejects);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__sticky_dominant_accepts",
      aggregate_stats.sticky_dominant_accepts);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__balanced_accepts",
                            aggregate_stats.balanced_accepts);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__fragile_dominant_accepts",
      aggregate_stats.fragile_dominant_accepts);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__fragile_override_accepts",
      aggregate_stats.fragile_override_accepts);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__extreme_fragile_rejects",
      aggregate_stats.extreme_fragile_rejects);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__quota_fragile_rejects",
      aggregate_stats.quota_fragile_rejects);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__fragile_override_quota",
      aggregate_stats.fragile_override_quota);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__protected_segments",
      aggregate_stats.protected_segments);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__runtime_ms",
                            aggregate_stats.runtime_ms);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__accepted_delta_dbu",
      aggregate_stats.accepted_delta);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__same_segment_assignment_accepted_delta_dbu",
      aggregate_stats.same_segment_assignment_accepted_delta);
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__quality_adjustment_dbu",
      aggregate_stats.quality_adjustment);
  mgr_->getLogger()->metric("dpl_evolve__exact_source_edge__sticky_cells_live",
                            mgr_->getNumStickyExactNodes());
  mgr_->getLogger()->metric(
      "dpl_evolve__exact_source_edge__sticky_segments_live",
      mgr_->getNumStickyExactSegments());
  mgr_->getLogger()->info(DPL,
                          910,
                          "Exact source-edge global swap complete: "
                          "final HPWL={:.6e}, improvement={:.2f}%, "
                          "ranked_source_edge_cells={}, hot_segment_seeds={}, "
                          "accepted_hot_seeds={}, assignment_bundles={}, "
                          "assignment_solves={}, assignment_candidates={}, "
                          "assignment_accepts={}, assignment_delta={:.2f}, "
                          "exact_scored={}, accepts={}, accepted_delta={:.2f}, "
                          "sticky_nodes={}, fragile_nodes={}, "
                          "fragility_rejects={}, bins sticky/balanced/fragile="
                          "{}/{}/{}, fragile_override_accepts={}, "
                          "sticky_live={}, focus_segments={}",
                          static_cast<double>(curr_hpwl),
                          final_improvement,
                          aggregate_stats.ranked_source_edge_cells,
                          aggregate_stats.hot_segment_seed_cells,
                          aggregate_stats.accepted_hot_segment_seed_cells,
                          aggregate_stats.same_segment_assignment_bundles,
                          aggregate_stats.same_segment_assignment_solves,
                          aggregate_stats.same_segment_assignment_candidates,
                          aggregate_stats.same_segment_assignment_accepts,
                          aggregate_stats.same_segment_assignment_accepted_delta,
                          aggregate_stats.exact_scored,
                          aggregate_stats.accepts,
                          aggregate_stats.accepted_delta,
                          aggregate_stats.sticky_nodes,
                          aggregate_stats.fragile_nodes,
                          aggregate_stats.fragility_rejects,
                          aggregate_stats.sticky_dominant_accepts,
                          aggregate_stats.balanced_accepts,
                          aggregate_stats.fragile_dominant_accepts,
                          aggregate_stats.fragile_override_accepts,
                          mgr_->getNumStickyExactNodes(),
                          mgr_->getNumFocusedSegments());

  // Ensure original displacement limits are fully restored
  mgr_->setMaxDisplacement(orig_disp_x_sites, orig_disp_y_sites);
  mgr_->getLogger()->info(
      DPL,
      924,
      "Final restoration: displacement limits restored to original ({}, {})",
      orig_disp_x_sites,
      orig_disp_y_sites);
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::ExactPassStats DetailedGlobalSwap::globalSwap(
    const int cell_budget,
    const int top_k)
{
  ExactPassStats pass_stats;
  if (swap_params_ == nullptr && mgr_ != nullptr) {
    swap_params_ = &mgr_->getGlobalSwapParams();
  }

  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, 0);

  mgr_->resortSegments();

  struct RankedSourceEdgeCell
  {
    Node* node = nullptr;
    double criticality = 0.0;
    int critical_edge_count = 0;
    bool focused_node_seed = false;
    bool hot_segment_seed = false;
    double accepted_hot_segment_score = 0.0;
    int accepted_hot_segment_hits = 0;
  };

  std::vector<RankedSourceEdgeCell> ranked_source_edge_cells;
  ranked_source_edge_cells.reserve(mgr_->getSingleHeightCells().size());
  std::vector<int> ranked_indices(network_->getNumNodes(), -1);
  auto push_ranked_source_edge = [&](Node* ndi,
                                     const bool focused_node_seed,
                                     const bool hot_segment_seed) {
    if (ndi == nullptr || mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
      return;
    }

    odb::Rect bbox;
    double source_edge_criticality = 0.0;
    int critical_edge_count = 0;
    if (!getRange(ndi, bbox, &source_edge_criticality, &critical_edge_count)) {
      return;
    }

    const int width = ndi->getWidth().v;
    const int height = ndi->getHeight().v;
    const DbuX current_left = ndi->getLeft();
    const DbuY current_bottom = ndi->getBottom();
    const double current_center_x = current_left.v + (0.5 * width);
    const double current_center_y = current_bottom.v + (0.5 * height);
    if (current_center_x >= bbox.xMin() && current_center_x <= bbox.xMax()
        && current_center_y >= bbox.yMin() && current_center_y <= bbox.yMax()) {
      return;
    }

    const double x_violation
        = current_center_x < bbox.xMin()
              ? bbox.xMin() - current_center_x
              : (current_center_x > bbox.xMax() ? current_center_x - bbox.xMax()
                                                : 0.0);
    const double y_violation
        = current_center_y < bbox.yMin()
              ? bbox.yMin() - current_center_y
              : (current_center_y > bbox.yMax() ? current_center_y - bbox.yMax()
                                                : 0.0);
    const double pin_scale
        = 1.0 + (0.10 * std::min(8, ndi->getNumPins()));
    const double bbox_criticality = (x_violation + y_violation) * pin_scale;
    const double criticality
        = bbox_criticality + (1.5 * source_edge_criticality);
    if (criticality <= 0.0) {
      return;
    }

    const int node_id = ndi->getId();
    if (node_id < 0 || node_id >= static_cast<int>(ranked_indices.size())) {
      return;
    }
    const double accepted_hot_segment_score
        = mgr_->getAcceptedHotSegmentScore(ndi);
    const int accepted_hot_segment_hits = mgr_->getAcceptedHotSegmentHits(ndi);
    const int existing_index = ranked_indices[node_id];
    if (existing_index >= 0) {
      RankedSourceEdgeCell& existing = ranked_source_edge_cells[existing_index];
      existing.focused_node_seed
          = existing.focused_node_seed || focused_node_seed;
      existing.hot_segment_seed
          = existing.hot_segment_seed || hot_segment_seed;
      existing.accepted_hot_segment_score = std::max(
          existing.accepted_hot_segment_score, accepted_hot_segment_score);
      existing.accepted_hot_segment_hits
          = std::max(existing.accepted_hot_segment_hits,
                     accepted_hot_segment_hits);
      if (criticality > existing.criticality) {
        existing.criticality = criticality;
        existing.critical_edge_count = critical_edge_count;
      }
      return;
    }

    ranked_indices[node_id] = static_cast<int>(ranked_source_edge_cells.size());
    ranked_source_edge_cells.push_back(RankedSourceEdgeCell{ndi,
                                                            criticality,
                                                            critical_edge_count,
                                                            focused_node_seed,
                                                            hot_segment_seed,
                                                            accepted_hot_segment_score,
                                                            accepted_hot_segment_hits});
  };

  for (Node* ndi : mgr_->getSingleHeightCells()) {
    if (mgr_->isFocusedNode(ndi)) {
      push_ranked_source_edge(ndi, true, false);
    }
  }
  if (mgr_->hasFocusedSegments()) {
    for (int seg_id = 0; seg_id < mgr_->getNumSegments(); ++seg_id) {
      if (!mgr_->isFocusedSegment(seg_id)) {
        continue;
      }
      for (Node* ndi : mgr_->getCellsInSeg(seg_id)) {
        push_ranked_source_edge(ndi, false, true);
      }
    }
  }
  for (Node* ndi : mgr_->getSingleHeightCells()) {
    push_ranked_source_edge(ndi, false, false);
  }
  std::stable_sort(ranked_source_edge_cells.begin(),
                   ranked_source_edge_cells.end(),
                   [](const RankedSourceEdgeCell& lhs,
                      const RankedSourceEdgeCell& rhs) {
                     if (lhs.focused_node_seed != rhs.focused_node_seed) {
                       return lhs.focused_node_seed > rhs.focused_node_seed;
                     }
                     if (lhs.hot_segment_seed != rhs.hot_segment_seed) {
                       return lhs.hot_segment_seed > rhs.hot_segment_seed;
                     }
                     if (lhs.accepted_hot_segment_score
                         != rhs.accepted_hot_segment_score) {
                       return lhs.accepted_hot_segment_score
                              > rhs.accepted_hot_segment_score;
                     }
                     if (lhs.accepted_hot_segment_hits
                         != rhs.accepted_hot_segment_hits) {
                       return lhs.accepted_hot_segment_hits
                              > rhs.accepted_hot_segment_hits;
                     }
                     if (lhs.criticality != rhs.criticality) {
                       return lhs.criticality > rhs.criticality;
                     }
                     if (lhs.critical_edge_count != rhs.critical_edge_count) {
                       return lhs.critical_edge_count > rhs.critical_edge_count;
                     }
                     return lhs.node->getId() < rhs.node->getId();
                   });
  pass_stats.ranked_source_edge_cells
      = static_cast<int>(ranked_source_edge_cells.size());
  if (cell_budget > 0
      && ranked_source_edge_cells.size() > static_cast<size_t>(cell_budget)) {
    ranked_source_edge_cells.resize(cell_budget);
  }
  pass_stats.selected_source_edge_cells
      = static_cast<int>(ranked_source_edge_cells.size());
  for (const RankedSourceEdgeCell& ranked_cell : ranked_source_edge_cells) {
    pass_stats.focused_node_seed_cells += ranked_cell.focused_node_seed ? 1 : 0;
    pass_stats.hot_segment_seed_cells += ranked_cell.hot_segment_seed ? 1 : 0;
    pass_stats.accepted_hot_segment_seed_cells
        += ranked_cell.accepted_hot_segment_hits > 0 ? 1 : 0;
  }
  if (ranked_source_edge_cells.empty()) {
    return pass_stats;
  }

  // Wirelength objective.
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);  // Ignore orientation.

  double currHpwl = hpwlObj.curr();
  const double initHpwl = currHpwl;
  const int site_width = arch_->getRow(0)->getSiteWidth().v;
  auto accepted_hot_segment_bonus = [&](const int seg_id) {
    const double accepted_score = mgr_->getAcceptedHotSegmentScore(seg_id);
    const int accepted_hits = mgr_->getAcceptedHotSegmentHits(seg_id);
    if (accepted_score <= 0.0 || accepted_hits <= 0) {
      return 0.0;
    }
    const double normalized_score
        = std::min(10.0, accepted_score / std::max(1, site_width));
    const double normalized_hits = std::min(5, accepted_hits);
    return static_cast<double>(site_width)
           * std::min(1.8, (0.22 * normalized_hits)
                               + (0.06 * normalized_score));
  };

  struct ExactCandidate
  {
    DbuX left;
    DbuY bottom;
    int seg_id;
    double priority;
    bool same_segment_assignment = false;
    SameSegmentAssignmentPlan assignment_plan;
  };

  const auto pass_start = std::chrono::steady_clock::now();
  const int fragile_override_quota = std::max(96, cell_budget / 24);
  pass_stats.fragile_override_quota = fragile_override_quota;
  int fragile_override_accepts = 0;
  for (const RankedSourceEdgeCell& ranked_cell : ranked_source_edge_cells) {
    Node* ndi = ranked_cell.node;
    ++pass_stats.cells_considered;
    if (ndi == nullptr || mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
      continue;
    }

    const int width = ndi->getWidth().v;
    const int height = ndi->getHeight().v;
    const DbuX current_left = ndi->getLeft();
    const DbuY current_bottom = ndi->getBottom();
    const int source_seg_id = mgr_->getReverseCellToSegs(ndi->getId())[0]
                                  ->getSegId();
    const DetailedSeg* source_seg = mgr_->getSegment(source_seg_id);
    if (source_seg == nullptr) {
      continue;
    }
    const int source_row_id = source_seg->getRowId();

    odb::Rect live_bbox;
    double source_edge_criticality = 0.0;
    int critical_edge_count = 0;
    if (!getRange(ndi,
                  live_bbox,
                  &source_edge_criticality,
                  &critical_edge_count)) {
      continue;
    }

    const double current_center_x = current_left.v + (0.5 * width);
    const double current_center_y = current_bottom.v + (0.5 * height);
    if (current_center_x >= live_bbox.xMin() && current_center_x <= live_bbox.xMax()
        && current_center_y >= live_bbox.yMin()
        && current_center_y <= live_bbox.yMax()) {
      continue;
    }

    const double x_violation
        = current_center_x < live_bbox.xMin()
              ? live_bbox.xMin() - current_center_x
              : (current_center_x > live_bbox.xMax()
                     ? current_center_x - live_bbox.xMax()
                     : 0.0);
    const double y_violation
        = current_center_y < live_bbox.yMin()
              ? live_bbox.yMin() - current_center_y
              : (current_center_y > live_bbox.yMax()
                     ? current_center_y - live_bbox.yMax()
                     : 0.0);
    const int x_dir = current_center_x < live_bbox.xMin()
                          ? 1
                          : (current_center_x > live_bbox.xMax() ? -1 : 0);
    const int y_dir = current_center_y < live_bbox.yMin()
                          ? 1
                          : (current_center_y > live_bbox.yMax() ? -1 : 0);
    double target_center_x = 0.5 * (live_bbox.xMin() + live_bbox.xMax());
    if (x_dir > 0) {
      target_center_x = live_bbox.xMin();
    } else if (x_dir < 0) {
      target_center_x = live_bbox.xMax();
    }
    double target_center_y = 0.5 * (live_bbox.yMin() + live_bbox.yMax());
    if (y_dir > 0) {
      target_center_y = live_bbox.yMin();
    } else if (y_dir < 0) {
      target_center_y = live_bbox.yMax();
    }

    const double pin_scale
        = 1.0 + (0.10 * std::min(8, ndi->getNumPins()));
    const double live_criticality
        = ((x_violation + y_violation) * pin_scale)
          + (1.5 * source_edge_criticality);
    if (live_criticality <= 0.0) {
      continue;
    }

    const int target_left = static_cast<int>(
        std::floor(target_center_x
                   - (0.5 * static_cast<double>(width))));
    const int target_bottom_seed = static_cast<int>(
        std::floor(target_center_y
                   - (0.5 * static_cast<double>(height))));
    const int target_row_id = arch_->find_closest_row(DbuY{target_bottom_seed});
    const int target_row_bottom = arch_->getRow(target_row_id)->getBottom().v;

    int disp_x = 0;
    int disp_y = 0;
    mgr_->getMaxDisplacement(disp_x, disp_y);

    std::vector<int> candidate_rows;
    auto add_row = [&](const int row_id) {
      if (row_id < 0 || row_id >= static_cast<int>(arch_->getRows().size())) {
        return;
      }
      if (std::find(candidate_rows.begin(), candidate_rows.end(), row_id)
          == candidate_rows.end()) {
        candidate_rows.push_back(row_id);
      }
    };
    add_row(target_row_id);
    add_row(source_row_id);
    add_row((target_row_id + source_row_id) / 2);
    add_row(target_row_id - 1);
    add_row(target_row_id + 1);
    add_row(target_row_id - 2);
    add_row(target_row_id + 2);
    add_row(source_row_id - 1);
    add_row(source_row_id + 1);

    std::vector<ExactCandidate> proposals;
    auto execute_candidate = [&](const ExactCandidate& candidate,
                                 bool& used_swap) {
      used_swap = false;
      if (candidate.same_segment_assignment) {
        return mgr_->tryLocalSegmentAssignment(
            candidate.assignment_plan.ordered_nodes,
            candidate.assignment_plan.target_left,
            candidate.assignment_plan.left_limit,
            candidate.assignment_plan.right_limit,
            candidate.assignment_plan.seg_id);
      }
      bool executed = mgr_->tryMove(ndi,
                                    current_left,
                                    current_bottom,
                                    source_seg_id,
                                    candidate.left,
                                    candidate.bottom,
                                    candidate.seg_id);
      if (!executed) {
        executed = mgr_->trySwap(ndi,
                                 current_left,
                                 current_bottom,
                                 source_seg_id,
                                 candidate.left,
                                 candidate.bottom,
                                 candidate.seg_id);
        used_swap = executed;
      }
      return executed;
    };
    auto reject_candidate = [&](const ExactCandidate& candidate) {
      mgr_->rejectMove();
      if (candidate.same_segment_assignment) {
        mgr_->sortCellsInSeg(candidate.assignment_plan.seg_id);
      }
    };
    auto push_candidate = [&](const int left,
                              const int bottom,
                              const int seg_id,
                              const double priority) {
      if (seg_id < 0 || seg_id >= mgr_->getNumSegments()) {
        return;
      }
      const DetailedSeg* seg_ptr = mgr_->getSegment(seg_id);
      const int min_left = seg_ptr->getMinX().v;
      const int max_left = (seg_ptr->getMaxX() - ndi->getWidth()).v;
      if (max_left < min_left) {
        return;
      }
      const int clamped_left = std::clamp(left, min_left, max_left);
      if (seg_id == source_seg_id && bottom == current_bottom.v
          && std::abs(clamped_left - current_left.v) < site_width) {
        return;
      }
      for (auto& candidate : proposals) {
        if (candidate.seg_id == seg_id && candidate.bottom.v == bottom
            && std::abs(candidate.left.v - clamped_left) < site_width) {
          if (priority < candidate.priority) {
            candidate.left = DbuX{clamped_left};
            candidate.priority = priority;
          }
          return;
        }
      }
      ExactCandidate candidate;
      candidate.left = DbuX{clamped_left};
      candidate.bottom = DbuY{bottom};
      candidate.seg_id = seg_id;
      candidate.priority = priority;
      proposals.push_back(std::move(candidate));
    };

    for (const int row_id : candidate_rows) {
      const int row_bottom = arch_->getRow(row_id)->getBottom().v;
      if (std::abs(row_bottom - current_bottom.v) > disp_y) {
        continue;
      }

      struct SegChoice
      {
        int seg_id;
        int row_bottom;
        int min_left;
        int max_left;
        double priority;
      };
      std::vector<SegChoice> row_choices;
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        const int min_left = seg_ptr->getMinX().v;
        const int max_left = (seg_ptr->getMaxX() - ndi->getWidth()).v;
        if (max_left < min_left) {
          continue;
        }
        const int clamped_target = std::clamp(target_left, min_left, max_left);
        const double x_distance
            = std::abs((clamped_target + (0.5 * width))
                       - target_center_x);
        const double row_penalty
            = 2.0 * std::abs(row_bottom - target_row_bottom);
        row_choices.push_back(SegChoice{seg_ptr->getSegId(),
                                        row_bottom,
                                        min_left,
                                        max_left,
                                        x_distance + row_penalty
                                            - accepted_hot_segment_bonus(
                                                seg_ptr->getSegId())});
      }
      if (row_choices.empty()) {
        continue;
      }

      std::sort(row_choices.begin(),
                row_choices.end(),
                [](const SegChoice& lhs, const SegChoice& rhs) {
                  return lhs.priority < rhs.priority;
                });
      if (row_choices.size() > 4) {
        row_choices.resize(4);
      }

      for (const SegChoice& choice : row_choices) {
        push_candidate(target_left,
                       choice.row_bottom,
                       choice.seg_id,
                       choice.priority - (0.05 * live_criticality));
        if (x_dir > 0) {
          push_candidate(choice.min_left,
                         choice.row_bottom,
                         choice.seg_id,
                         choice.priority + (0.20 * site_width));
        } else if (x_dir < 0) {
          push_candidate(choice.max_left,
                         choice.row_bottom,
                         choice.seg_id,
                         choice.priority + (0.20 * site_width));
        } else {
          push_candidate((choice.min_left + choice.max_left) / 2,
                         choice.row_bottom,
                         choice.seg_id,
                          choice.priority + (0.50 * site_width));
        }
      }
    }

    SameSegmentAssignmentSearchStats assignment_search_stats;
    std::vector<SameSegmentAssignmentPlan> assignment_plans
        = buildSameSegmentAssignmentPlans(mgr_,
                                          arch_,
                                          ndi,
                                          source_seg_id,
                                          target_left,
                                          live_criticality,
                                          &assignment_search_stats);
    pass_stats.same_segment_assignment_bundles
        += assignment_search_stats.bundles;
    pass_stats.same_segment_assignment_solves += assignment_search_stats.solves;
    pass_stats.same_segment_assignment_candidates
        += static_cast<int>(assignment_plans.size());
    for (auto& plan : assignment_plans) {
      ExactCandidate assignment_candidate;
      assignment_candidate.left = current_left;
      assignment_candidate.bottom = current_bottom;
      assignment_candidate.seg_id = source_seg_id;
      assignment_candidate.priority = plan.priority;
      assignment_candidate.same_segment_assignment = true;
      assignment_candidate.assignment_plan = std::move(plan);
      proposals.push_back(std::move(assignment_candidate));
    }
    if (proposals.empty()) {
      continue;
    }
    std::sort(proposals.begin(),
              proposals.end(),
              [](const ExactCandidate& lhs, const ExactCandidate& rhs) {
                return lhs.priority < rhs.priority;
              });
    if (proposals.size() > static_cast<size_t>(top_k)) {
      proposals.resize(top_k);
    }
    pass_stats.proposals += static_cast<int>(proposals.size());

    ExactCandidate best_candidate = proposals.front();
    double best_delta = 0.0;
    double best_quality = std::numeric_limits<double>::lowest();
    bool best_is_swap = false;
    bool found_candidate = false;

    for (const ExactCandidate& candidate : proposals) {
      bool used_swap = false;
      const bool executed = execute_candidate(candidate, used_swap);
      if (!executed) {
        continue;
      }

      ++pass_stats.exact_scored;
      pass_stats.same_segment_assignment_exact_scored
          += candidate.same_segment_assignment ? 1 : 0;
      const double hpwl_delta = hpwlObj.delta(mgr_->getJournal());
      const auto sticky_profile = mgr_->scoreStickyMove(mgr_->getJournal());
      const auto decision
          = classifyStickyDecision(sticky_profile, hpwl_delta, site_width);
      reject_candidate(candidate);
      if (hpwl_delta <= 0.0) {
        continue;
      }
      if (!found_candidate || decision.quality_score > best_quality
          || (std::abs(decision.quality_score - best_quality) < 1e-6
              && hpwl_delta > best_delta)) {
        best_delta = hpwl_delta;
        best_quality = decision.quality_score;
        best_candidate = candidate;
        best_is_swap = used_swap;
        found_candidate = true;
      }
    }

    if (!found_candidate || best_delta <= 0.0) {
      continue;
    }

    bool replay_ok = false;
    bool replay_used_swap = false;
    if (best_candidate.same_segment_assignment) {
      replay_ok = mgr_->tryLocalSegmentAssignment(
          best_candidate.assignment_plan.ordered_nodes,
          best_candidate.assignment_plan.target_left,
          best_candidate.assignment_plan.left_limit,
          best_candidate.assignment_plan.right_limit,
          best_candidate.assignment_plan.seg_id);
    } else {
      replay_ok = mgr_->tryMove(ndi,
                                current_left,
                                current_bottom,
                                source_seg_id,
                                best_candidate.left,
                                best_candidate.bottom,
                                best_candidate.seg_id);
      if (!replay_ok) {
        replay_ok = mgr_->trySwap(ndi,
                                  current_left,
                                  current_bottom,
                                  source_seg_id,
                                  best_candidate.left,
                                  best_candidate.bottom,
                                  best_candidate.seg_id);
        replay_used_swap = replay_ok;
      }
    }
    if (!replay_ok) {
      ++pass_stats.replay_failures;
      continue;
    }

    const double replay_delta = hpwlObj.delta(mgr_->getJournal());
    if (replay_delta <= 0.0) {
      reject_candidate(best_candidate);
      ++pass_stats.replay_failures;
      continue;
    }
    const auto replay_profile = mgr_->scoreStickyMove(mgr_->getJournal());
    const auto replay_decision
        = classifyStickyDecision(replay_profile, replay_delta, site_width);
    const bool reject_extreme_fragile
        = replay_decision.extreme_fragile
          && !replay_decision.high_delta_override
          && replay_profile.quality_adjustment < (-0.60 * replay_delta);
    const bool reject_quota_fragile
        = replay_decision.fragile_dominant
          && !replay_decision.high_delta_override
          && fragile_override_accepts >= fragile_override_quota
          && replay_profile.quality_adjustment < (-0.42 * replay_delta);
    if (reject_extreme_fragile || reject_quota_fragile) {
      reject_candidate(best_candidate);
      ++pass_stats.fragility_rejects;
      pass_stats.extreme_fragile_rejects += reject_extreme_fragile ? 1 : 0;
      pass_stats.quota_fragile_rejects += reject_quota_fragile ? 1 : 0;
      continue;
    }

    const size_t focus_before = mgr_->getReorderFocusSegments().size();
    for (const auto& action_ptr : mgr_->getJournal()) {
      if (action_ptr == nullptr
          || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
        continue;
      }
      const auto* move_action
          = static_cast<const MoveCellAction*>(action_ptr.get());
      if (move_action->getNode() != nullptr) {
        mgr_->markFocusedNode(move_action->getNode());
        mgr_->markFocusedSegmentsForNode(move_action->getNode());
      }
      for (const int seg_id : move_action->getOrigSegs()) {
        mgr_->markReorderFocusSegment(seg_id);
        mgr_->markFocusedSegment(seg_id);
      }
      for (const int seg_id : move_action->getNewSegs()) {
        mgr_->markReorderFocusSegment(seg_id);
        mgr_->markFocusedSegment(seg_id);
      }
    }
    pass_stats.focus_segments_added += static_cast<int>(
        mgr_->getReorderFocusSegments().size() - focus_before);

    mgr_->recordStickyExactMove(mgr_->getJournal(), replay_profile);
    mgr_->recordAcceptedHotSegmentJournal(mgr_->getJournal(), replay_delta);
    hpwlObj.accept();
    mgr_->acceptMove();
    currHpwl -= replay_delta;
    ++pass_stats.accepts;
    pass_stats.same_segment_assignment_accepts
        += best_candidate.same_segment_assignment ? 1 : 0;
    pass_stats.accepted_delta += replay_delta;
    pass_stats.same_segment_assignment_accepted_delta
        += best_candidate.same_segment_assignment ? replay_delta : 0.0;
    pass_stats.sticky_nodes += replay_profile.sticky_nodes;
    pass_stats.fragile_nodes += replay_profile.fragile_nodes;
    pass_stats.supported_row_changes += replay_profile.supported_row_changes;
    pass_stats.unsupported_row_changes += replay_profile.unsupported_row_changes;
    pass_stats.protected_segments += replay_profile.protected_segments;
    pass_stats.quality_adjustment += replay_profile.quality_adjustment;
    if (replay_decision.sticky_dominant) {
      ++pass_stats.sticky_dominant_accepts;
    } else if (replay_decision.fragile_dominant) {
      ++pass_stats.fragile_dominant_accepts;
      const bool used_override
          = replay_decision.high_delta_override
            || replay_profile.supported_row_changes
                   > replay_profile.unsupported_row_changes;
      if (used_override) {
        ++fragile_override_accepts;
        ++pass_stats.fragile_override_accepts;
      }
    } else {
      ++pass_stats.balanced_accepts;
    }
    if (replay_used_swap || best_is_swap) {
      ++pass_stats.accepted_swaps;
    } else {
      ++pass_stats.accepted_moves;
    }
  }

  attempts_ += pass_stats.exact_scored;
  moves_ += pass_stats.accepted_moves;
  swaps_ += pass_stats.accepted_swaps;

  pass_stats.runtime_ms
      = std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now() - pass_start)
            .count();
  pass_stats.hpwl_change = initHpwl - currHpwl;
  mgr_->getLogger()->info(
      DPL,
      911,
      "Exact source-edge pass summary: ranked_source_edge_cells={}, "
      "selected_source_edge_cells={}, focused_node_seeds={}, "
      "hot_segment_seeds={}, accepted_hot_seeds={}, cells={}, "
      "proposals={}, assignment_bundles={}, assignment_solves={}, "
      "assignment_candidates={}, assignment_exact_scored={}, "
      "assignment_accepts={}, assignment_delta={:.2f}, "
      "exact_scored={}, accepts={}, accepted_delta={:.2f}, "
      "accepted_moves={}, accepted_swaps={}, focus_segments_added={}, "
      "replay_failures={}, sticky_nodes={}, fragile_nodes={}, "
      "fragility_rejects={}, supported_row_changes={}, "
      "unsupported_row_changes={}, bins sticky/balanced/fragile={}/{}/{}, "
      "fragile_override_accepts={}, extreme_fragile_rejects={}, "
      "quota_fragile_rejects={}, fragile_override_quota={}, "
      "quality_adjustment={:.2f}, runtime_ms={}, hpwl_change={:.2f}",
      pass_stats.ranked_source_edge_cells,
      pass_stats.selected_source_edge_cells,
      pass_stats.focused_node_seed_cells,
      pass_stats.hot_segment_seed_cells,
      pass_stats.accepted_hot_segment_seed_cells,
      pass_stats.cells_considered,
      pass_stats.proposals,
      pass_stats.same_segment_assignment_bundles,
      pass_stats.same_segment_assignment_solves,
      pass_stats.same_segment_assignment_candidates,
      pass_stats.same_segment_assignment_exact_scored,
      pass_stats.same_segment_assignment_accepts,
      pass_stats.same_segment_assignment_accepted_delta,
      pass_stats.exact_scored,
      pass_stats.accepts,
      pass_stats.accepted_delta,
      pass_stats.accepted_moves,
      pass_stats.accepted_swaps,
      pass_stats.focus_segments_added,
      pass_stats.replay_failures,
      pass_stats.sticky_nodes,
      pass_stats.fragile_nodes,
      pass_stats.fragility_rejects,
      pass_stats.supported_row_changes,
      pass_stats.unsupported_row_changes,
      pass_stats.sticky_dominant_accepts,
      pass_stats.balanced_accepts,
      pass_stats.fragile_dominant_accepts,
      pass_stats.fragile_override_accepts,
      pass_stats.extreme_fragile_rejects,
      pass_stats.quota_fragile_rejects,
      pass_stats.fragile_override_quota,
      pass_stats.quality_adjustment,
      pass_stats.runtime_ms,
      pass_stats.hpwl_change);
  return pass_stats;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::focusedSourceEdgeSwap(const int passes,
                                               const double tol)
{
  struct ExactCandidate
  {
    DbuX left;
    DbuY bottom;
    int seg_id;
    double priority;
    bool same_segment_assignment = false;
    SameSegmentAssignmentPlan assignment_plan;
  };

  struct FocusedSeed
  {
    Node* node = nullptr;
    bool focused_node_seed = false;
    bool hot_segment_seed = false;
    double accepted_hot_segment_score = 0.0;
    int accepted_hot_segment_hits = 0;
  };

  struct SeedBundle
  {
    std::vector<FocusedSeed> candidates;
    int focused_node_seed_count = 0;
    int hot_segment_seed_count = 0;
    int accepted_hot_segment_seed_count = 0;
  };

  auto collect_seed_bundle = [&]() -> SeedBundle {
    SeedBundle bundle;
    bundle.candidates.reserve(std::max(
        mgr_->getNumFocusedNodes(), mgr_->getNumFocusedSegments() * 8));
    std::vector<int> seed_indices(network_->getNumNodes(), -1);
    auto add_seed = [&](Node* node,
                        const bool focused_node_seed,
                        const bool hot_segment_seed) {
      if (node == nullptr || node->isFixed() || !node->isStdCell()
          || !node->isPlaced() || arch_->isMultiHeightCell(node)) {
        return;
      }
      const int node_id = node->getId();
      if (node_id < 0 || node_id >= static_cast<int>(seed_indices.size())) {
        return;
      }
      const int existing_index = seed_indices[node_id];
      const double accepted_hot_segment_score
          = mgr_->getAcceptedHotSegmentScore(node);
      const int accepted_hot_segment_hits
          = mgr_->getAcceptedHotSegmentHits(node);
      if (existing_index >= 0) {
        FocusedSeed& existing = bundle.candidates[existing_index];
        existing.focused_node_seed
            = existing.focused_node_seed || focused_node_seed;
        existing.hot_segment_seed
            = existing.hot_segment_seed || hot_segment_seed;
        existing.accepted_hot_segment_score
            = std::max(existing.accepted_hot_segment_score,
                       accepted_hot_segment_score);
        existing.accepted_hot_segment_hits
            = std::max(existing.accepted_hot_segment_hits,
                       accepted_hot_segment_hits);
        return;
      }

      seed_indices[node_id] = static_cast<int>(bundle.candidates.size());
      bundle.candidates.push_back(FocusedSeed{node,
                                              focused_node_seed,
                                              hot_segment_seed,
                                              accepted_hot_segment_score,
                                              accepted_hot_segment_hits});
    };

    for (Node* node : mgr_->getSingleHeightCells()) {
      if (mgr_->isFocusedNode(node)) {
        add_seed(node, true, false);
      }
    }
    if (mgr_->hasFocusedSegments()) {
      for (int seg_id = 0; seg_id < mgr_->getNumSegments(); ++seg_id) {
        if (!mgr_->isFocusedSegment(seg_id)) {
          continue;
        }
        for (Node* node : mgr_->getCellsInSeg(seg_id)) {
          add_seed(node, false, true);
        }
      }
    }

    std::stable_sort(bundle.candidates.begin(),
                     bundle.candidates.end(),
                     [](const FocusedSeed& lhs, const FocusedSeed& rhs) {
                       if (lhs.focused_node_seed != rhs.focused_node_seed) {
                         return lhs.focused_node_seed > rhs.focused_node_seed;
                       }
                       if (lhs.hot_segment_seed != rhs.hot_segment_seed) {
                         return lhs.hot_segment_seed > rhs.hot_segment_seed;
                       }
                       if (lhs.accepted_hot_segment_score
                           != rhs.accepted_hot_segment_score) {
                         return lhs.accepted_hot_segment_score
                                > rhs.accepted_hot_segment_score;
                       }
                       if (lhs.accepted_hot_segment_hits
                           != rhs.accepted_hot_segment_hits) {
                         return lhs.accepted_hot_segment_hits
                                > rhs.accepted_hot_segment_hits;
                       }
                       if (lhs.node->getNumPins() != rhs.node->getNumPins()) {
                         return lhs.node->getNumPins() > rhs.node->getNumPins();
                       }
                       return lhs.node->getId() < rhs.node->getId();
                     });
    constexpr int kMaxFocusedNodes = 2200;
    if (static_cast<int>(bundle.candidates.size()) > kMaxFocusedNodes) {
      bundle.candidates.resize(kMaxFocusedNodes);
    }
    for (const FocusedSeed& seed : bundle.candidates) {
      bundle.focused_node_seed_count += seed.focused_node_seed ? 1 : 0;
      bundle.hot_segment_seed_count += seed.hot_segment_seed ? 1 : 0;
      bundle.accepted_hot_segment_seed_count
          += seed.accepted_hot_segment_hits > 0 ? 1 : 0;
    }
    return bundle;
  };

  const SeedBundle initial_seed_bundle = collect_seed_bundle();
  if (initial_seed_bundle.candidates.empty()) {
    return;
  }

  constexpr int kTopK = 8;
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);
  double curr_hpwl = hpwlObj.curr();
  const double init_hpwl = curr_hpwl;
  const int site_width = arch_->getRow(0)->getSiteWidth().v;

  mgr_->getLogger()->info(DPL,
                          927,
                          "Focused exact source-edge global swap "
                          "(focused_nodes={}, hot_segment_nodes={}, "
                          "accepted_hot_nodes={}, candidates={}, passes={}, top_k={}).",
                          initial_seed_bundle.focused_node_seed_count,
                          initial_seed_bundle.hot_segment_seed_count,
                          initial_seed_bundle.accepted_hot_segment_seed_count,
                          initial_seed_bundle.candidates.size(),
                          passes,
                          kTopK);
  int total_fragile_override_accepts = 0;

  for (int pass = 1; pass <= std::max(1, passes); ++pass) {
    const SeedBundle seed_bundle = collect_seed_bundle();
    if (seed_bundle.candidates.empty()) {
      break;
    }

    const auto pass_start = std::chrono::steady_clock::now();
    const double before_pass_hpwl = curr_hpwl;
    int source_edge_cells = 0;
    int accepted_hot_segment_seed_cells = 0;
    int exact_candidates_generated = 0;
    int exact_candidates_scored = 0;
    int same_segment_assignment_bundles = 0;
    int same_segment_assignment_solves = 0;
    int same_segment_assignment_candidates = 0;
    int same_segment_assignment_exact_scored = 0;
    int exact_accepts = 0;
    int same_segment_assignment_accepts = 0;
    int replay_failures = 0;
    int sticky_nodes = 0;
    int fragile_nodes = 0;
    int fragility_rejects = 0;
    int supported_row_changes = 0;
    int unsupported_row_changes = 0;
    int sticky_dominant_accepts = 0;
    int balanced_accepts = 0;
    int fragile_dominant_accepts = 0;
    int fragile_override_accepts = 0;
    int extreme_fragile_rejects = 0;
    int quota_fragile_rejects = 0;
    int protected_segments = 0;
    double accepted_delta = 0.0;
    double same_segment_assignment_accepted_delta = 0.0;
    double quality_adjustment = 0.0;
    const int fragile_override_quota
        = std::max(24, static_cast<int>(seed_bundle.candidates.size() / 18));
    auto accepted_hot_segment_bonus = [&](const int seg_id) {
      const double accepted_score = mgr_->getAcceptedHotSegmentScore(seg_id);
      const int accepted_hits = mgr_->getAcceptedHotSegmentHits(seg_id);
      if (accepted_score <= 0.0 || accepted_hits <= 0) {
        return 0.0;
      }
      const double normalized_score
          = std::min(10.0, accepted_score / std::max(1, site_width));
      const double normalized_hits = std::min(5, accepted_hits);
      return static_cast<double>(site_width)
             * std::min(2.2, (0.28 * normalized_hits)
                                 + (0.08 * normalized_score));
    };

    for (const FocusedSeed& seed : seed_bundle.candidates) {
      Node* ndi = seed.node;
      if (ndi == nullptr || mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
        continue;
      }
      accepted_hot_segment_seed_cells += seed.accepted_hot_segment_hits > 0 ? 1
                                                                            : 0;

      odb::Rect bbox;
      double source_edge_criticality = 0.0;
      int critical_edge_count = 0;
      if (!getRange(ndi,
                    bbox,
                    &source_edge_criticality,
                    &critical_edge_count)) {
        continue;
      }

      const int width = ndi->getWidth().v;
      const int height = ndi->getHeight().v;
      const DbuX current_left = ndi->getLeft();
      const DbuY current_bottom = ndi->getBottom();
      const int source_seg_id = mgr_->getReverseCellToSegs(ndi->getId())[0]
                                    ->getSegId();
      const DetailedSeg* source_seg = mgr_->getSegment(source_seg_id);
      if (source_seg == nullptr) {
        continue;
      }
      const int source_row_id = source_seg->getRowId();
      const double current_center_x = current_left.v + (0.5 * width);
      const double current_center_y = current_bottom.v + (0.5 * height);
      if (current_center_x >= bbox.xMin() && current_center_x <= bbox.xMax()
          && current_center_y >= bbox.yMin()
          && current_center_y <= bbox.yMax()) {
        continue;
      }
      ++source_edge_cells;

      const double x_violation
          = current_center_x < bbox.xMin()
                ? bbox.xMin() - current_center_x
                : (current_center_x > bbox.xMax()
                       ? current_center_x - bbox.xMax()
                       : 0.0);
      const double y_violation
          = current_center_y < bbox.yMin()
                ? bbox.yMin() - current_center_y
                : (current_center_y > bbox.yMax()
                       ? current_center_y - bbox.yMax()
                       : 0.0);
      const int x_dir = current_center_x < bbox.xMin()
                            ? 1
                            : (current_center_x > bbox.xMax() ? -1 : 0);
      const int y_dir = current_center_y < bbox.yMin()
                            ? 1
                            : (current_center_y > bbox.yMax() ? -1 : 0);
      double target_center_x = 0.5 * (bbox.xMin() + bbox.xMax());
      if (x_dir > 0) {
        target_center_x = bbox.xMin();
      } else if (x_dir < 0) {
        target_center_x = bbox.xMax();
      }
      double target_center_y = 0.5 * (bbox.yMin() + bbox.yMax());
      if (y_dir > 0) {
        target_center_y = bbox.yMin();
      } else if (y_dir < 0) {
        target_center_y = bbox.yMax();
      }

      const double pin_scale
          = 1.0 + (0.10 * std::min(8, ndi->getNumPins()));
      const double live_criticality
          = ((x_violation + y_violation) * pin_scale)
            + (1.5 * source_edge_criticality);
      if (live_criticality <= 0.0) {
        continue;
      }

      const int target_left = static_cast<int>(
          std::floor(target_center_x - (0.5 * static_cast<double>(width))));
      const int target_bottom_seed = static_cast<int>(
          std::floor(target_center_y - (0.5 * static_cast<double>(height))));
      const int target_row_id
          = arch_->find_closest_row(DbuY{target_bottom_seed});
      const int target_row_bottom = arch_->getRow(target_row_id)->getBottom().v;

      int disp_x = 0;
      int disp_y = 0;
      mgr_->getMaxDisplacement(disp_x, disp_y);

      std::vector<int> candidate_rows;
      auto add_row = [&](const int row_id) {
        if (row_id < 0 || row_id >= static_cast<int>(arch_->getRows().size())) {
          return;
        }
        if (std::find(candidate_rows.begin(), candidate_rows.end(), row_id)
            == candidate_rows.end()) {
          candidate_rows.push_back(row_id);
        }
      };
      add_row(target_row_id);
      add_row(source_row_id);
      add_row((target_row_id + source_row_id) / 2);
      add_row(target_row_id - 1);
      add_row(target_row_id + 1);
      add_row(target_row_id - 2);
      add_row(target_row_id + 2);
      add_row(source_row_id - 1);
      add_row(source_row_id + 1);

      std::vector<ExactCandidate> proposals;
      const double seed_bonus = seed.focused_node_seed
                                    ? 0.75 * site_width
                                    : (seed.hot_segment_seed ? 0.35 * site_width
                                                             : 0.0);
      const double accepted_hot_seed_bonus
          = static_cast<double>(site_width)
            * std::min(1.8,
                       (0.22 * std::min(5, seed.accepted_hot_segment_hits))
                           + (0.06
                              * std::min(10.0,
                                         seed.accepted_hot_segment_score
                                             / std::max(1, site_width))));
      const double critical_edge_bonus
          = 0.10 * site_width * std::min(critical_edge_count, 4);
      auto execute_candidate = [&](const ExactCandidate& candidate,
                                   bool& used_swap) {
        used_swap = false;
        if (candidate.same_segment_assignment) {
          return mgr_->tryLocalSegmentAssignment(
              candidate.assignment_plan.ordered_nodes,
              candidate.assignment_plan.target_left,
              candidate.assignment_plan.left_limit,
              candidate.assignment_plan.right_limit,
              candidate.assignment_plan.seg_id);
        }
        bool executed = mgr_->tryMove(ndi,
                                      current_left,
                                      current_bottom,
                                      source_seg_id,
                                      candidate.left,
                                      candidate.bottom,
                                      candidate.seg_id);
        if (!executed) {
          executed = mgr_->trySwap(ndi,
                                   current_left,
                                   current_bottom,
                                   source_seg_id,
                                   candidate.left,
                                   candidate.bottom,
                                   candidate.seg_id);
          used_swap = executed;
        }
        return executed;
      };
      auto reject_candidate = [&](const ExactCandidate& candidate) {
        mgr_->rejectMove();
        if (candidate.same_segment_assignment) {
          mgr_->sortCellsInSeg(candidate.assignment_plan.seg_id);
        }
      };
      auto push_candidate = [&](const int left,
                                const int bottom,
                                const int seg_id,
                                const double priority) {
        if (seg_id < 0 || seg_id >= mgr_->getNumSegments()) {
          return;
        }
        const DetailedSeg* seg_ptr = mgr_->getSegment(seg_id);
        const int min_left = seg_ptr->getMinX().v;
        const int max_left = (seg_ptr->getMaxX() - ndi->getWidth()).v;
        if (max_left < min_left) {
          return;
        }
        const int clamped_left = std::clamp(left, min_left, max_left);
        if (seg_id == source_seg_id && bottom == current_bottom.v
            && std::abs(clamped_left - current_left.v) < site_width) {
          return;
        }
        for (auto& candidate : proposals) {
          if (candidate.seg_id == seg_id && candidate.bottom.v == bottom
              && std::abs(candidate.left.v - clamped_left) < site_width) {
            if (priority < candidate.priority) {
              candidate.left = DbuX{clamped_left};
              candidate.priority = priority;
            }
            return;
          }
        }
        ExactCandidate candidate;
        candidate.left = DbuX{clamped_left};
        candidate.bottom = DbuY{bottom};
        candidate.seg_id = seg_id;
        candidate.priority = priority;
        proposals.push_back(std::move(candidate));
      };

      for (const int row_id : candidate_rows) {
        const int row_bottom = arch_->getRow(row_id)->getBottom().v;
        if (std::abs(row_bottom - current_bottom.v) > disp_y) {
          continue;
        }

        struct SegChoice
        {
          int seg_id;
          int row_bottom;
          int min_left;
          int max_left;
          double priority;
        };
        std::vector<SegChoice> row_choices;
        for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
          if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
            continue;
          }
          const int min_left = seg_ptr->getMinX().v;
          const int max_left = (seg_ptr->getMaxX() - ndi->getWidth()).v;
          if (max_left < min_left) {
            continue;
          }
          const int clamped_target = std::clamp(target_left, min_left, max_left);
          const double x_distance
              = std::abs((clamped_target + (0.5 * width)) - target_center_x);
          const double row_penalty
              = 2.0 * std::abs(row_bottom - target_row_bottom);
          row_choices.push_back(SegChoice{seg_ptr->getSegId(),
                                          row_bottom,
                                          min_left,
                                          max_left,
                                          x_distance + row_penalty
                                              - accepted_hot_segment_bonus(
                                                  seg_ptr->getSegId())});
        }
        if (row_choices.empty()) {
          continue;
        }

        std::sort(row_choices.begin(),
                  row_choices.end(),
                  [](const SegChoice& lhs, const SegChoice& rhs) {
                    return lhs.priority < rhs.priority;
                  });
        if (row_choices.size() > 4) {
          row_choices.resize(4);
        }

        for (const SegChoice& choice : row_choices) {
          push_candidate(target_left,
                         choice.row_bottom,
                         choice.seg_id,
                         choice.priority - (0.05 * live_criticality)
                             - seed_bonus - accepted_hot_seed_bonus
                             - critical_edge_bonus);
          if (x_dir > 0) {
            push_candidate(choice.min_left,
                           choice.row_bottom,
                           choice.seg_id,
                           choice.priority + (0.20 * site_width));
          } else if (x_dir < 0) {
            push_candidate(choice.max_left,
                           choice.row_bottom,
                           choice.seg_id,
                           choice.priority + (0.20 * site_width));
          } else {
            push_candidate((choice.min_left + choice.max_left) / 2,
                           choice.row_bottom,
                           choice.seg_id,
                           choice.priority + (0.50 * site_width));
          }
        }
      }

      SameSegmentAssignmentSearchStats assignment_search_stats;
      std::vector<SameSegmentAssignmentPlan> assignment_plans
          = buildSameSegmentAssignmentPlans(mgr_,
                                            arch_,
                                            ndi,
                                            source_seg_id,
                                            target_left,
                                            live_criticality,
                                            &assignment_search_stats);
      same_segment_assignment_bundles += assignment_search_stats.bundles;
      same_segment_assignment_solves += assignment_search_stats.solves;
      same_segment_assignment_candidates
          += static_cast<int>(assignment_plans.size());
      for (auto& plan : assignment_plans) {
        ExactCandidate assignment_candidate;
        assignment_candidate.left = current_left;
        assignment_candidate.bottom = current_bottom;
        assignment_candidate.seg_id = source_seg_id;
        assignment_candidate.priority
            = plan.priority - seed_bonus - accepted_hot_seed_bonus
              - critical_edge_bonus;
        assignment_candidate.same_segment_assignment = true;
        assignment_candidate.assignment_plan = std::move(plan);
        proposals.push_back(std::move(assignment_candidate));
      }
      if (proposals.empty()) {
        continue;
      }
      std::sort(proposals.begin(),
                proposals.end(),
                [](const ExactCandidate& lhs, const ExactCandidate& rhs) {
                  return lhs.priority < rhs.priority;
                });
      if (proposals.size() > static_cast<size_t>(kTopK)) {
        proposals.resize(kTopK);
      }
      exact_candidates_generated += proposals.size();

      ExactCandidate best_candidate = proposals.front();
      double best_delta = 0.0;
      double best_quality = std::numeric_limits<double>::lowest();
      bool best_is_swap = false;
      bool found_candidate = false;

      for (const ExactCandidate& candidate : proposals) {
        bool used_swap = false;
        const bool executed = execute_candidate(candidate, used_swap);
        if (!executed) {
          continue;
        }

        ++exact_candidates_scored;
        same_segment_assignment_exact_scored
            += candidate.same_segment_assignment ? 1 : 0;
        const double hpwl_delta = hpwlObj.delta(mgr_->getJournal());
        const auto sticky_profile = mgr_->scoreStickyMove(mgr_->getJournal());
        const auto decision
            = classifyStickyDecision(sticky_profile, hpwl_delta, site_width);
        reject_candidate(candidate);
        if (hpwl_delta <= 0.0) {
          continue;
        }
        if (!found_candidate || decision.quality_score > best_quality
            || (std::abs(decision.quality_score - best_quality) < 1e-6
                && hpwl_delta > best_delta)) {
          best_delta = hpwl_delta;
          best_quality = decision.quality_score;
          best_candidate = candidate;
          best_is_swap = used_swap;
          found_candidate = true;
        }
      }

      if (!found_candidate || best_delta <= 0.0) {
        continue;
      }

      bool replay_used_swap = false;
      const bool replay_ok = execute_candidate(best_candidate, replay_used_swap);
      if (!replay_ok) {
        ++replay_failures;
        continue;
      }

      const double replay_delta = hpwlObj.delta(mgr_->getJournal());
      if (replay_delta <= 0.0) {
        reject_candidate(best_candidate);
        ++replay_failures;
        continue;
      }
      const auto replay_profile = mgr_->scoreStickyMove(mgr_->getJournal());
      const auto replay_decision
          = classifyStickyDecision(replay_profile, replay_delta, site_width);
      const bool reject_extreme_fragile
          = replay_decision.extreme_fragile
            && !replay_decision.high_delta_override
            && replay_profile.quality_adjustment < (-0.60 * replay_delta);
      const bool reject_quota_fragile
          = replay_decision.fragile_dominant
            && !replay_decision.high_delta_override
            && fragile_override_accepts >= fragile_override_quota
            && replay_profile.quality_adjustment < (-0.42 * replay_delta);
      if (reject_extreme_fragile || reject_quota_fragile) {
        reject_candidate(best_candidate);
        ++fragility_rejects;
        extreme_fragile_rejects += reject_extreme_fragile ? 1 : 0;
        quota_fragile_rejects += reject_quota_fragile ? 1 : 0;
        continue;
      }

      for (const auto& action_ptr : mgr_->getJournal()) {
        if (action_ptr == nullptr
            || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
          continue;
        }
        const auto* move_action
            = static_cast<const MoveCellAction*>(action_ptr.get());
        if (move_action->getNode() != nullptr) {
          mgr_->markFocusedNode(move_action->getNode());
          mgr_->markFocusedSegmentsForNode(move_action->getNode());
        }
        for (const int seg_id : move_action->getOrigSegs()) {
          mgr_->markFocusedSegment(seg_id);
        }
        for (const int seg_id : move_action->getNewSegs()) {
          mgr_->markFocusedSegment(seg_id);
        }
      }

      mgr_->recordStickyExactMove(mgr_->getJournal(), replay_profile);
      mgr_->recordAcceptedHotSegmentJournal(mgr_->getJournal(), replay_delta);
      hpwlObj.accept();
      mgr_->acceptMove();
      curr_hpwl -= replay_delta;
      ++exact_accepts;
      same_segment_assignment_accepts
          += best_candidate.same_segment_assignment ? 1 : 0;
      accepted_delta += replay_delta;
      same_segment_assignment_accepted_delta
          += best_candidate.same_segment_assignment ? replay_delta : 0.0;
      sticky_nodes += replay_profile.sticky_nodes;
      fragile_nodes += replay_profile.fragile_nodes;
      supported_row_changes += replay_profile.supported_row_changes;
      unsupported_row_changes += replay_profile.unsupported_row_changes;
      protected_segments += replay_profile.protected_segments;
      quality_adjustment += replay_profile.quality_adjustment;
      if (replay_decision.sticky_dominant) {
        ++sticky_dominant_accepts;
      } else if (replay_decision.fragile_dominant) {
        ++fragile_dominant_accepts;
        const bool used_override
            = replay_decision.high_delta_override
              || replay_profile.supported_row_changes
                     > replay_profile.unsupported_row_changes;
        if (used_override) {
          ++fragile_override_accepts;
          ++total_fragile_override_accepts;
        }
      } else {
        ++balanced_accepts;
      }
      if (replay_used_swap || best_is_swap) {
        ++swaps_;
      } else {
        ++moves_;
      }
    }

    attempts_ += exact_candidates_scored;
    const auto runtime_ms
        = std::chrono::duration_cast<std::chrono::milliseconds>(
              std::chrono::steady_clock::now() - pass_start)
              .count();
    mgr_->getLogger()->info(
        DPL,
        928,
        "Focused exact source-edge pass {} summary: focused_node_seeds={}, "
        "hot_segment_seeds={}, accepted_hot_seeds={}, source_edge_cells={}, proposals={}, "
        "assignment_bundles={}, assignment_solves={}, assignment_candidates={}, "
        "assignment_exact_scored={}, "
        "assignment_accepts={}, assignment_delta={:.2f}, "
        "exact_scored={}, accepts={}, accepted_delta={:.2f}, "
        "replay_failures={}, sticky_nodes={}, fragile_nodes={}, "
        "fragility_rejects={}, supported_row_changes={}, "
        "unsupported_row_changes={}, bins sticky/balanced/fragile={}/{}/{}, "
        "fragile_override_accepts={}, extreme_fragile_rejects={}, "
        "quota_fragile_rejects={}, fragile_override_quota={}, "
        "quality_adjustment={:.2f}, runtime_ms={}",
        pass,
        seed_bundle.focused_node_seed_count,
        seed_bundle.hot_segment_seed_count,
        accepted_hot_segment_seed_cells,
        source_edge_cells,
        exact_candidates_generated,
        same_segment_assignment_bundles,
        same_segment_assignment_solves,
        same_segment_assignment_candidates,
        same_segment_assignment_exact_scored,
        same_segment_assignment_accepts,
        same_segment_assignment_accepted_delta,
        exact_candidates_scored,
        exact_accepts,
        accepted_delta,
        replay_failures,
        sticky_nodes,
        fragile_nodes,
        fragility_rejects,
        supported_row_changes,
        unsupported_row_changes,
        sticky_dominant_accepts,
        balanced_accepts,
        fragile_dominant_accepts,
        fragile_override_accepts,
        extreme_fragile_rejects,
        quota_fragile_rejects,
        fragile_override_quota,
        quality_adjustment,
        runtime_ms);
    mgr_->getLogger()->metric("dpl_evolve__focused_global_swap__exact_scored",
                              exact_candidates_scored);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_bundles",
        same_segment_assignment_bundles);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_solves",
        same_segment_assignment_solves);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_candidates",
        same_segment_assignment_candidates);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_exact_scored",
        same_segment_assignment_exact_scored);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__accepted_hot_segment_seed_cells",
        accepted_hot_segment_seed_cells);
    mgr_->getLogger()->metric("dpl_evolve__focused_global_swap__accepts",
                              exact_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_accepts",
        same_segment_assignment_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__accepted_delta_dbu", accepted_delta);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__same_segment_assignment_accepted_delta_dbu",
        same_segment_assignment_accepted_delta);
    mgr_->getLogger()->metric("dpl_evolve__focused_global_swap__replay_failures",
                              replay_failures);
    mgr_->getLogger()->metric("dpl_evolve__focused_global_swap__sticky_nodes",
                              sticky_nodes);
    mgr_->getLogger()->metric("dpl_evolve__focused_global_swap__fragile_nodes",
                              fragile_nodes);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__supported_row_changes",
        supported_row_changes);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__unsupported_row_changes",
        unsupported_row_changes);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__fragility_rejects",
        fragility_rejects);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__sticky_dominant_accepts",
        sticky_dominant_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__balanced_accepts",
        balanced_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__fragile_dominant_accepts",
        fragile_dominant_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__fragile_override_accepts",
        fragile_override_accepts);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__extreme_fragile_rejects",
        extreme_fragile_rejects);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__quota_fragile_rejects",
        quota_fragile_rejects);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__fragile_override_quota",
        fragile_override_quota);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__protected_segments",
        protected_segments);
    mgr_->getLogger()->metric(
        "dpl_evolve__focused_global_swap__quality_adjustment_dbu",
        quality_adjustment);

    const double relative_improvement
        = before_pass_hpwl == 0.0
              ? 0.0
              : std::abs(curr_hpwl - before_pass_hpwl) / before_pass_hpwl;
    if (pass >= 2
        && (exact_accepts == 0 || accepted_delta <= 0.0
            || relative_improvement <= tol)) {
      break;
    }
    mgr_->resortSegments();
  }

  const double final_improvement
      = ((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.0;
  mgr_->getLogger()->info(DPL,
                          929,
                          "Focused exact source-edge global swap complete: "
                          "final HPWL={:.6e}, improvement={:.2f}%, "
                          "sticky_live={}, sticky_segments={}, "
                          "fragile_override_accepts={}",
                          curr_hpwl,
                          final_improvement,
                          mgr_->getNumStickyExactNodes(),
                          mgr_->getNumStickyExactSegments(),
                          total_fragile_override_accepts);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::getRange(Node* nd,
                                  odb::Rect& nodeBbox,
                                  double* source_edge_criticality,
                                  int* critical_edge_count)
{
  // Determines the median location for a node.

  Edge* ed;
  unsigned mid;

  Pin* pin;
  unsigned t = 0;

  DbuX xmin = arch_->getMinX();
  DbuX xmax = arch_->getMaxX();
  DbuY ymin = arch_->getMinY();
  DbuY ymax = arch_->getMaxY();
  double source_edge_score = 0.0;
  int edge_hits = 0;
  const double current_center_x = nd->getLeft().v + (0.5 * nd->getWidth().v);
  const double current_center_y = nd->getBottom().v + (0.5 * nd->getHeight().v);

  xpts_.clear();
  ypts_.clear();
  for (int n = 0; n < nd->getNumPins(); n++) {
    pin = nd->getPins()[n];

    ed = pin->getEdge();

    nodeBbox.mergeInit();

    int numPins = ed->getNumPins();
    if (numPins <= 1) {
      continue;
    }
    if (numPins > skipNetsLargerThanThis_) {
      continue;
    }
    if (!calculateEdgeBB(ed, nd, nodeBbox)) {
      continue;
    }

    // We've computed an interval for the pin.  We need to alter it to work for
    // the cell center. Also, we need to avoid going off the edge of the chip.
    nodeBbox.set_xlo(std::min(
        std::max(xmin.v, nodeBbox.xMin() - pin->getOffsetX().v), xmax.v));
    nodeBbox.set_xhi(std::max(
        std::min(xmax.v, nodeBbox.xMax() - pin->getOffsetX().v), xmin.v));
    nodeBbox.set_ylo(std::min(
        std::max(ymin.v, nodeBbox.yMin() - pin->getOffsetY().v), ymax.v));
    nodeBbox.set_yhi(std::max(
        std::min(ymax.v, nodeBbox.yMax() - pin->getOffsetY().v), ymin.v));

    if (source_edge_criticality != nullptr || critical_edge_count != nullptr) {
      const double x_violation
          = current_center_x < nodeBbox.xMin()
                ? nodeBbox.xMin() - current_center_x
                : (current_center_x > nodeBbox.xMax()
                       ? current_center_x - nodeBbox.xMax()
                       : 0.0);
      const double y_violation
          = current_center_y < nodeBbox.yMin()
                ? nodeBbox.yMin() - current_center_y
                : (current_center_y > nodeBbox.yMax()
                       ? current_center_y - nodeBbox.yMax()
                       : 0.0);
      if (x_violation > 0.0 || y_violation > 0.0) {
        const double net_weight
            = 1.0 + (4.0 / static_cast<double>(std::max(2, numPins)));
        source_edge_score += net_weight * (x_violation + y_violation);
        ++edge_hits;
      }
    }

    // Record the location and pin offset used to generate this point.

    xpts_.push_back(nodeBbox.xMin());
    xpts_.push_back(nodeBbox.xMax());

    ypts_.push_back(nodeBbox.yMin());
    ypts_.push_back(nodeBbox.yMax());

    ++t;
    ++t;
  }

  // If, for some weird reason, we didn't find anything connected, then
  // return false to indicate that there's nowhere to move the cell.
  if (t <= 1) {
    return false;
  }

  // Get the median values.
  mid = t >> 1;

  std::ranges::sort(xpts_);
  std::ranges::sort(ypts_);

  nodeBbox.set_xlo(xpts_[mid - 1]);
  nodeBbox.set_xhi(xpts_[mid]);

  nodeBbox.set_ylo(ypts_[mid - 1]);
  nodeBbox.set_yhi(ypts_[mid]);

  if (source_edge_criticality != nullptr) {
    *source_edge_criticality = source_edge_score;
  }
  if (critical_edge_count != nullptr) {
    *critical_edge_count = edge_hits;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox)
{
  // Computes the bounding box of an edge.  Node 'nd' is the node to SKIP.
  DbuX curX;
  DbuY curY;

  bbox.mergeInit();

  int count = 0;
  for (Pin* pin : ed->getPins()) {
    auto other = pin->getNode();
    if (other == nd) {
      continue;
    }
    curX = other->getCenterX() + pin->getOffsetX().v;
    curY = other->getCenterY() + pin->getOffsetY().v;

    bbox.set_xlo(std::min(curX.v, bbox.xMin()));
    bbox.set_xhi(std::max(curX.v, bbox.xMax()));
    bbox.set_ylo(std::min(curY.v, bbox.yMin()));
    bbox.set_yhi(std::max(curY.v, bbox.yMax()));

    ++count;
  }

  return count != 0;
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generateWirelengthOptimalMove(Node* ndi)
{
  double yi = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
  double xi = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

  // Determine optimal region.
  odb::Rect bbox;
  if (!getRange(ndi, bbox)) {
    // Failed to find an optimal region.
    return false;
  }
  if (xi >= bbox.xMin() && xi <= bbox.xMax() && yi >= bbox.yMin()
      && yi <= bbox.yMax()) {
    // If cell inside box, do nothing.
    return false;
  }

  // Observe displacement limit.
  int dispX, dispY;
  mgr_->getMaxDisplacement(dispX, dispY);
  odb::Rect lbox(ndi->getLeft().v - dispX,
                 ndi->getBottom().v - dispY,
                 ndi->getLeft().v + dispX,
                 ndi->getBottom().v + dispY);
  if (lbox.xMax() <= bbox.xMin()) {
    bbox.set_xlo(ndi->getLeft().v);
    bbox.set_xhi(lbox.xMax());
  } else if (lbox.xMin() >= bbox.xMax()) {
    bbox.set_xlo(lbox.xMin());
    bbox.set_xhi(ndi->getLeft().v);
  } else {
    bbox.set_xlo(std::max(bbox.xMin(), lbox.xMin()));
    bbox.set_xhi(std::min(bbox.xMax(), lbox.xMax()));
  }
  if (lbox.yMax() <= bbox.yMin()) {
    bbox.set_ylo(ndi->getBottom().v);
    bbox.set_yhi(lbox.yMax());
  } else if (lbox.yMin() >= bbox.yMax()) {
    bbox.set_ylo(lbox.yMin());
    bbox.set_yhi(ndi->getBottom().v);
  } else {
    bbox.set_ylo(std::max(bbox.yMin(), lbox.yMin()));
    bbox.set_yhi(std::min(bbox.yMax(), lbox.yMax()));
  }

  if (mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
    return false;
  }
  int si = mgr_->getReverseCellToSegs(ndi->getId())[0]->getSegId();

  // Position target so center of cell at center of box.
  DbuX xj{(int) std::floor(0.5 * (bbox.xMin() + bbox.xMax())
                           - 0.5 * ndi->getWidth().v)};
  DbuY yj{(int) std::floor(0.5 * (bbox.yMin() + bbox.yMax())
                           - 0.5 * ndi->getHeight().v)};

  // Row and segment for the destination.
  int rj = arch_->find_closest_row(yj);
  yj = DbuY{arch_->getRow(rj)->getBottom()};  // Row alignment.
  int sj = -1;
  for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
    DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
    if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
      sj = segPtr->getSegId();
      break;
    }
  }
  if (sj == -1) {
    return false;
  }
  if (ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
    return false;
  }

  if (mgr_->tryMove(ndi, ndi->getLeft(), ndi->getBottom(), si, xj, yj, sj)) {
    ++moves_;
    return true;
  }
  if (mgr_->trySwap(ndi, ndi->getLeft(), ndi->getBottom(), si, xj, yj, sj)) {
    ++swaps_;
    return true;
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generateRandomMove(Node* ndi)
{
  // Generate a random move within the current displacement constraints
  // This is for exploration and power optimization purposes

  if (mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
    return false;
  }
  int si = mgr_->getReverseCellToSegs(ndi->getId())[0]->getSegId();

  // Get current displacement limits
  int dispX, dispY;
  mgr_->getMaxDisplacement(dispX, dispY);

  // Define the search area around the current cell position
  DbuX curr_x = ndi->getLeft();
  DbuY curr_y = ndi->getBottom();

  DbuX min_x = std::max(arch_->getMinX(), curr_x - dispX);
  DbuX max_x = std::min(arch_->getMaxX(), curr_x + dispX);
  DbuY min_y = std::max(arch_->getMinY(), curr_y - dispY);
  DbuY max_y = std::min(arch_->getMaxY(), curr_y + dispY);

  // Try up to 10 random locations within the displacement area
  const int max_attempts = 10;
  for (int attempt = 0; attempt < max_attempts; attempt++) {
    // Generate random coordinates within the allowed displacement area
    DbuX rand_x{min_x.v + mgr_->getRandom(max_x.v - min_x.v + 1)};
    DbuY rand_y{min_y.v + mgr_->getRandom(max_y.v - min_y.v + 1)};

    // Find the appropriate row and segment for this random location
    int rj = arch_->find_closest_row(rand_y);
    rand_y = DbuY{arch_->getRow(rj)->getBottom()};  // Row alignment

    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (rand_x >= segPtr->getMinX() && rand_x <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }

    if (sj == -1) {
      continue;  // Invalid segment, try another random location
    }

    if (ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
      continue;  // Wrong region, try another location
    }

    // Try to execute the move/swap to this random location
    if (mgr_->tryMove(ndi, curr_x, curr_y, si, rand_x, rand_y, sj)) {
      ++moves_;
      return true;
    }
    if (mgr_->trySwap(ndi, curr_x, curr_y, si, rand_x, rand_y, sj)) {
      ++swaps_;
      return true;
    }
  }

  return false;  // Could not find a valid random move after max_attempts
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(Node* ndi)
{
  // Hybrid move generation: Smart Swap logic
  bool move_generated = false;

  // Phase 1: Try wirelength-optimal move (unless we decide to override with
  // exploration)
  if (mgr_->getRandom(1000) >= static_cast<int>(tradeoff_ * 1000)) {
    move_generated = generateWirelengthOptimalMove(ndi);
  }

  // Phase 2: If no move generated OR we decided to override, try random
  // exploration move
  if (!move_generated && allow_random_moves_) {
    move_generated = generateRandomMove(ndi);
  }

  return move_generated;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();
  swap_params_ = &mgr->getGlobalSwapParams();

  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, 0);

  // Congestion-aware placement initialization.
  const float area_weight = static_cast<float>(swap_params_->area_weight);
  const float pin_weight = static_cast<float>(swap_params_->pin_weight);
  mgr_->getGrid()->computeUtilizationMap(network_, area_weight, pin_weight);

  congestion_contribution_.resize(network_->getNumNodes());
  for (const auto& node_ptr : network_->getNodes()) {
    Node* node = node_ptr.get();
    if (node && node->getType() == Node::Type::CELL) {
      const double cell_area
          = static_cast<double>(node->getWidth().v) * node->getHeight().v;
      const double num_pins = static_cast<double>(node->getNumPins());
      congestion_contribution_[node->getId()]
          = area_weight * cell_area + pin_weight * num_pins;
    }
  }

  // Calculate adaptive congestion weight by sampling typical HPWL deltas and
  // improvements.
  congestion_weight_ = calculateAdaptiveCongestionWeight();

  mgr_->getLogger()->info(
      DPL,
      901,
      "Initialized congestion-aware global swap with adaptive weight={:.3f}",
      congestion_weight_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(DetailedMgr* mgr,
                                  std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();
  swap_params_ = &mgr->getGlobalSwapParams();

  Node* ndi = candidates[mgr_->getRandom(candidates.size())];

  return generate(ndi);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedGlobalSwap::calculateAdaptiveCongestionWeight()
{
  const int num_samples = swap_params_ ? swap_params_->sampling_moves : 150;
  const double user_knob
      = swap_params_ ? swap_params_->user_congestion_weight : 35.0;

  // Get candidate cells for sampling
  std::vector<Node*> candidates = mgr_->getSingleHeightCells();
  if (candidates.size() < 2) {
    return 1.0 * mgr_->getGrid()->getSiteWidth().v;
  }

  // Create temporary HPWL objective for sampling
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);

  double total_hpwl_delta = 0.0;
  double total_cong_improvement = 0.0;
  int valid_samples = 0;

  // Sample random swaps to estimate typical deltas
  for (int i = 0; i < num_samples && i < candidates.size(); i++) {
    // Pick a random candidate cell
    Node* cell_a = candidates[mgr_->getRandom(candidates.size())];

    // Try to generate a move/swap for this cell
    if (!generate(cell_a)) {
      continue;  // Skip if no valid move found
    }

    // Calculate HPWL delta
    double hpwl_delta = hpwlObj.delta(mgr_->getJournal());

    // Calculate congestion improvement
    double cong_improvement = 0.0;
    const auto& journal = mgr_->getJournal();
    if (!journal.empty()) {
      for (const auto& action_ptr : journal) {
        if (action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
          continue;
        }

        const MoveCellAction* move_action
            = static_cast<const MoveCellAction*>(action_ptr.get());
        Node* moved_cell = move_action->getNode();
        if (!moved_cell
            || moved_cell->getId() >= congestion_contribution_.size()) {
          continue;
        }

        // Get grid coordinates
        const auto* grid = mgr_->getGrid();
        const GridX orig_grid_x = grid->gridX(move_action->getOrigLeft());
        const GridY orig_grid_y
            = grid->gridSnapDownY(move_action->getOrigBottom());
        const GridX new_grid_x = grid->gridX(move_action->getNewLeft());
        const GridY new_grid_y
            = grid->gridSnapDownY(move_action->getNewBottom());

        // Calculate pixel indices
        const int row_site_count = grid->getRowSiteCount().v;
        const int orig_pixel_idx
            = (orig_grid_y.v * row_site_count) + orig_grid_x.v;
        const int new_pixel_idx
            = (new_grid_y.v * row_site_count) + new_grid_x.v;

        // Get densities
        const float orig_density = grid->getUtilizationDensity(orig_pixel_idx);
        const float new_density = grid->getUtilizationDensity(new_pixel_idx);

        // Get cell contribution
        const double cell_cong_contrib
            = congestion_contribution_[moved_cell->getId()];

        // Calculate improvement
        cong_improvement += (orig_density - new_density) * cell_cong_contrib;
      }
    }

    // Accumulate magnitudes
    total_hpwl_delta += std::abs(hpwl_delta);
    total_cong_improvement += std::abs(cong_improvement);
    valid_samples++;

    // Always reject the sample move
    mgr_->rejectMove();
  }

  if (valid_samples == 0) {
    mgr_->getLogger()->warn(
        DPL,
        902,
        "No valid samples for adaptive weight calculation, using fallback");
    return 1.0 * mgr_->getGrid()->getSiteWidth().v;
  }

  // Calculate averages
  double avg_hpwl_delta = total_hpwl_delta / valid_samples;
  double avg_cong_improvement = total_cong_improvement / valid_samples;

  // Calculate adaptive weight
  double adaptive_weight;
  if (avg_cong_improvement > 0) {
    adaptive_weight = (avg_hpwl_delta / avg_cong_improvement) * user_knob;
  } else {
    adaptive_weight = 0.5 * mgr_->getGrid()->getSiteWidth().v;
  }

  mgr_->getLogger()->info(DPL,
                          903,
                          "Adaptive congestion weight: avg_hpwl_delta={:.2f}, "
                          "avg_cong_improvement={:.6f}, "
                          "samples={}, weight={:.3f}",
                          avg_hpwl_delta,
                          avg_cong_improvement,
                          valid_samples,
                          adaptive_weight);

  return adaptive_weight;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::stats()
{
  mgr_->getLogger()->info(
      DPL,
      334,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

}  // namespace dpl_evolve
