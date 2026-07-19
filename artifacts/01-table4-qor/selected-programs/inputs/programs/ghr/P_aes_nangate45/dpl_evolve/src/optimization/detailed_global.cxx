// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_global.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "detailed_generator.h"
#include "detailed_global_legacy.h"
#include "detailed_manager.h"
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
constexpr int kHandoffTopNetCap = 64;
constexpr int kEndpointHintsPerCellCap = 4;
constexpr size_t kEndpointTargetCap = 12;

struct ExactTarget
{
  int seg_id = -1;
  DbuX left{0};
  DbuY bottom{0};
  double cheap_score = 0.0;
  bool from_endpoint_hint = false;
};

struct EdgeEndpointStats
{
  int min_x = std::numeric_limits<int>::max();
  int second_min_x = std::numeric_limits<int>::max();
  int max_x = std::numeric_limits<int>::min();
  int second_max_x = std::numeric_limits<int>::min();
  int min_y = std::numeric_limits<int>::max();
  int second_min_y = std::numeric_limits<int>::max();
  int max_y = std::numeric_limits<int>::min();
  int second_max_y = std::numeric_limits<int>::min();
  int min_x_count = 0;
  int max_x_count = 0;
  int min_y_count = 0;
  int max_y_count = 0;
};

struct EndpointRepairHint
{
  DbuX target_left{0};
  DbuY target_bottom{0};
  uint64_t edge_hpwl = 0;
  int endpoint_dims = 0;
  double priority = 0.0;
};

struct MoveActionSnapshot
{
  Node* node = nullptr;
  DbuX orig_left{0};
  DbuY orig_bottom{0};
  DbuX new_left{0};
  DbuY new_bottom{0};
  std::vector<int> orig_segs;
  std::vector<int> new_segs;
};

double intervalDistance(const int value, const int lo, const int hi)
{
  if (value < lo) {
    return static_cast<double>(lo - value);
  }
  if (value > hi) {
    return static_cast<double>(value - hi);
  }
  return 0.0;
}

void appendJournal(const Journal& source, Journal& dest)
{
  for (const auto& action_ptr : source) {
    if (action_ptr == nullptr) {
      continue;
    }
    switch (action_ptr->typeId()) {
      case JournalActionTypeEnum::MOVE_CELL: {
        const auto* move_action
            = static_cast<const MoveCellAction*>(action_ptr.get());
        dest.addAction(MoveCellAction(move_action->getNode(),
                                      move_action->getOrigLeft(),
                                      move_action->getOrigBottom(),
                                      move_action->getNewLeft(),
                                      move_action->getNewBottom(),
                                      move_action->wasPlaced(),
                                      move_action->getOrigSegs(),
                                      move_action->getNewSegs()));
        break;
      }
      case JournalActionTypeEnum::UNPLACE_CELL: {
        const auto* unplace_action
            = static_cast<const UnplaceCellAction*>(action_ptr.get());
        dest.addAction(UnplaceCellAction(
            unplace_action->getNode(), unplace_action->wasHold()));
        break;
      }
    }
  }
}

void captureAcceptedMoveData(const Journal& source,
                            std::vector<MoveActionSnapshot>& moves,
                            std::vector<int>& touched_segments)
{
  moves.clear();
  touched_segments.clear();
  for (const auto& action_ptr : source) {
    if (action_ptr == nullptr
        || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
      continue;
    }
    const auto* move_action = static_cast<const MoveCellAction*>(action_ptr.get());
    moves.push_back(MoveActionSnapshot{move_action->getNode(),
                                       move_action->getOrigLeft(),
                                       move_action->getOrigBottom(),
                                       move_action->getNewLeft(),
                                       move_action->getNewBottom(),
                                       move_action->getOrigSegs(),
                                       move_action->getNewSegs()});
    for (const int segId : move_action->getOrigSegs()) {
      if (segId >= 0
          && std::find(touched_segments.begin(), touched_segments.end(), segId)
                 == touched_segments.end()) {
        touched_segments.push_back(segId);
      }
    }
    for (const int segId : move_action->getNewSegs()) {
      if (segId >= 0
          && std::find(touched_segments.begin(), touched_segments.end(), segId)
                 == touched_segments.end()) {
        touched_segments.push_back(segId);
      }
    }
  }
}

bool hasTarget(const std::vector<ExactTarget>& targets,
               const int segId,
               const DbuX left,
               const DbuY bottom)
{
  return std::ranges::any_of(
      targets, [&](const ExactTarget& target) {
        return target.seg_id == segId && target.left == left
               && target.bottom == bottom;
      });
}

bool isMovableStdCell(const Node* node)
{
  return node != nullptr && node->getType() == Node::Type::CELL
         && node->isStdCell() && !node->isFixed();
}

EdgeEndpointStats analyzeEdgeEndpoints(const Edge* edge)
{
  EdgeEndpointStats stats;
  if (edge == nullptr) {
    return stats;
  }
  for (const Pin* pin : edge->getPins()) {
    if (pin == nullptr || pin->getNode() == nullptr) {
      continue;
    }
    const int x = (pin->getNode()->getCenterX() + pin->getOffsetX()).v;
    const int y = (pin->getNode()->getCenterY() + pin->getOffsetY()).v;

    if (x < stats.min_x) {
      stats.second_min_x = stats.min_x;
      stats.min_x = x;
      stats.min_x_count = 1;
    } else if (x == stats.min_x) {
      stats.min_x_count++;
    } else if (x < stats.second_min_x) {
      stats.second_min_x = x;
    }

    if (x > stats.max_x) {
      stats.second_max_x = stats.max_x;
      stats.max_x = x;
      stats.max_x_count = 1;
    } else if (x == stats.max_x) {
      stats.max_x_count++;
    } else if (x > stats.second_max_x) {
      stats.second_max_x = x;
    }

    if (y < stats.min_y) {
      stats.second_min_y = stats.min_y;
      stats.min_y = y;
      stats.min_y_count = 1;
    } else if (y == stats.min_y) {
      stats.min_y_count++;
    } else if (y < stats.second_min_y) {
      stats.second_min_y = y;
    }

    if (y > stats.max_y) {
      stats.second_max_y = stats.max_y;
      stats.max_y = y;
      stats.max_y_count = 1;
    } else if (y == stats.max_y) {
      stats.max_y_count++;
    } else if (y > stats.second_max_y) {
      stats.second_max_y = y;
    }
  }
  return stats;
}

int countEndpointDimensions(const Pin* pin, const EdgeEndpointStats& stats)
{
  if (pin == nullptr || pin->getNode() == nullptr) {
    return 0;
  }
  const int pin_x = (pin->getNode()->getCenterX() + pin->getOffsetX()).v;
  const int pin_y = (pin->getNode()->getCenterY() + pin->getOffsetY()).v;
  int dims = 0;
  if ((pin_x == stats.min_x && stats.min_x_count == 1
       && stats.second_min_x != std::numeric_limits<int>::max())
      || (pin_x == stats.max_x && stats.max_x_count == 1
          && stats.second_max_x != std::numeric_limits<int>::min())) {
    dims++;
  }
  if ((pin_y == stats.min_y && stats.min_y_count == 1
       && stats.second_min_y != std::numeric_limits<int>::max())
      || (pin_y == stats.max_y && stats.max_y_count == 1
          && stats.second_max_y != std::numeric_limits<int>::min())) {
    dims++;
  }
  return dims;
}

bool buildEndpointRepairHint(Node* node,
                             const Pin* pin,
                             const EdgeEndpointStats& stats,
                             const uint64_t edge_hpwl,
                             EndpointRepairHint& hint)
{
  if (node == nullptr || pin == nullptr || pin->getNode() != node) {
    return false;
  }

  const int pin_x = (node->getCenterX() + pin->getOffsetX()).v;
  const int pin_y = (node->getCenterY() + pin->getOffsetY()).v;
  int target_pin_x = pin_x;
  int target_pin_y = pin_y;
  int endpoint_dims = 0;

  if (pin_x == stats.min_x && stats.min_x_count == 1
      && stats.second_min_x != std::numeric_limits<int>::max()) {
    target_pin_x = stats.second_min_x;
    endpoint_dims++;
  } else if (pin_x == stats.max_x && stats.max_x_count == 1
             && stats.second_max_x != std::numeric_limits<int>::min()) {
    target_pin_x = stats.second_max_x;
    endpoint_dims++;
  }

  if (pin_y == stats.min_y && stats.min_y_count == 1
      && stats.second_min_y != std::numeric_limits<int>::max()) {
    target_pin_y = stats.second_min_y;
    endpoint_dims++;
  } else if (pin_y == stats.max_y && stats.max_y_count == 1
             && stats.second_max_y != std::numeric_limits<int>::min()) {
    target_pin_y = stats.second_max_y;
    endpoint_dims++;
  }

  if (endpoint_dims == 0) {
    return false;
  }

  const int width = node->getWidth().v;
  const int height = node->getHeight().v;
  hint.target_left = DbuX{target_pin_x - pin->getOffsetX().v
                          - static_cast<int>(std::floor(0.5 * width))};
  hint.target_bottom = DbuY{target_pin_y - pin->getOffsetY().v
                            - static_cast<int>(std::floor(0.5 * height))};
  hint.edge_hpwl = edge_hpwl;
  hint.endpoint_dims = endpoint_dims;
  hint.priority
      = static_cast<double>(edge_hpwl) * (1.0 + 0.35 * (endpoint_dims - 1));
  return true;
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
  resetExactSearchStats();
  consume_handoff_ = false;
  incoming_hot_segments_.clear();
  incoming_hot_segment_mask_.clear();
  incoming_source_edge_hot_cells_.clear();
  incoming_handoff_segments_ = 0;
  incoming_handoff_source_cells_ = 0;
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
    } else if (args[i] == "-handoff") {
      consume_handoff_ = true;
    }
  }
  if (consume_handoff_) {
    incoming_hot_segments_ = mgr_->getHotSegments();
    incoming_handoff_segments_
        = static_cast<int>(incoming_hot_segments_.size());
    incoming_hot_segment_mask_.assign(mgr_->getNumSegments(), 0);
    for (const int seg_id : incoming_hot_segments_) {
      if (seg_id >= 0
          && seg_id < static_cast<int>(incoming_hot_segment_mask_.size())) {
        incoming_hot_segment_mask_[seg_id] = 1;
      }
    }
    incoming_source_edge_hot_cells_ = mgr_->getSourceEdgeHotCellMask();
    incoming_handoff_source_cells_
        = static_cast<int>(std::count(incoming_source_edge_hot_cells_.begin(),
                                      incoming_source_edge_hot_cells_.end(),
                                      static_cast<uint8_t>(1)));
  }
  mgr_->clearHotSegments();
  passes = std::max(passes, 1);
  tol = std::max(tol, 0.01);
  tradeoff_ = std::max(0.0, std::min(1.0, tradeoff_));  // Clamp to [0.0, 1.0]

  uint64_t hpwl_x, hpwl_y;
  int64_t init_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  if (init_hpwl == 0) {
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
  const int chip_width_dbu = arch_->getMaxX().v - arch_->getMinX().v;
  const int chip_height_dbu = arch_->getMaxY().v - arch_->getMinY().v;
  const int chip_width_sites = std::max(1, chip_width_dbu / row_height);
  const int chip_height_sites = std::max(1, chip_height_dbu / row_height);

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
  constexpr double kExactSearchFloor = 0.35;

  // Even low-util designs can benefit from the bounded exact DPO path.
  // Keep a floor for exact source-edge search instead of dropping to legacy.
  extra_dpl_intensity_ = std::max(kExactSearchFloor,
                                  std::clamp(combined_score, 0.0, 1.0));
  extra_dpl_alpha_ = extra_dpl_intensity_ * extra_dpl_intensity_;

  if (extra_dpl_intensity_ <= 0.0) {
    mgr_->clearHotSegments();
    return;
  }
  if (combined_score <= 0.0) {
    mgr_->getLogger()->info(
        DPL,
        905,
        "Low-util exact-source mode: forcing enhanced global swap "
        "(stdcell_util={:.3f}, util_score={:.2f}, utilmap: mean={:.3f} "
        "p95={:.3f} p99={:.3f} frac>0.80={:.3f} frac>0.90={:.3f}, "
        "search_floor={:.2f}).",
        stdcell_utilization,
        util_score,
        density_stats.mean,
        density_stats.p95,
        density_stats.p99,
        density_stats.frac_gt_080,
        density_stats.frac_gt_090,
        kExactSearchFloor);
  }

  const double base_tradeoff = tradeoff_;
  const bool pass2_allow_random_moves = extra_dpl_intensity_ >= 0.50;
  const double pass2_tradeoff
      = pass2_allow_random_moves ? base_tradeoff * extra_dpl_intensity_ : 0.0;

  mgr_->getLogger()->info(
      DPL,
      906,
      "Starting two-pass congestion-aware global swap optimization "
      "(stdcell_util={:.3f}, util_score={:.2f}, utilmap: mean={:.3f} "
      "p95={:.3f} p99={:.3f} frac>0.80={:.3f} frac>0.90={:.3f}, "
      "intensity={:.2f}, alpha={:.3f}, tradeoff={:.2f}->{:.2f}, "
      "random_moves={})",
      stdcell_utilization,
      util_score,
      density_stats.mean,
      density_stats.p95,
      density_stats.p99,
      density_stats.frac_gt_080,
      density_stats.frac_gt_090,
      extra_dpl_intensity_,
      extra_dpl_alpha_,
      base_tradeoff,
      pass2_tradeoff,
      pass2_allow_random_moves ? "on" : "off");

  // PASS 1: HPWL Profiling Pass
  mgr_->getLogger()->info(
      DPL, 907, "Pass 1: HPWL profiling to determine budget");

  // Clear journal to ensure clean state tracking for profiling pass
  mgr_->getJournal().clear();

  // Snapshot RNG state so Pass 2 is not affected by profiling randomness.
  const Placer_RNG rng_state = mgr_->getRngState();
  Journal profiling_journal(mgr_->getGrid(), mgr_);
  profiling_journal.clear();
  profiling_journal_ = &profiling_journal;

  is_profiling_pass_ = true;
  congestion_weight_ = 0.0;  // Pure HPWL optimization
  tradeoff_ = 0.0;
  allow_random_moves_ = false;  // Match legacy generator during profiling

  const int profiling_passes
      = std::max(1, static_cast<int>(std::ceil(passes * extra_dpl_alpha_)));

  int64_t last_hpwl, curr_hpwl = init_hpwl;
  for (int p = 1; p <= profiling_passes; p++) {
    last_hpwl = curr_hpwl;
    globalSwap();
    curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);

    mgr_->getLogger()->info(DPL,
                            316,
                            "Profiling pass {:d}; hpwl is {:.6e}.",
                            p,
                            (double) curr_hpwl);

    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / (double) last_hpwl <= tol) {
      break;
    }
  }

  // Calculate budget allowance from profiling pass
  double optimal_hpwl = curr_hpwl;
  double profiling_excess = params.profiling_excess;
  if (profiling_excess <= 0.0) {
    profiling_excess = 1.10;
  }
  budget_hpwl_ = optimal_hpwl * profiling_excess;
  const double budget_pct = ((budget_hpwl_ / optimal_hpwl) - 1.0) * 100.0;
  mgr_->getLogger()->info(
      DPL,
      908,
      "Profiling complete. Optimal HPWL={:.2f}, Budget HPWL={:.2f} ({:+.1f}%)",
      optimal_hpwl,
      budget_hpwl_,
      budget_pct);

  // Restore initial state using Journal's built-in undo mechanism
  mgr_->getLogger()->info(DPL,
                          917,
                          "Undoing {} profiling moves to restore initial state",
                          profiling_journal.size());
  profiling_journal.undo();
  profiling_journal.clear();
  profiling_journal_ = nullptr;
  mgr_->getJournal().clear();  // Clear journal for second pass
  mgr_->setRngState(rng_state);
  mgr_->clearHotSegments();

  // PASS 2: Iterative Budget-Constrained Congestion Optimization (4 iterations)
  mgr_->getLogger()->info(
      DPL,
      909,
      "Pass 2: Iterative budget-constrained congestion optimization "
      "(alpha={:.3f})",
      extra_dpl_alpha_);
  is_profiling_pass_ = false;
  tradeoff_ = pass2_tradeoff;
  allow_random_moves_ = pass2_allow_random_moves;

  // Utilization density map was computed before profiling and remains in sync
  // after Journal undo (profiling does not update the map). Ensure the map is
  // normalized for accurate density queries.
  if (mgr_->getGrid() != nullptr) {
    mgr_->getGrid()->normalizeUtilization();
  }

  // Calculate adaptive congestion weight once for all iterations and apply a
  // utilization-based taper so low-util designs behave more like legacy.
  if (extra_dpl_alpha_ <= 0.0) {
    congestion_weight_ = 0.0;
  } else {
    const double base_congestion_weight = calculateAdaptiveCongestionWeight();
    congestion_weight_ = base_congestion_weight * extra_dpl_alpha_;
    mgr_->getLogger()->info(DPL,
                            925,
                            "Tapered congestion weight: base={:.3f}, "
                            "scale={:.3f}, effective={:.3f}",
                            base_congestion_weight,
                            extra_dpl_alpha_,
                            congestion_weight_);
  }
  resetExactSearchStats();
  mgr_->clearHotSegments();

  // Define the iterative refinement schedule
  std::vector<double> budget_multipliers = params.budget_multipliers;
  if (budget_multipliers.empty()) {
    budget_multipliers = {1.10};
  }
  const std::vector<std::string> stage_names
      = {"Exploratory", "Consolidation", "Fine-tuning", "Final Polish"};

  const size_t num_stages = budget_multipliers.size();
  const int base_stage_passes = std::max(
      1, static_cast<int>(std::llround(passes * extra_dpl_intensity_)));
  const int extra_stage_passes = std::max(
      0,
      static_cast<int>(std::llround(
          passes * (static_cast<double>(num_stages - 1) * extra_dpl_alpha_))));
  const int total_stage_passes
      = std::max(1, base_stage_passes + extra_stage_passes);

  std::vector<double> stage_weights(num_stages, 0.0);
  if (num_stages == 1) {
    stage_weights[0] = 1.0;
  } else {
    // High intensity: spend more time in exploratory stages.
    const double denom
        = static_cast<double>(num_stages * (num_stages + 1)) / 2.0;
    std::vector<double> high(num_stages, 0.0);
    for (size_t i = 0; i < num_stages; i++) {
      high[i] = static_cast<double>(num_stages - i) / denom;
    }

    // Low intensity: concentrate passes in the last stages.
    std::vector<double> low(num_stages, 0.0);
    if (num_stages == 2) {
      low[0] = 0.25;
      low[1] = 0.75;
    } else {
      low[num_stages - 2] = 0.30;
      low[num_stages - 1] = 0.70;
    }

    for (size_t i = 0; i < num_stages; i++) {
      stage_weights[i]
          = (1.0 - extra_dpl_alpha_) * low[i] + extra_dpl_alpha_ * high[i];
    }
  }

  std::vector<int> stage_passes(num_stages, 0);
  std::vector<double> stage_fracs(num_stages, 0.0);
  int allocated = 0;
  for (size_t i = 0; i < num_stages; i++) {
    const double exact = total_stage_passes * stage_weights[i];
    const int whole = static_cast<int>(std::floor(exact));
    stage_passes[i] = whole;
    stage_fracs[i] = exact - whole;
    allocated += whole;
  }
  int remaining = total_stage_passes - allocated;
  while (remaining > 0) {
    size_t best = 0;
    for (size_t i = 1; i < num_stages; i++) {
      if (stage_fracs[i] > stage_fracs[best]) {
        best = i;
      }
    }
    stage_passes[best] += 1;
    stage_fracs[best] = -1.0;
    remaining--;
  }

  curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);

  const double min_budget_multiplier = std::max(
      1.0, static_cast<double>(init_hpwl) / std::max(1.0, optimal_hpwl));

  // Iterative refinement loop
  for (size_t iteration = 0; iteration < budget_multipliers.size();
       iteration++) {
    if (stage_passes[iteration] <= 0) {
      continue;
    }
    // Update budget for this iteration
    const double requested_multiplier = budget_multipliers[iteration] > 0.0
                                            ? budget_multipliers[iteration]
                                            : 1.0;
    const double effective_multiplier
        = std::max(min_budget_multiplier,
                   min_budget_multiplier
                       + extra_dpl_alpha_
                             * (requested_multiplier - min_budget_multiplier));
    budget_hpwl_ = optimal_hpwl * effective_multiplier;
    std::string stage_name;
    if (iteration < stage_names.size()) {
      stage_name = stage_names[iteration];
    } else {
      stage_name = "Stage " + std::to_string(iteration + 1);
    }
    int disp_x_sites = orig_disp_x_sites;
    int disp_y_sites = orig_disp_y_sites;
    if (iteration == 0) {
      const double mult = 1.0 + extra_dpl_alpha_ * 4.0;  // up to 5x
      disp_x_sites = std::max(
          1, static_cast<int>(std::llround(orig_disp_x_sites * mult)));
      disp_y_sites = std::max(
          1, static_cast<int>(std::llround(orig_disp_y_sites * mult)));
    } else if (iteration == 1) {
      const double mult = 1.0 + extra_dpl_alpha_ * 2.0;  // up to 3x
      disp_x_sites = std::max(
          1, static_cast<int>(std::llround(orig_disp_x_sites * mult)));
      disp_y_sites = std::max(
          1, static_cast<int>(std::llround(orig_disp_y_sites * mult)));
    }
    disp_x_sites = std::min(disp_x_sites, chip_width_sites);
    disp_y_sites = std::min(disp_y_sites, chip_height_sites);
    mgr_->setMaxDisplacement(disp_x_sites, disp_y_sites);
    mgr_->getLogger()->info(DPL,
                            921,
                            "Iteration {} ({}): displacement set to ({}, {})",
                            iteration + 1,
                            stage_name,
                            disp_x_sites,
                            disp_y_sites);

    mgr_->getLogger()->info(
        DPL,
        919,
        "Iteration {}: {} stage - "
        "passes={}, Budget={:.2f} (req={:.3f}x, eff={:.3f}x, min={:.3f}x)",
        iteration + 1,
        stage_name,
        stage_passes[iteration],
        budget_hpwl_,
        requested_multiplier,
        effective_multiplier,
        min_budget_multiplier);

    // Run optimization passes for this iteration
    for (int p = 1; p <= stage_passes[iteration]; p++) {
      last_hpwl = curr_hpwl;
      globalSwap();
      curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);

      mgr_->getLogger()->info(
          DPL,
          331,
          "Congestion optimization iteration {} pass {:d}; hpwl is {:.6e}.",
          iteration + 1,
          p,
          (double) curr_hpwl);

      if (last_hpwl == 0
          || std::abs(curr_hpwl - last_hpwl) / (double) last_hpwl <= tol) {
        break;
      }
    }

    // Report iteration results
    const double iteration_improvement
        = ((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.0;
    double budget_utilization = 0.0;
    const double budget_range = budget_hpwl_ - optimal_hpwl;
    if (std::abs(budget_range) > std::numeric_limits<double>::epsilon()) {
      budget_utilization = ((curr_hpwl - optimal_hpwl) / budget_range) * 100.0;
    }
    mgr_->getLogger()->info(DPL,
                            920,
                            "Iteration {} complete: HPWL={:.6e}, "
                            "improvement={:.2f}%, budget utilization={:.1f}%",
                            iteration + 1,
                            static_cast<double>(curr_hpwl),
                            iteration_improvement,
                            budget_utilization);
  }

  // Final reporting
  double final_improvement
      = (((init_hpwl - curr_hpwl) / (double) init_hpwl) * 100.);
  double final_budget_utilization = 0.0;
  const double final_budget_range = budget_hpwl_ - optimal_hpwl;
  if (std::abs(final_budget_range) > std::numeric_limits<double>::epsilon()) {
    final_budget_utilization
        = ((curr_hpwl - optimal_hpwl) / final_budget_range) * 100.0;
  }

  mgr_->getLogger()->info(
      DPL,
      910,
      "Two-pass optimization complete: "
      "final HPWL={:.6e}, improvement={:.2f}%, budget utilization={:.1f}%",
      (double) curr_hpwl,
      final_improvement,
      final_budget_utilization);

  // Ensure original displacement limits are fully restored
  mgr_->setMaxDisplacement(orig_disp_x_sites, orig_disp_y_sites);
  mgr_->getLogger()->info(
      DPL,
      924,
      "Final restoration: displacement limits restored to original ({}, {})",
      orig_disp_x_sites,
      orig_disp_y_sites);
  mgr_->getLogger()->info(
      DPL,
      926,
      "Exact source-edge global swap: cells={}, source_edge_cells={}, "
      "targets={}, exact_evals={}, positive_candidates={}, staged={}, "
      "accepted={}, source_edge_accepts={}, budget_rejects={}, "
      "profit_rejects={}, random_fallbacks={}, hot_segments={}",
      exact_cells_considered_,
      exact_source_edge_cells_,
      exact_candidates_enumerated_,
      exact_candidates_scored_,
      exact_positive_candidates_,
      exact_moves_staged_,
      exact_moves_accepted_,
      exact_source_edge_accepts_,
      exact_budget_rejects_,
      exact_profit_rejects_,
      random_fallback_attempts_,
      mgr_->getHotSegments().size());
  mgr_->getLogger()->info(
      DPL,
      938,
      "Endpoint target-source repair: handoff={}, frontier_segments={}, "
      "frontier_source_cells={}, top_nets={}, prioritized_cells={}, "
      "endpoint_cells={}, endpoint_hints={}, endpoint_targets={}, "
      "endpoint_accepts={}, endpoint_gain={}",
      consume_handoff_ ? "on" : "off",
      incoming_handoff_segments_,
      incoming_handoff_source_cells_,
      handoff_top_nets_,
      handoff_prioritized_cells_,
      endpoint_target_cells_,
      endpoint_hint_count_,
      endpoint_target_candidates_,
      endpoint_accepts_,
      endpoint_accepted_gain_);
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::resetExactSearchStats()
{
  exact_cells_considered_ = 0;
  exact_source_edge_cells_ = 0;
  exact_candidates_enumerated_ = 0;
  exact_candidates_scored_ = 0;
  exact_positive_candidates_ = 0;
  exact_moves_staged_ = 0;
  exact_moves_accepted_ = 0;
  exact_source_edge_accepts_ = 0;
  exact_budget_rejects_ = 0;
  exact_profit_rejects_ = 0;
  random_fallback_attempts_ = 0;
  handoff_top_nets_ = 0;
  handoff_prioritized_cells_ = 0;
  endpoint_target_cells_ = 0;
  endpoint_hint_count_ = 0;
  endpoint_target_candidates_ = 0;
  endpoint_accepts_ = 0;
  endpoint_accepted_gain_ = 0;
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::isSourceEdgeCell(Node* ndi, const int segId) const
{
  if (segId < 0 || segId >= mgr_->getNumSegments()) {
    return false;
  }
  const auto& seg_nodes = mgr_->getCellsInSeg(segId);
  const auto it = std::find(seg_nodes.begin(), seg_nodes.end(), ndi);
  if (it == seg_nodes.end()) {
    return false;
  }
  const int index = static_cast<int>(std::distance(seg_nodes.begin(), it));
  if (index <= 1 || index >= static_cast<int>(seg_nodes.size()) - 2) {
    return true;
  }

  const auto* seg = mgr_->getSegment(segId);
  const DbuX site_width = arch_->getRow(0)->getSiteWidth();
  const DbuX edge_threshold = 4 * site_width;
  return (ndi->getLeft() - seg->getMinX() <= edge_threshold)
         || (seg->getMaxX() - ndi->getRight() <= edge_threshold);
}

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::globalSwap()
{
  // Two-pass budget-constrained global swap: profiling pass or congestion
  // optimization pass
  if (swap_params_ == nullptr && mgr_ != nullptr) {
    swap_params_ = &mgr_->getGlobalSwapParams();
  }

  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, 0);

  mgr_->resortSegments();

  // Get candidate cells.
  std::vector<Node*> candidates = mgr_->getSingleHeightCells();
  mgr_->shuffle(candidates);

  // Wirelength objective.
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);  // Ignore orientation.
  active_hpwl_obj_ = &hpwlObj;

  double currHpwl = hpwlObj.curr();
  const double initHpwl = currHpwl;
  if (!is_profiling_pass_ && consume_handoff_
      && (!incoming_hot_segments_.empty()
          || !incoming_source_edge_hot_cells_.empty())) {
    std::vector<uint8_t> frontier_nodes(network_->getNumNodes(), 0);
    std::vector<Node*> frontier_cell_list;
    auto add_frontier_node = [&](Node* node) {
      if (!isMovableStdCell(node)) {
        return;
      }
      const int node_id = node->getId();
      if (node_id < 0 || node_id >= static_cast<int>(frontier_nodes.size())
          || frontier_nodes[node_id] != 0) {
        return;
      }
      frontier_nodes[node_id] = 1;
      frontier_cell_list.push_back(node);
    };
    for (const int seg_id : incoming_hot_segments_) {
      if (seg_id < 0 || seg_id >= mgr_->getNumSegments()) {
        continue;
      }
      for (Node* node : mgr_->getCellsInSeg(seg_id)) {
        add_frontier_node(node);
      }
    }
    for (int node_id = 0;
         node_id < static_cast<int>(incoming_source_edge_hot_cells_.size());
         node_id++) {
      if (incoming_source_edge_hot_cells_[node_id] == 0) {
        continue;
      }
      add_frontier_node(network_->getNode(node_id));
    }

    struct RankedEdge
    {
      Edge* edge = nullptr;
      double score = 0.0;
      uint64_t hpwl = 0;
    };

    std::unordered_set<int> seen_edges;
    std::vector<RankedEdge> ranked_edges;
    for (Node* node : frontier_cell_list) {
      for (Pin* pin : node->getPins()) {
        Edge* edge = pin == nullptr ? nullptr : pin->getEdge();
        if (edge == nullptr || !seen_edges.insert(edge->getId()).second) {
          continue;
        }
        const int edge_pins = edge->getNumPins();
        if (edge_pins <= 1 || edge_pins >= skipNetsLargerThanThis_) {
          continue;
        }
        const EdgeEndpointStats stats = analyzeEdgeEndpoints(edge);
        int frontier_pin_count = 0;
        int endpoint_frontier_pins = 0;
        for (Pin* edge_pin : edge->getPins()) {
          Node* edge_node = edge_pin == nullptr ? nullptr : edge_pin->getNode();
          if (!isMovableStdCell(edge_node)) {
            continue;
          }
          const int edge_node_id = edge_node->getId();
          if (edge_node_id < 0
              || edge_node_id >= static_cast<int>(frontier_nodes.size())
              || frontier_nodes[edge_node_id] == 0) {
            continue;
          }
          frontier_pin_count++;
          endpoint_frontier_pins += countEndpointDimensions(edge_pin, stats);
        }
        const uint64_t edge_hpwl = hpwlObj.edgeHpwl(edge);
        ranked_edges.push_back(RankedEdge{
            edge,
            static_cast<double>(edge_hpwl)
                + 4000.0 * static_cast<double>(endpoint_frontier_pins)
                + 300.0 * static_cast<double>(frontier_pin_count),
            edge_hpwl});
      }
    }
    std::stable_sort(ranked_edges.begin(),
                     ranked_edges.end(),
                     [](const RankedEdge& lhs, const RankedEdge& rhs) {
                       return lhs.score > rhs.score;
                     });
    if (ranked_edges.size() > kHandoffTopNetCap) {
      ranked_edges.resize(kHandoffTopNetCap);
    }
    handoff_top_nets_
        = std::max(handoff_top_nets_, static_cast<int>(ranked_edges.size()));

    std::unordered_map<int, double> candidate_priority;
    for (const RankedEdge& ranked_edge : ranked_edges) {
      const EdgeEndpointStats stats = analyzeEdgeEndpoints(ranked_edge.edge);
      for (Pin* pin : ranked_edge.edge->getPins()) {
        Node* node = pin == nullptr ? nullptr : pin->getNode();
        if (!isMovableStdCell(node)) {
          continue;
        }
        const int node_id = node->getId();
        const bool frontier_node
            = node_id >= 0 && node_id < static_cast<int>(frontier_nodes.size())
              && frontier_nodes[node_id] != 0;
        const int endpoint_dims = countEndpointDimensions(pin, stats);
        if (!frontier_node && endpoint_dims == 0) {
          continue;
        }
        double priority = static_cast<double>(ranked_edge.hpwl)
                          * (endpoint_dims > 0 ? 1.0 + 0.25 * endpoint_dims
                                               : 0.20);
        if (frontier_node) {
          priority *= 1.15;
        }
        if (node_id >= 0
            && node_id < static_cast<int>(incoming_source_edge_hot_cells_.size())
            && incoming_source_edge_hot_cells_[node_id] != 0) {
          priority += 800.0;
        }
        candidate_priority[node_id] += priority;
      }
    }
    handoff_prioritized_cells_
        = std::max(handoff_prioritized_cells_,
                   static_cast<int>(candidate_priority.size()));
    if (!candidate_priority.empty()) {
      const auto lookup_priority = [&](Node* node) {
        if (node == nullptr) {
          return 0.0;
        }
        const auto it = candidate_priority.find(node->getId());
        return it == candidate_priority.end() ? 0.0 : it->second;
      };
      std::stable_sort(candidates.begin(),
                       candidates.end(),
                       [&](Node* lhs, Node* rhs) {
                         const double lhs_priority = lookup_priority(lhs);
                         const double rhs_priority = lookup_priority(rhs);
                         if (lhs_priority != rhs_priority) {
                           return lhs_priority > rhs_priority;
                         }
                         return lhs != nullptr && rhs != nullptr
                                && lhs->getId() < rhs->getId();
                       });
    }
  }

  // Determine budget constraint based on pass type
  double maxAllowedHpwl;
  if (is_profiling_pass_) {
    // In profiling pass: use generous budget for pure HPWL optimization
    maxAllowedHpwl = initHpwl * 2.0;  // Allow large changes during profiling
    mgr_->getLogger()->info(
        DPL,
        914,
        "Profiling pass: initial HPWL={:.2f}, generous budget={:.2f}",
        initHpwl,
        maxAllowedHpwl);
  } else {
    // In congestion optimization pass: use strict budget from profiling
    maxAllowedHpwl = budget_hpwl_;
    mgr_->getLogger()->info(DPL,
                            915,
                            "Congestion optimization pass: initial "
                            "HPWL={:.2f}, budget={:.2f} (from profiling)",
                            initHpwl,
                            maxAllowedHpwl);
  }

  int moves_since_normalization = 0;
  const int normalization_interval
      = swap_params_ ? swap_params_->normalization_interval : 1000;

  // Consider each candidate cell once.
  for (auto ndi : candidates) {
    last_generated_move_exact_ = false;
    last_generated_move_source_edge_ = false;
    last_generated_move_endpoint_target_ = false;

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
      random_fallback_attempts_++;
      move_generated = generateRandomMove(ndi);
    }

    if (!move_generated) {
      continue;  // No valid move found with either generator
    }

    // Calculate HPWL delta
    double hpwl_delta = hpwlObj.delta(mgr_->getJournal());
    double nextHpwl = currHpwl - hpwl_delta;  // Projected HPWL after this move

    // Calculate congestion improvement (only relevant in second pass)
    double congestion_improvement = 0.0;
    if (!is_profiling_pass_) {  // Only calculate congestion improvement in
                                // second pass
      const auto& journal = mgr_->getJournal();
      if (!journal.empty()) {
        for (const auto& action_ptr : journal) {
          // Only handle MoveCellAction types
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

          // Get original and new grid coordinates
          const auto* grid = mgr_->getGrid();
          const GridX orig_grid_x = grid->gridX(move_action->getOrigLeft());
          const GridY orig_grid_y
              = grid->gridSnapDownY(move_action->getOrigBottom());
          const GridX new_grid_x = grid->gridX(move_action->getNewLeft());
          const GridY new_grid_y
              = grid->gridSnapDownY(move_action->getNewBottom());

          // Calculate pixel indices (row-major order)
          const int row_site_count = grid->getRowSiteCount().v;
          const int orig_pixel_idx
              = (orig_grid_y.v * row_site_count) + orig_grid_x.v;
          const int new_pixel_idx
              = (new_grid_y.v * row_site_count) + new_grid_x.v;

          // Get utilization densities at original and new locations
          const float orig_density
              = grid->getUtilizationDensity(orig_pixel_idx);
          const float new_density = grid->getUtilizationDensity(new_pixel_idx);

          // Get pre-calculated congestion contribution for this cell
          const double cell_cong_contrib
              = congestion_contribution_[moved_cell->getId()];

          // ΔCongestion = (orig_density - new_density) scaled by the cell's
          // weighted contribution.
          congestion_improvement
              += (orig_density - new_density) * cell_cong_contrib;
        }
      }
    }

    // Hybrid acceptance criteria: budget constraint + combined objective
    if (nextHpwl > maxAllowedHpwl) {
      // Hard constraint violated: reject move regardless of other benefits
      if (last_generated_move_exact_) {
        exact_budget_rejects_++;
      }
      mgr_->rejectMove();
      continue;
    }

    // Within budget: evaluate combined profit
    double combined_profit
        = hpwl_delta + (congestion_weight_ * congestion_improvement);

    if (combined_profit > 0) {
      std::vector<MoveActionSnapshot> accepted_moves;
      std::vector<int> touched_segments;
      captureAcceptedMoveData(mgr_->getJournal(),
                              accepted_moves,
                              touched_segments);
      if (is_profiling_pass_ && profiling_journal_ != nullptr) {
        appendJournal(mgr_->getJournal(), *profiling_journal_);
      }

      // Accept: move is profitable and within budget
      hpwlObj.accept();
      if (!is_profiling_pass_) {
        mgr_->noteHotSegments(touched_segments);
        if (last_generated_move_exact_) {
          exact_moves_accepted_++;
          if (last_generated_move_source_edge_) {
            exact_source_edge_accepts_++;
            std::vector<Node*> source_edge_nodes;
            source_edge_nodes.reserve(accepted_moves.size());
            for (const auto& move : accepted_moves) {
              if (move.node != nullptr) {
                source_edge_nodes.push_back(move.node);
              }
            }
            mgr_->noteSourceEdgeHotCells(source_edge_nodes, touched_segments);
          }
          if (last_generated_move_endpoint_target_) {
            endpoint_accepts_++;
            endpoint_accepted_gain_
                += static_cast<int64_t>(std::llround(hpwl_delta));
          }
        }
      }
      mgr_->acceptMove();
      currHpwl = nextHpwl;

      // Update utilization map for accepted moves (only in congestion
      // optimization pass)
      if (!is_profiling_pass_) {
        for (const auto& move : accepted_moves) {
          if (move.node == nullptr) {
            continue;
          }
          mgr_->getGrid()->updateUtilizationMap(move.node,
                                                move.orig_left,
                                                move.orig_bottom,
                                                false);
          mgr_->getGrid()->updateUtilizationMap(move.node,
                                                move.new_left,
                                                move.new_bottom,
                                                true);
          moves_since_normalization++;
        }
        // Lazy normalization
        if (moves_since_normalization >= normalization_interval) {
          mgr_->getGrid()->normalizeUtilization();
          moves_since_normalization = 0;
        }
      }
    } else {
      if (last_generated_move_exact_) {
        exact_profit_rejects_++;
      }
      mgr_->rejectMove();
    }
  }

  active_hpwl_obj_ = nullptr;

  // Report final statistics
  const double finalDegradation = ((currHpwl - initHpwl) / initHpwl) * 100.0;
  const char* pass_name
      = is_profiling_pass_ ? "Profiling" : "Congestion optimization";
  mgr_->getLogger()->info(DPL,
                          916,
                          "{} pass complete: final HPWL={:.2f}, change={:.1f}%",
                          pass_name,
                          currHpwl,
                          finalDegradation);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::getRange(Node* nd, odb::Rect& nodeBbox)
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
  const double yi = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
  const double xi = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

  if (mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
    return false;
  }
  const int si = mgr_->getReverseCellToSegs(ndi->getId())[0]->getSegId();
  const bool source_edge_cell = isSourceEdgeCell(ndi, si);

  // Determine optimal region.
  odb::Rect bbox;
  if (!getRange(ndi, bbox)) {
    // Failed to find an optimal region.
    return false;
  }
  if (xi >= bbox.xMin() && xi <= bbox.xMax() && yi >= bbox.yMin()
      && yi <= bbox.yMax() && !source_edge_cell) {
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

  if (bbox.xMin() > bbox.xMax() || bbox.yMin() > bbox.yMax()) {
    return false;
  }

  const DbuX center_left{static_cast<int>(std::floor(
      0.5 * (bbox.xMin() + bbox.xMax()) - 0.5 * ndi->getWidth().v))};
  const DbuY center_bottom{static_cast<int>(std::floor(
      0.5 * (bbox.yMin() + bbox.yMax()) - 0.5 * ndi->getHeight().v))};

  if (active_hpwl_obj_ == nullptr) {
    DbuX xj = center_left;
    DbuY yj = center_bottom;
    int rj = arch_->find_closest_row(yj);
    yj = DbuY{arch_->getRow(rj)->getBottom()};
    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }
    if (sj == -1 || ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
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

  ++exact_cells_considered_;
  if (source_edge_cell) {
    ++exact_source_edge_cells_;
  }
  last_generated_move_endpoint_target_ = false;

  const int source_row = mgr_->getSegment(si)->getRowId();
  const double bbox_center_x = 0.5 * (bbox.xMin() + bbox.xMax());
  const double bbox_center_y = 0.5 * (bbox.yMin() + bbox.yMax());
  const DbuX left_edge_left{
      static_cast<int>(std::floor(bbox.xMin() - 0.5 * ndi->getWidth().v))};
  const DbuX right_edge_left{
      static_cast<int>(std::floor(bbox.xMax() - 0.5 * ndi->getWidth().v))};

  const bool frontier_source_cell
      = ndi->getId() >= 0
        && ndi->getId() < static_cast<int>(incoming_source_edge_hot_cells_.size())
        && incoming_source_edge_hot_cells_[ndi->getId()] != 0;
  const bool frontier_hot_segment
      = si >= 0 && si < static_cast<int>(incoming_hot_segment_mask_.size())
        && incoming_hot_segment_mask_[si] != 0;
  std::vector<EndpointRepairHint> endpoint_hints;
  if (!is_profiling_pass_ && consume_handoff_
      && (source_edge_cell || frontier_source_cell || frontier_hot_segment)) {
    for (Pin* pin : ndi->getPins()) {
      Edge* edge = pin == nullptr ? nullptr : pin->getEdge();
      if (edge == nullptr) {
        continue;
      }
      const int edge_pins = edge->getNumPins();
      if (edge_pins <= 1 || edge_pins >= skipNetsLargerThanThis_) {
        continue;
      }
      EndpointRepairHint hint;
      if (!buildEndpointRepairHint(ndi,
                                   pin,
                                   analyzeEdgeEndpoints(edge),
                                   active_hpwl_obj_->edgeHpwl(edge),
                                   hint)) {
        continue;
      }
      endpoint_hints.push_back(hint);
    }
    std::stable_sort(endpoint_hints.begin(),
                     endpoint_hints.end(),
                     [](const EndpointRepairHint& lhs,
                        const EndpointRepairHint& rhs) {
                       return lhs.priority > rhs.priority;
                     });
    if (endpoint_hints.size() > kEndpointHintsPerCellCap) {
      endpoint_hints.resize(kEndpointHintsPerCellCap);
    }
    if (!endpoint_hints.empty()) {
      endpoint_target_cells_++;
      endpoint_hint_count_ += static_cast<int>(endpoint_hints.size());
    }
  }

  std::vector<int> candidate_rows;
  auto add_row = [&](const int row_id) {
    if (row_id < 0 || row_id >= arch_->getNumRows()
        || std::find(candidate_rows.begin(), candidate_rows.end(), row_id)
               != candidate_rows.end()) {
      return;
    }
    candidate_rows.push_back(row_id);
  };

  const int row_lo = arch_->find_closest_row(DbuY{bbox.yMin()});
  const int row_hi = arch_->find_closest_row(DbuY{bbox.yMax()});
  const int row_mid = arch_->find_closest_row(center_bottom);
  add_row(source_row);
  for (const EndpointRepairHint& hint : endpoint_hints) {
    const int hint_row = arch_->find_closest_row(hint.target_bottom);
    add_row(hint_row);
    add_row(hint_row - 1);
    add_row(hint_row + 1);
  }
  add_row(row_lo);
  add_row(row_mid);
  add_row(row_hi);
  if (std::abs(row_hi - row_lo) <= 4) {
    for (int row = std::min(row_lo, row_hi); row <= std::max(row_lo, row_hi);
         row++) {
      add_row(row);
    }
  }
  if (source_edge_cell) {
    add_row(source_row - 1);
    add_row(source_row + 1);
  }

  std::vector<ExactTarget> targets;
  auto add_target = [&](DetailedSeg* seg,
                        const DbuX raw_left,
                        const DbuY row_bottom,
                        const double kind_penalty,
                        const bool from_endpoint_hint) {
    if (seg == nullptr || ndi->getGroupId() != seg->getRegId()) {
      return;
    }
    const DbuX min_left = seg->getMinX();
    const DbuX max_left = seg->getMaxX() - ndi->getWidth();
    if (max_left < min_left) {
      return;
    }
    const DbuX left = std::min(std::max(raw_left, min_left), max_left);
    if (hasTarget(targets, seg->getSegId(), left, row_bottom)) {
      return;
    }
    const double cand_center_x = left.v + 0.5 * ndi->getWidth().v;
    const double cand_center_y = row_bottom.v + 0.5 * ndi->getHeight().v;
    const double x_penalty = intervalDistance(
        static_cast<int>(std::llround(cand_center_x)), bbox.xMin(), bbox.xMax());
    const double y_penalty = intervalDistance(
        static_cast<int>(std::llround(cand_center_y)), bbox.yMin(), bbox.yMax());
    const double center_bias
        = 0.05 * std::abs(cand_center_x - bbox_center_x)
          + 0.05 * std::abs(cand_center_y - bbox_center_y);
    targets.push_back(
        ExactTarget{seg->getSegId(), left, row_bottom, x_penalty + y_penalty
                                                         + center_bias
                                                         + kind_penalty,
                    from_endpoint_hint});
  };

  const int per_row_limit = source_edge_cell ? 3 : 2;
  for (const EndpointRepairHint& hint : endpoint_hints) {
    const int row_center = arch_->find_closest_row(hint.target_bottom);
    for (int row_delta = 0; row_delta <= 1; row_delta++) {
      for (const int sign : {-1, 1}) {
        const int row_id = row_delta == 0 ? row_center
                                          : row_center + sign * row_delta;
        if (row_id < 0 || row_id >= arch_->getNumRows()) {
          continue;
        }
        const DbuY row_bottom{arch_->getRow(row_id)->getBottom()};
        std::vector<std::pair<double, DetailedSeg*>> ranked_segments;
        ranked_segments.reserve(mgr_->getNumSegsInRow(row_id));
        for (int s = 0; s < mgr_->getNumSegsInRow(row_id); s++) {
          DetailedSeg* seg = mgr_->getSegsInRow(row_id)[s];
          if (seg == nullptr || ndi->getGroupId() != seg->getRegId()) {
            continue;
          }
          const DbuX min_left = seg->getMinX();
          const DbuX max_left = seg->getMaxX() - ndi->getWidth();
          if (max_left < min_left) {
            continue;
          }
          const DbuX clamped_left
              = std::min(std::max(hint.target_left, min_left), max_left);
          ranked_segments.emplace_back(std::abs(clamped_left.v - hint.target_left.v),
                                       seg);
        }
        std::stable_sort(ranked_segments.begin(),
                         ranked_segments.end(),
                         [](const auto& lhs, const auto& rhs) {
                           return lhs.first < rhs.first;
                         });
        const int segment_limit
            = std::min<int>(hint.endpoint_dims >= 2 ? 2 : 1,
                            ranked_segments.size());
        for (int idx = 0; idx < segment_limit; idx++) {
          add_target(ranked_segments[idx].second,
                     hint.target_left,
                     row_bottom,
                     -std::min(2500.0,
                               0.015 * static_cast<double>(hint.edge_hpwl))
                         + 4.0 * row_delta,
                     true);
        }
      }
    }
  }
  for (const int row_id : candidate_rows) {
    const DbuY row_bottom{arch_->getRow(row_id)->getBottom()};
    std::vector<std::pair<double, DetailedSeg*>> ranked_segments;
    ranked_segments.reserve(mgr_->getNumSegsInRow(row_id));
    for (int s = 0; s < mgr_->getNumSegsInRow(row_id); s++) {
      DetailedSeg* seg = mgr_->getSegsInRow(row_id)[s];
      if (seg == nullptr || ndi->getGroupId() != seg->getRegId()) {
        continue;
      }
      const DbuX min_left = seg->getMinX();
      const DbuX max_left = seg->getMaxX() - ndi->getWidth();
      if (max_left < min_left) {
        continue;
      }
      const DbuX clamped_center
          = std::min(std::max(center_left, min_left), max_left);
      const double score = intervalDistance(
          static_cast<int>(std::llround(clamped_center.v
                                        + 0.5 * ndi->getWidth().v)),
          bbox.xMin(),
          bbox.xMax());
      ranked_segments.emplace_back(score, seg);
    }

    std::stable_sort(ranked_segments.begin(),
                     ranked_segments.end(),
                     [](const auto& lhs, const auto& rhs) {
                       return lhs.first < rhs.first;
                     });

    const int segment_limit
        = std::min<int>(per_row_limit, ranked_segments.size());
    for (int idx = 0; idx < segment_limit; idx++) {
      DetailedSeg* seg = ranked_segments[idx].second;
      add_target(seg, center_left, row_bottom, 0.0, false);
      add_target(seg, left_edge_left, row_bottom, 0.25, false);
      add_target(seg, right_edge_left, row_bottom, 0.25, false);
    }

    if (source_edge_cell && row_id == source_row) {
      DetailedSeg* seg = mgr_->getSegment(si);
      add_target(seg, seg->getMinX(), row_bottom, 0.10, false);
      add_target(seg, seg->getMaxX() - ndi->getWidth(), row_bottom, 0.10, false);
    }
  }

  if (targets.empty()) {
    return false;
  }

  std::stable_sort(
      targets.begin(),
      targets.end(),
      [](const ExactTarget& lhs, const ExactTarget& rhs) {
        return lhs.cheap_score < rhs.cheap_score;
      });
  const size_t top_k
      = !endpoint_hints.empty() ? kEndpointTargetCap
                                : (source_edge_cell ? 8 : 4);
  if (targets.size() > top_k) {
    targets.resize(top_k);
  }
  exact_candidates_enumerated_ += static_cast<int>(targets.size());
  endpoint_target_candidates_ += static_cast<int>(
      std::count_if(targets.begin(), targets.end(), [](const ExactTarget& target) {
        return target.from_endpoint_hint;
      }));

  Journal best_journal(mgr_->getGrid(), mgr_);
  double best_delta = 0.0;
  bool best_from_endpoint_hint = false;
  for (const ExactTarget& target : targets) {
    if (target.seg_id == si && target.left == ndi->getLeft()
        && target.bottom == ndi->getBottom()) {
      continue;
    }
    const bool move_generated
        = mgr_->tryMove(ndi,
                        ndi->getLeft(),
                        ndi->getBottom(),
                        si,
                        target.left,
                        target.bottom,
                        target.seg_id)
          || mgr_->trySwap(ndi,
                           ndi->getLeft(),
                           ndi->getBottom(),
                           si,
                           target.left,
                           target.bottom,
                           target.seg_id);
    if (!move_generated) {
      continue;
    }
    ++exact_candidates_scored_;
    const double hpwl_delta = active_hpwl_obj_->delta(mgr_->getJournal());
    if (hpwl_delta > 0.0) {
      ++exact_positive_candidates_;
    }
    if (hpwl_delta > best_delta
        || (hpwl_delta == best_delta && target.from_endpoint_hint
            && !best_from_endpoint_hint)) {
      best_delta = hpwl_delta;
      best_from_endpoint_hint = target.from_endpoint_hint;
      best_journal.clear();
      appendJournal(mgr_->getJournal(), best_journal);
    }
    mgr_->rejectMove();
  }

  if (best_journal.empty() || best_delta <= 0.0) {
    return false;
  }

  best_journal.redo();
  mgr_->getJournal().clear();
  appendJournal(best_journal, mgr_->getJournal());
  ++exact_moves_staged_;
  last_generated_move_exact_ = true;
  last_generated_move_source_edge_ = source_edge_cell;
  last_generated_move_endpoint_target_ = best_from_endpoint_hint;
  return true;
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
  last_generated_move_exact_ = false;
  last_generated_move_source_edge_ = false;
  last_generated_move_endpoint_target_ = false;
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
  active_hpwl_obj_ = &hpwlObj;

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

  active_hpwl_obj_ = nullptr;

  if (valid_samples == 0) {
    active_hpwl_obj_ = nullptr;
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
