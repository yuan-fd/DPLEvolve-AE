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
#include <utility>
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

enum class EliteExactSeedKind
{
  kGeneric = 0,
  kAnchor = 1,
  kLateTouched = 2
};

struct EliteExactRankedCell
{
  Node* node = nullptr;
  odb::Rect bbox;
  DbuX target_x{0};
  DbuY target_y{0};
  DbuX original_x{0};
  DbuY original_y{0};
  int residual_sites = 0;
  int residual_rows = 0;
  int priority = 0;
  int outside_distance = 0;
  int target_distance = 0;
  int tail_distance = 0;
  int scorable_edges = 0;
  int late_touched_edge_count = 0;
  uint64_t marginal_hpwl = 0;
  uint64_t late_marginal_hpwl = 0;
  EliteExactSeedKind seed_kind = EliteExactSeedKind::kGeneric;
  double accepted_delta_dbu = 0.0;
};

struct EliteExactTrial
{
  DbuX x{0};
  DbuY y{0};
  int seg_id = -1;
  int cheap_rank = 0;
};

struct EliteExactReplayAction
{
  Node* node = nullptr;
  DbuX current_x{0};
  DbuY current_y{0};
  int current_seg = -1;
  EliteExactTrial trial;
  bool is_swap = false;
};

struct EliteExactBeamState
{
  std::vector<EliteExactReplayAction> actions;
  double total_delta = 0.0;
  uint64_t signature = 0;
};

struct EliteExactStats
{
  int stored_anchors = 0;
  int late_touched_cells = 0;
  int late_touched_nets = 0;
  int late_touched_ranked_cells = 0;
  int anchor_ranked_cells = 0;
  int generic_ranked_cells = 0;
  int mapped_cells = 0;
  int generated_candidates = 0;
  int topk_candidates = 0;
  int exact_scored = 0;
  int legal_trials = 0;
  int move_commits = 0;
  int swap_commits = 0;
  int rejected_trials = 0;
  int failed_trials = 0;
  int expanded_mapped_cells = 0;
  int scoring_batches = 0;
  int expanded_batches = 0;
  int max_batch_size = 0;
  int max_effective_topk = 0;
  int early_stop_zero_commit = 0;
  int early_stop_low_accept_rate = 0;
  int beam_components = 0;
  int beam_branch_trials = 0;
  int beam_candidate_states = 0;
  int beam_commits = 0;
  int beam_committed_cells = 0;
  int beam_replay_failures = 0;
  int beam_depth_peak = 0;
  int beam_width_peak = 0;
  int beam_trial_cache_hits = 0;
  int beam_trial_cache_misses = 0;
  int64_t cache_hits = 0;
  int64_t cache_misses = 0;
  double accepted_delta_dbu = 0.0;
  double beam_accepted_delta_dbu = 0.0;
};

bool addUniqueExactTrial(std::vector<EliteExactTrial>& trials,
                         const EliteExactTrial& trial)
{
  for (const auto& existing : trials) {
    if (existing.seg_id == trial.seg_id && existing.x == trial.x
        && existing.y == trial.y) {
      return false;
    }
  }
  trials.push_back(trial);
  return true;
}

int clampRowId(const Architecture* arch, int row_id)
{
  if (arch == nullptr || arch->getNumRows() <= 0) {
    return -1;
  }
  return std::max(0, std::min(arch->getNumRows() - 1, row_id));
}

void addUniqueRow(std::vector<int>& rows, const Architecture* arch, int row_id)
{
  row_id = clampRowId(arch, row_id);
  if (row_id < 0) {
    return;
  }
  if (std::find(rows.begin(), rows.end(), row_id) == rows.end()) {
    rows.push_back(row_id);
  }
}

int outsideDistanceToBox(const Node* node, const odb::Rect& bbox)
{
  const int x = node->getLeft().v;
  const int y = node->getBottom().v;
  const int dx = x < bbox.xMin() ? bbox.xMin() - x
                                 : (x > bbox.xMax() ? x - bbox.xMax() : 0);
  const int dy = y < bbox.yMin() ? bbox.yMin() - y
                                 : (y > bbox.yMax() ? y - bbox.yMax() : 0);
  return dx + (2 * dy);
}

void limitBoxToDisplacement(DetailedMgr* mgr, Node* node, odb::Rect& bbox)
{
  int disp_x = 0;
  int disp_y = 0;
  mgr->getMaxDisplacement(disp_x, disp_y);
  odb::Rect lbox(node->getLeft().v - disp_x,
                 node->getBottom().v - disp_y,
                 node->getLeft().v + disp_x,
                 node->getBottom().v + disp_y);
  if (lbox.xMax() <= bbox.xMin()) {
    bbox.set_xlo(node->getLeft().v);
    bbox.set_xhi(lbox.xMax());
  } else if (lbox.xMin() >= bbox.xMax()) {
    bbox.set_xlo(lbox.xMin());
    bbox.set_xhi(node->getLeft().v);
  } else {
    bbox.set_xlo(std::max(bbox.xMin(), lbox.xMin()));
    bbox.set_xhi(std::min(bbox.xMax(), lbox.xMax()));
  }
  if (lbox.yMax() <= bbox.yMin()) {
    bbox.set_ylo(node->getBottom().v);
    bbox.set_yhi(lbox.yMax());
  } else if (lbox.yMin() >= bbox.yMax()) {
    bbox.set_ylo(lbox.yMin());
    bbox.set_yhi(node->getBottom().v);
  } else {
    bbox.set_ylo(std::max(bbox.yMin(), lbox.yMin()));
    bbox.set_yhi(std::min(bbox.yMax(), lbox.yMax()));
  }
}

bool snapTrialToRow(DetailedMgr* mgr,
                    Architecture* arch,
                    Node* node,
                    int row_id,
                    DbuX target_x,
                    EliteExactTrial& trial)
{
  row_id = clampRowId(arch, row_id);
  if (row_id < 0) {
    return false;
  }

  const DbuY row_bottom = arch->getRow(row_id)->getBottom();
  bool found = false;
  int best_cost = std::numeric_limits<int>::max();
  EliteExactTrial best;
  for (DetailedSeg* seg : mgr->getSegsInRow(row_id)) {
    if (seg == nullptr || seg->getRegId() != node->getGroupId()) {
      continue;
    }
    if (seg->getMaxX() - seg->getMinX() < node->getWidth()) {
      continue;
    }
    DbuX aligned_x = target_x;
    if (!mgr->alignPos(node, aligned_x, seg->getMinX(), seg->getMaxX())) {
      continue;
    }
    const int cost = std::abs((aligned_x - target_x).v);
    if (!found || cost < best_cost) {
      best = EliteExactTrial{aligned_x, row_bottom, seg->getSegId(), 0};
      best_cost = cost;
      found = true;
    }
  }

  if (!found) {
    return false;
  }
  trial = best;
  return true;
}

std::vector<int> buildEliteRowSamples(Architecture* arch,
                                      const EliteExactRankedCell& candidate,
                                      int row_samples)
{
  std::vector<int> rows;
  if (arch == nullptr || arch->getNumRows() <= 0) {
    return rows;
  }

  const Node* node = candidate.node;
  const int current_row = clampRowId(arch, arch->find_closest_row(node->getBottom()));
  const int target_row = clampRowId(arch, arch->find_closest_row(candidate.target_y));
  const int orig_row = clampRowId(arch, arch->find_closest_row(candidate.original_y));
  const int low_row
      = clampRowId(arch, arch->find_closest_row(DbuY{candidate.bbox.yMin()}));
  const DbuY high_y{
      std::max(candidate.bbox.yMin(),
               candidate.bbox.yMax() - node->getHeight().v)};
  const int high_row = clampRowId(arch, arch->find_closest_row(high_y));
  const DbuY center_y{(candidate.bbox.yMin() + high_y.v) / 2};
  const int center_row = clampRowId(arch, arch->find_closest_row(center_y));

  const std::vector<int> seed_rows = {current_row,
                                      target_row,
                                      orig_row,
                                      center_row,
                                      low_row,
                                      high_row,
                                      target_row - candidate.residual_rows,
                                      target_row + candidate.residual_rows,
                                      target_row - 1,
                                      target_row + 1,
                                      center_row - 1,
                                      center_row + 1};
  for (const int row : seed_rows) {
    addUniqueRow(rows, arch, row);
    if (static_cast<int>(rows.size()) >= row_samples) {
      break;
    }
  }
  return rows;
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
  bool elite_exact = false;
  int elite_limit = 384;
  int elite_topk = 6;
  int elite_rows = 6;

  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    } else if (args[i] == "-x" && i + 1 < args.size()) {
      tradeoff_ = std::atof(args[++i].c_str());
    } else if (args[i] == "-elite_exact") {
      elite_exact = true;
    } else if (args[i] == "-elite_limit" && i + 1 < args.size()) {
      elite_limit = std::atoi(args[++i].c_str());
    } else if (args[i] == "-elite_topk" && i + 1 < args.size()) {
      elite_topk = std::atoi(args[++i].c_str());
    } else if (args[i] == "-elite_rows" && i + 1 < args.size()) {
      elite_rows = std::atoi(args[++i].c_str());
    }
  }
  passes = std::max(passes, 1);
  tol = elite_exact ? std::max(tol, 1e-4) : std::max(tol, 0.01);
  tradeoff_ = std::max(0.0, std::min(1.0, tradeoff_));  // Clamp to [0.0, 1.0]
  elite_limit = std::max(elite_limit, 1);
  elite_topk = std::max(elite_topk, 1);
  elite_rows = std::max(elite_rows, 3);

  if (elite_exact) {
    eliteExactGlobalSwap(passes, tol, elite_limit, elite_topk, elite_rows);
    return;
  }

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

  // Intensity is a 0..1 knob that smart-tapers the effect of the alternate
  // global swap. If the score is 0, fall back to legacy behavior to avoid
  // perturbing low-util designs.
  extra_dpl_intensity_ = std::clamp(combined_score, 0.0, 1.0);
  extra_dpl_alpha_ = extra_dpl_intensity_ * extra_dpl_intensity_;

  if (extra_dpl_intensity_ <= 0.0) {
    mgr_->getLogger()->info(DPL,
                            905,
                            "Extra DPL enabled but intensity=0 "
                            "(stdcell_util={:.3f}, util_score={:.2f}, utilmap: "
                            "mean={:.3f} p95={:.3f} "
                            "p99={:.3f} frac>0.80={:.3f} frac>0.90={:.3f}); "
                            "using legacy global swap.",
                            stdcell_utilization,
                            util_score,
                            density_stats.mean,
                            density_stats.p95,
                            density_stats.p99,
                            density_stats.frac_gt_080,
                            density_stats.frac_gt_090);
    legacy::DetailedGlobalSwap legacy_swap;
    legacy_swap.run(mgrPtr, args);
    return;
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

  double currHpwl = hpwlObj.curr();
  const double initHpwl = currHpwl;

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
      mgr_->rejectMove();
      continue;
    }

    // Within budget: evaluate combined profit
    double combined_profit
        = hpwl_delta + (congestion_weight_ * congestion_improvement);

    if (combined_profit > 0) {
      if (is_profiling_pass_ && profiling_journal_ != nullptr) {
        const auto& journal = mgr_->getJournal();
        for (const auto& action_ptr : journal) {
          if (action_ptr == nullptr) {
            continue;
          }
          switch (action_ptr->typeId()) {
            case JournalActionTypeEnum::MOVE_CELL: {
              const auto* move_action
                  = static_cast<const MoveCellAction*>(action_ptr.get());
              profiling_journal_->addAction(
                  MoveCellAction(move_action->getNode(),
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
              profiling_journal_->addAction(UnplaceCellAction(
                  unplace_action->getNode(), unplace_action->wasHold()));
              break;
            }
          }
        }
      }

      // Accept: move is profitable and within budget
      hpwlObj.accept();
      mgr_->acceptMove();
      currHpwl = nextHpwl;

      // Update utilization map for accepted moves (only in congestion
      // optimization pass)
      if (!is_profiling_pass_) {
        const auto& journal = mgr_->getJournal();
        if (!journal.empty()) {
          for (const auto& action_ptr : journal) {
            if (action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
              continue;
            }

            const MoveCellAction* move_action
                = static_cast<const MoveCellAction*>(action_ptr.get());
            Node* moved_cell = move_action->getNode();
            if (!moved_cell) {
              continue;
            }

            // Remove cell from old location and add to new location
            mgr_->getGrid()->updateUtilizationMap(moved_cell,
                                                  move_action->getOrigLeft(),
                                                  move_action->getOrigBottom(),
                                                  false);
            mgr_->getGrid()->updateUtilizationMap(moved_cell,
                                                  move_action->getNewLeft(),
                                                  move_action->getNewBottom(),
                                                  true);

            moves_since_normalization++;
          }
        }
        // Lazy normalization
        if (moves_since_normalization >= normalization_interval) {
          mgr_->getGrid()->normalizeUtilization();
          moves_since_normalization = 0;
        }
      }
    } else {
      mgr_->rejectMove();
    }
  }

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

//////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::eliteExactGlobalSwap(int passes,
                                              double tol,
                                              int cell_limit,
                                              int topk,
                                              int row_samples)
{
  EliteExactStats stats;
  stats.stored_anchors = static_cast<int>(mgr_->exactGlobalAnchors().size());
  stats.late_touched_cells = mgr_->lateBroadTouchedCellCount();
  stats.late_touched_nets = mgr_->lateBroadTouchedNetCount();

  uint64_t hpwl_x = 0;
  uint64_t hpwl_y = 0;
  int64_t curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  const int64_t init_hpwl = curr_hpwl;
  if (init_hpwl == 0) {
    return;
  }

  DetailedHPWL hpwl(network_);
  hpwl.init(mgr_, nullptr);
  hpwl.curr();

  std::vector<const DetailedMgr::ExactGlobalAnchor*> sorted_anchors;
  sorted_anchors.reserve(mgr_->exactGlobalAnchors().size());
  for (const auto& anchor : mgr_->exactGlobalAnchors()) {
    sorted_anchors.push_back(&anchor);
  }
  std::stable_sort(sorted_anchors.begin(),
                   sorted_anchors.end(),
                   [](const auto* lhs, const auto* rhs) {
                     if (lhs->priority != rhs->priority) {
                       return lhs->priority > rhs->priority;
                     }
                     if (lhs->accepted_delta_dbu != rhs->accepted_delta_dbu) {
                       return lhs->accepted_delta_dbu > rhs->accepted_delta_dbu;
                     }
                     if (lhs->target_y != rhs->target_y) {
                       return lhs->target_y < rhs->target_y;
                     }
                     return lhs->node->name() < rhs->node->name();
                   });

  std::vector<const DetailedMgr::LateBroadTouchedCell*> sorted_touched;
  sorted_touched.reserve(mgr_->lateBroadTouchedCells().size());
  for (const auto& touched : mgr_->lateBroadTouchedCells()) {
    sorted_touched.push_back(&touched);
  }
  std::stable_sort(sorted_touched.begin(),
                   sorted_touched.end(),
                   [](const auto* lhs, const auto* rhs) {
                     if (lhs->touches != rhs->touches) {
                       return lhs->touches > rhs->touches;
                     }
                     if (lhs->total_shift_rows != rhs->total_shift_rows) {
                       return lhs->total_shift_rows > rhs->total_shift_rows;
                     }
                     if (lhs->total_shift_sites != rhs->total_shift_sites) {
                       return lhs->total_shift_sites > rhs->total_shift_sites;
                     }
                     if (lhs->pin_touches != rhs->pin_touches) {
                       return lhs->pin_touches > rhs->pin_touches;
                     }
                     return lhs->node->name() < rhs->node->name();
                   });

  std::vector<int> touched_slot(network_->getNumNodes(), -1);
  for (int i = 0; i < static_cast<int>(mgr_->lateBroadTouchedCells().size()); ++i) {
    const auto& touched = mgr_->lateBroadTouchedCells()[i];
    if (touched.node == nullptr || touched.node->getId() < 0
        || touched.node->getId() >= static_cast<int>(touched_slot.size())) {
      continue;
    }
    touched_slot[touched.node->getId()] = i;
  }

  const int component_cell_cap = std::max(8, std::min(12, row_samples + 4));
  const int component_neighbor_cap = std::max(6, std::min(10, component_cell_cap));
  const int transaction_cell_cap = 4;
  const int component_net_pin_cap = 24;
  const int component_touched_net_cap = std::max(8, row_samples + 2);
  const int component_row_span_cap = std::max(2, row_samples);
  const int beam_width_cap = 6;
  const int beam_neighbor_trial_cap = std::max(3, std::min(topk, 5));
  std::vector<int> bfs_node_mark(network_->getNumNodes(), 0);
  std::vector<int> bfs_edge_mark(network_->getNumEdges(), 0);
  int bfs_token = 0;

  auto late_touched_priority = [](const DetailedMgr::LateBroadTouchedCell& touched,
                                  int residual_sites,
                                  int residual_rows) {
    return 256 + (64 * std::min(touched.touches, 16))
           + (32 * std::min(touched.total_shift_rows, 8))
           + (12 * std::min(touched.total_shift_sites, 96))
           + (2 * std::min(touched.pin_touches, 64)) + (8 * residual_sites)
           + (24 * residual_rows);
  };

  auto build_late_touched_component = [&](Node* seed) {
    std::vector<const DetailedMgr::LateBroadTouchedCell*> component;
    if (seed == nullptr || seed->getId() < 0
        || seed->getId() >= static_cast<int>(touched_slot.size())) {
      return component;
    }
    const int seed_slot = touched_slot[seed->getId()];
    if (seed_slot < 0) {
      return component;
    }

    ++bfs_token;
    if (bfs_token == 0) {
      std::fill(bfs_node_mark.begin(), bfs_node_mark.end(), 0);
      std::fill(bfs_edge_mark.begin(), bfs_edge_mark.end(), 0);
      bfs_token = 1;
    }

    const int row_height = std::max(1, mgr_->getSingleRowHeight().v);
    const int seed_row = seed->getBottom().v / row_height;
    std::vector<Node*> frontier = {seed};
    bfs_node_mark[seed->getId()] = bfs_token;
    component.push_back(&mgr_->lateBroadTouchedCells()[seed_slot]);

    for (int index = 0;
         index < static_cast<int>(frontier.size())
         && static_cast<int>(component.size()) < component_cell_cap;
         ++index) {
      Node* node = frontier[index];
      if (node == nullptr) {
        continue;
      }
      for (Pin* pin : node->getPins()) {
        Edge* edge = pin == nullptr ? nullptr : pin->getEdge();
        if (edge == nullptr || edge->getId() < 0
            || edge->getId() >= static_cast<int>(bfs_edge_mark.size())) {
          continue;
        }
        if (bfs_edge_mark[edge->getId()] == bfs_token) {
          continue;
        }
        bfs_edge_mark[edge->getId()] = bfs_token;
        if (edge->getNumPins() <= 1 || edge->getNumPins() >= skipNetsLargerThanThis_
            || edge->getNumPins() > component_net_pin_cap) {
          continue;
        }

        std::vector<const DetailedMgr::LateBroadTouchedCell*> neighbors;
        neighbors.reserve(edge->getNumPins());
        for (Pin* edge_pin : edge->getPins()) {
          Node* other = edge_pin == nullptr ? nullptr : edge_pin->getNode();
          if (other == nullptr || other->getId() < 0
              || other->getId() >= static_cast<int>(touched_slot.size())) {
            continue;
          }
          const int slot = touched_slot[other->getId()];
          if (slot < 0) {
            continue;
          }
          const int other_row = other->getBottom().v / row_height;
          if (std::abs(other_row - seed_row) > component_row_span_cap) {
            continue;
          }
          neighbors.push_back(&mgr_->lateBroadTouchedCells()[slot]);
        }
        if (neighbors.size() <= 1
            || static_cast<int>(neighbors.size()) > component_touched_net_cap) {
          continue;
        }
        std::stable_sort(neighbors.begin(),
                         neighbors.end(),
                         [&](const auto* lhs, const auto* rhs) {
                           const int lhs_sites = std::abs(
                               (lhs->target_x - lhs->original_x).v)
                                                 / std::max(1, lhs->node->siteWidth().v);
                           const int rhs_sites = std::abs(
                               (rhs->target_x - rhs->original_x).v)
                                                 / std::max(1, rhs->node->siteWidth().v);
                           const int lhs_rows = std::abs(
                               (lhs->target_y - lhs->original_y).v) / row_height;
                           const int rhs_rows = std::abs(
                               (rhs->target_y - rhs->original_y).v) / row_height;
                           const int lhs_priority
                               = late_touched_priority(*lhs, lhs_sites, lhs_rows);
                           const int rhs_priority
                               = late_touched_priority(*rhs, rhs_sites, rhs_rows);
                           if (lhs_priority != rhs_priority) {
                             return lhs_priority > rhs_priority;
                           }
                           return lhs->node->name() < rhs->node->name();
                         });
        for (const auto* neighbor : neighbors) {
          if (neighbor == nullptr || neighbor->node == nullptr) {
            continue;
          }
          const int node_id = neighbor->node->getId();
          if (bfs_node_mark[node_id] == bfs_token) {
            continue;
          }
          bfs_node_mark[node_id] = bfs_token;
          frontier.push_back(neighbor->node);
          component.push_back(neighbor);
          if (static_cast<int>(component.size()) >= component_cell_cap) {
            break;
          }
        }
      }
    }

    if (component.size() > 1) {
      std::stable_sort(component.begin() + 1,
                       component.end(),
                       [&](const auto* lhs, const auto* rhs) {
                         const int lhs_sites = std::abs(
                             (lhs->target_x - lhs->original_x).v)
                                               / std::max(1, lhs->node->siteWidth().v);
                         const int rhs_sites = std::abs(
                             (rhs->target_x - rhs->original_x).v)
                                               / std::max(1, rhs->node->siteWidth().v);
                         const int lhs_rows = std::abs(
                             (lhs->target_y - lhs->original_y).v) / row_height;
                         const int rhs_rows = std::abs(
                             (rhs->target_y - rhs->original_y).v) / row_height;
                         const int lhs_priority
                             = late_touched_priority(*lhs, lhs_sites, lhs_rows);
                         const int rhs_priority
                             = late_touched_priority(*rhs, rhs_sites, rhs_rows);
                         if (lhs_priority != rhs_priority) {
                           return lhs_priority > rhs_priority;
                         }
                         return lhs->node->name() < rhs->node->name();
                       });
    }
    return component;
  };

  for (int pass = 1; pass <= passes; pass++) {
    const int64_t last_hpwl = curr_hpwl;
    mgr_->resortSegments();

    std::vector<int> seen_nodes(network_->getNumNodes(), 0);
    std::vector<int> committed_node_mark(network_->getNumNodes(), 0);
    std::vector<EliteExactRankedCell> ranked;
    ranked.reserve(std::max(64, cell_limit * 2));

    auto add_ranked = [&](Node* node,
                          int priority,
                          double accepted_delta_dbu,
                          DbuX target_x,
                          DbuY target_y,
                          DbuX original_x,
                          DbuY original_y,
                          int residual_sites,
                          int residual_rows,
                          const EliteExactSeedKind seed_kind) {
      if (node == nullptr || node->getId() < 0
          || node->getId() >= static_cast<int>(seen_nodes.size())
          || seen_nodes[node->getId()] != 0 || node->isFixed()
          || !node->isStdCell() || !node->isPlaced()
          || mgr_->getNumReverseCellToSegs(node->getId()) != 1) {
        return;
      }

      odb::Rect bbox;
      if (!getRange(node, bbox)) {
        return;
      }
      limitBoxToDisplacement(mgr_, node, bbox);
      if (bbox.xMin() > bbox.xMax() || bbox.yMin() > bbox.yMax()) {
        return;
      }

      const int outside_distance = outsideDistanceToBox(node, bbox);
      const int target_distance
          = std::abs((node->getLeft() - target_x).v)
            + (2 * std::abs((node->getBottom() - target_y).v));
      const int tail_distance
          = std::abs((node->getLeft() - original_x).v)
            + (2 * std::abs((node->getBottom() - original_y).v));
      if (outside_distance <= 0 && target_distance <= node->siteWidth().v
          && tail_distance <= node->siteWidth().v
          && seed_kind == EliteExactSeedKind::kGeneric) {
        return;
      }

      int scorable_edges = 0;
      int late_touched_edge_count = 0;
      uint64_t marginal_hpwl = 0;
      uint64_t late_marginal_hpwl = 0;
      for (Pin* pin : node->getPins()) {
        Edge* edge = pin->getEdge();
        if (!hpwl.isScorableEdge(edge)) {
          continue;
        }
        ++scorable_edges;
        const uint64_t edge_hpwl = hpwl.edgeHpwlCached(edge);
        marginal_hpwl += edge_hpwl;
        if (mgr_->isLateBroadTouchedEdge(edge)) {
          ++late_touched_edge_count;
          late_marginal_hpwl += edge_hpwl;
        }
      }

      const int site_width = std::max(1, node->siteWidth().v);
      const int hpwl_priority = static_cast<int>(std::min<uint64_t>(
          (late_marginal_hpwl / site_width) / 8
              + (marginal_hpwl / site_width) / 64,
          2048));

      seen_nodes[node->getId()] = 1;
      ranked.push_back(EliteExactRankedCell{node,
                                            bbox,
                                            target_x,
                                            target_y,
                                            original_x,
                                            original_y,
                                            residual_sites,
                                            residual_rows,
                                            priority + (4 * outside_distance)
                                                + (target_distance / 4)
                                                + (tail_distance / 8)
                                                + (12 * late_touched_edge_count)
                                                + (4 * scorable_edges)
                                                + hpwl_priority,
                                            outside_distance,
                                            target_distance,
                                            tail_distance,
                                            scorable_edges,
                                            late_touched_edge_count,
                                            marginal_hpwl,
                                            late_marginal_hpwl,
                                            seed_kind,
                                            accepted_delta_dbu});
      switch (seed_kind) {
        case EliteExactSeedKind::kLateTouched:
          ++stats.late_touched_ranked_cells;
          break;
        case EliteExactSeedKind::kAnchor:
          ++stats.anchor_ranked_cells;
          break;
        case EliteExactSeedKind::kGeneric:
          ++stats.generic_ranked_cells;
          break;
      }
    };

    auto make_late_touched_candidate
        = [&](const DetailedMgr::LateBroadTouchedCell& touched,
              EliteExactRankedCell& out) {
            Node* node = touched.node;
            if (node == nullptr || node->getId() < 0 || node->isFixed()
                || !node->isStdCell() || !node->isPlaced()
                || mgr_->getNumReverseCellToSegs(node->getId()) != 1) {
              return false;
            }

            odb::Rect bbox;
            if (!getRange(node, bbox)) {
              return false;
            }
            limitBoxToDisplacement(mgr_, node, bbox);
            if (bbox.xMin() > bbox.xMax() || bbox.yMin() > bbox.yMax()) {
              return false;
            }

            const int site_width = std::max(1, node->siteWidth().v);
            const int row_height = std::max(1, mgr_->getSingleRowHeight().v);
            const int residual_sites
                = std::abs((touched.target_x - touched.original_x).v) / site_width;
            const int residual_rows
                = std::abs((touched.target_y - touched.original_y).v) / row_height;
            const int outside_distance = outsideDistanceToBox(node, bbox);
            const int target_distance
                = std::abs((node->getLeft() - touched.target_x).v)
                  + (2 * std::abs((node->getBottom() - touched.target_y).v));
            const int tail_distance
                = std::abs((node->getLeft() - touched.original_x).v)
                  + (2 * std::abs((node->getBottom() - touched.original_y).v));

            int scorable_edges = 0;
            int late_touched_edge_count = 0;
            uint64_t marginal_hpwl = 0;
            uint64_t late_marginal_hpwl = 0;
            for (Pin* pin : node->getPins()) {
              Edge* edge = pin->getEdge();
              if (!hpwl.isScorableEdge(edge)) {
                continue;
              }
              ++scorable_edges;
              const uint64_t edge_hpwl = hpwl.edgeHpwlCached(edge);
              marginal_hpwl += edge_hpwl;
              if (mgr_->isLateBroadTouchedEdge(edge)) {
                ++late_touched_edge_count;
                late_marginal_hpwl += edge_hpwl;
              }
            }

            const int hpwl_priority = static_cast<int>(std::min<uint64_t>(
                (late_marginal_hpwl / site_width) / 8
                    + (marginal_hpwl / site_width) / 64,
                2048));

            out = EliteExactRankedCell{node,
                                       bbox,
                                       touched.target_x,
                                       touched.target_y,
                                       touched.original_x,
                                       touched.original_y,
                                       residual_sites,
                                       residual_rows,
                                       late_touched_priority(
                                           touched, residual_sites, residual_rows)
                                           + (4 * outside_distance)
                                           + (target_distance / 4)
                                           + (tail_distance / 8)
                                           + (12 * late_touched_edge_count)
                                           + (4 * scorable_edges)
                                           + hpwl_priority,
                                       outside_distance,
                                       target_distance,
                                       tail_distance,
                                       scorable_edges,
                                       late_touched_edge_count,
                                       marginal_hpwl,
                                       late_marginal_hpwl,
                                       EliteExactSeedKind::kLateTouched,
                                       0.0};
            return true;
          };

    for (const auto* touched : sorted_touched) {
      if (touched == nullptr || touched->node == nullptr) {
        continue;
      }
      const int site_width = std::max(1, touched->node->siteWidth().v);
      const int row_height = std::max(1, mgr_->getSingleRowHeight().v);
      const int residual_sites
          = std::abs((touched->target_x - touched->original_x).v) / site_width;
      const int residual_rows
          = std::abs((touched->target_y - touched->original_y).v) / row_height;
      const int priority = 256 + (64 * std::min(touched->touches, 16))
                           + (32 * std::min(touched->total_shift_rows, 8))
                           + (12 * std::min(touched->total_shift_sites, 96))
                           + (2 * std::min(touched->pin_touches, 64))
                           + (8 * residual_sites) + (24 * residual_rows);
      add_ranked(touched->node,
                 priority,
                 0.0,
                 touched->target_x,
                 touched->target_y,
                 touched->original_x,
                 touched->original_y,
                 residual_sites,
                 residual_rows,
                 EliteExactSeedKind::kLateTouched);
      if (static_cast<int>(ranked.size()) >= cell_limit * 2) {
        break;
      }
    }

    for (const auto* anchor : sorted_anchors) {
      if (anchor == nullptr) {
        continue;
      }
      add_ranked(anchor->node,
                 anchor->priority,
                 anchor->accepted_delta_dbu,
                 anchor->target_x,
                 anchor->target_y,
                 anchor->original_x,
                 anchor->original_y,
                 anchor->residual_sites,
                 anchor->residual_rows,
                 EliteExactSeedKind::kAnchor);
      if (static_cast<int>(ranked.size()) >= cell_limit * 2) {
        break;
      }
    }

    if (static_cast<int>(ranked.size()) < cell_limit) {
      for (Node* node : mgr_->getSingleHeightCells()) {
        add_ranked(node,
                   0,
                   0.0,
                   node->getLeft(),
                   node->getBottom(),
                   node->getOrigLeft(),
                   node->getOrigBottom(),
                   0,
                   0,
                   EliteExactSeedKind::kGeneric);
        if (static_cast<int>(ranked.size()) >= cell_limit * 2) {
          break;
        }
      }
    }

    std::stable_sort(ranked.begin(),
                     ranked.end(),
                     [](const auto& lhs, const auto& rhs) {
                       if (lhs.priority != rhs.priority) {
                         return lhs.priority > rhs.priority;
                       }
                       if (lhs.seed_kind != rhs.seed_kind) {
                         return static_cast<int>(lhs.seed_kind)
                                > static_cast<int>(rhs.seed_kind);
                       }
                       if (lhs.late_touched_edge_count
                           != rhs.late_touched_edge_count) {
                         return lhs.late_touched_edge_count
                                > rhs.late_touched_edge_count;
                       }
                       if (lhs.late_marginal_hpwl != rhs.late_marginal_hpwl) {
                         return lhs.late_marginal_hpwl > rhs.late_marginal_hpwl;
                       }
                       if (lhs.marginal_hpwl != rhs.marginal_hpwl) {
                         return lhs.marginal_hpwl > rhs.marginal_hpwl;
                       }
                       if (lhs.outside_distance != rhs.outside_distance) {
                         return lhs.outside_distance > rhs.outside_distance;
                       }
                       if (lhs.target_distance != rhs.target_distance) {
                         return lhs.target_distance > rhs.target_distance;
                       }
                       if (lhs.tail_distance != rhs.tail_distance) {
                         return lhs.tail_distance > rhs.tail_distance;
                       }
                       return lhs.node->name() < rhs.node->name();
                     });

    const int base_cell_limit = std::max(1, cell_limit);
    const int expansion_budget = std::min((3 * base_cell_limit) / 4,
                                          std::max(base_cell_limit / 6,
                                                   stats.late_touched_cells / 16));
    const int hard_cell_limit
        = std::min(static_cast<int>(ranked.size()),
                   base_cell_limit + std::max(0, expansion_budget));
    if (static_cast<int>(ranked.size()) > hard_cell_limit) {
      ranked.resize(hard_cell_limit);
    }

    auto compute_effective_topk = [&](const EliteExactRankedCell& candidate) {
      int effective_topk = topk;
      if (candidate.seed_kind != EliteExactSeedKind::kGeneric) {
        effective_topk += 2;
      }
      if (candidate.late_touched_edge_count > 0) {
        effective_topk += 2;
      }
      if (candidate.late_marginal_hpwl > 0) {
        effective_topk += 1;
      }
      if (candidate.seed_kind == EliteExactSeedKind::kLateTouched
          && candidate.late_touched_edge_count >= 2) {
        effective_topk += 2;
      }
      if (candidate.scorable_edges >= 8) {
        effective_topk += 1;
      }
      effective_topk = std::min(effective_topk, topk + 7);
      stats.max_effective_topk
          = std::max(stats.max_effective_topk, effective_topk);
      return effective_topk;
    };

    auto build_trials = [&](const EliteExactRankedCell& candidate,
                            Node* node,
                            const int current_seg,
                            const DbuX current_x,
                            const DbuY current_y,
                            const int trial_cap) {
      const DbuX left_edge{candidate.bbox.xMin()};
      const DbuX right_edge{
          std::max(candidate.bbox.xMin(),
                   candidate.bbox.xMax() - node->getWidth().v)};
      const DbuX center_x{(left_edge.v + right_edge.v) / 2};
      std::vector<DbuX> x_candidates = {candidate.target_x,
                                        left_edge,
                                        center_x,
                                        right_edge,
                                        current_x,
                                        candidate.original_x};
      const int direction_x = candidate.target_x.v - candidate.original_x.v;
      if (direction_x != 0) {
        x_candidates.push_back(DbuX{candidate.target_x.v + (direction_x / 2)});
        x_candidates.push_back(DbuX{candidate.target_x.v + direction_x});
      }
      if (candidate.residual_sites != 0) {
        const int halfway_x = current_x.v
                              - (candidate.residual_sites * node->siteWidth().v)
                                    / 2;
        x_candidates.push_back(DbuX{halfway_x});
      }
      const std::vector<DbuX> x_rank_refs = x_candidates;

      const DbuY center_y{
          (candidate.bbox.yMin()
           + std::max(candidate.bbox.yMin(),
                      candidate.bbox.yMax() - node->getHeight().v))
          / 2};
      const std::vector<int> row_ids
          = buildEliteRowSamples(arch_, candidate, row_samples);

      std::vector<EliteExactTrial> trials;
      trials.reserve(static_cast<int>(row_ids.size()) * x_candidates.size());
      for (const int row_id : row_ids) {
        for (const DbuX x : x_candidates) {
          EliteExactTrial trial;
          if (!snapTrialToRow(mgr_, arch_, node, row_id, x, trial)) {
            continue;
          }
          if (trial.seg_id == current_seg && trial.x == current_x
              && trial.y == current_y) {
            continue;
          }
          int min_x_rank = std::numeric_limits<int>::max();
          for (const DbuX ref_x : x_rank_refs) {
            min_x_rank = std::min(min_x_rank, std::abs((trial.x - ref_x).v));
          }
          trial.cheap_rank = min_x_rank
                             + 2 * std::abs((trial.y - candidate.target_y).v)
                             + std::abs((trial.y - center_y).v)
                             + (std::abs((trial.x - current_x).v) / 8);
          addUniqueExactTrial(trials, trial);
        }
      }
      std::stable_sort(trials.begin(),
                       trials.end(),
                       [](const auto& lhs, const auto& rhs) {
                         if (lhs.cheap_rank != rhs.cheap_rank) {
                           return lhs.cheap_rank < rhs.cheap_rank;
                         }
                         if (lhs.seg_id != rhs.seg_id) {
                           return lhs.seg_id < rhs.seg_id;
                         }
                         if (lhs.y != rhs.y) {
                           return lhs.y < rhs.y;
                         }
                         return lhs.x < rhs.x;
                       });
      if (trial_cap > 0 && static_cast<int>(trials.size()) > trial_cap) {
        trials.resize(trial_cap);
      }
      return trials;
    };

    auto replay_actions = [&](const std::vector<EliteExactReplayAction>& actions) {
      for (const auto& action : actions) {
        if (action.node == nullptr) {
          return false;
        }
        const bool ok
            = action.is_swap
                  ? mgr_->trySwap(action.node,
                                  action.current_x,
                                  action.current_y,
                                  action.current_seg,
                                  action.trial.x,
                                  action.trial.y,
                                  action.trial.seg_id)
                  : mgr_->tryMove(action.node,
                                  action.current_x,
                                  action.current_y,
                                  action.current_seg,
                                  action.trial.x,
                                  action.trial.y,
                                  action.trial.seg_id);
        if (!ok) {
          return false;
        }
      }
      return true;
    };

    auto hash_u64 = [](uint64_t seed, uint64_t value) {
      seed ^= value + 0x9e3779b97f4a7c15ULL + (seed << 6) + (seed >> 2);
      return seed;
    };

    auto action_signature = [&](const std::vector<EliteExactReplayAction>& actions) {
      std::vector<uint64_t> keys;
      keys.reserve(actions.size());
      for (const auto& action : actions) {
        uint64_t key = 1469598103934665603ULL;
        key = hash_u64(key, static_cast<uint64_t>(action.node->getId() + 1));
        key = hash_u64(key, static_cast<uint64_t>(action.trial.seg_id + 1));
        key = hash_u64(key, static_cast<uint64_t>(action.trial.x.v + 1));
        key = hash_u64(key, static_cast<uint64_t>(action.trial.y.v + 1));
        key = hash_u64(key, static_cast<uint64_t>(action.is_swap ? 2 : 1));
        keys.push_back(key);
      }
      std::sort(keys.begin(), keys.end());
      uint64_t signature = 1469598103934665603ULL;
      for (const uint64_t key : keys) {
        signature = hash_u64(signature, key);
      }
      return hash_u64(signature, static_cast<uint64_t>(keys.size() + 1));
    };

    auto better_beam_state = [](const EliteExactBeamState& lhs,
                                const EliteExactBeamState& rhs) {
      if (lhs.total_delta != rhs.total_delta) {
        return lhs.total_delta > rhs.total_delta;
      }
      if (lhs.actions.size() != rhs.actions.size()) {
        return lhs.actions.size() > rhs.actions.size();
      }
      return lhs.signature < rhs.signature;
    };

    const int batch_goal = std::min(256, std::max(96, base_cell_limit / 8));
    int mapped_index = 0;
    while (mapped_index < static_cast<int>(ranked.size())) {
      const bool expanded_batch = mapped_index >= base_cell_limit;
      if (expanded_batch) {
        const int total_commits = stats.move_commits + stats.swap_commits;
        if (total_commits == 0) {
          ++stats.early_stop_zero_commit;
          break;
        }
        const double accept_rate = stats.exact_scored == 0
                                       ? 0.0
                                       : total_commits
                                             / static_cast<double>(stats.exact_scored);
        if (accept_rate < 0.01) {
          ++stats.early_stop_low_accept_rate;
          break;
        }
      }

      const int batch_begin = mapped_index;
      const int batch_end
          = std::min(static_cast<int>(ranked.size()), batch_begin + batch_goal);
      const int batch_commits_before = stats.move_commits + stats.swap_commits;
      const double batch_delta_before = stats.accepted_delta_dbu;
      ++stats.scoring_batches;
      stats.max_batch_size = std::max(stats.max_batch_size,
                                      batch_end - batch_begin);
      if (expanded_batch) {
        ++stats.expanded_batches;
      }

      for (; mapped_index < batch_end; ++mapped_index) {
        const auto& candidate = ranked[mapped_index];
        if (candidate.seed_kind == EliteExactSeedKind::kLateTouched
            && candidate.node->getId() >= 0
            && candidate.node->getId() < static_cast<int>(committed_node_mark.size())
            && committed_node_mark[candidate.node->getId()] != 0) {
          continue;
        }
        ++stats.mapped_cells;
        if (expanded_batch) {
          ++stats.expanded_mapped_cells;
        }

        Node* node = candidate.node;
        const int current_seg
            = mgr_->getReverseCellToSegs(node->getId())[0]->getSegId();
        const DbuX current_x = node->getLeft();
        const DbuY current_y = node->getBottom();

        std::vector<EliteExactTrial> lead_trials
            = build_trials(candidate, node, current_seg, current_x, current_y,
                           compute_effective_topk(candidate));
        stats.generated_candidates += static_cast<int>(lead_trials.size());
        stats.topk_candidates += static_cast<int>(lead_trials.size());

        double best_lead_delta = 0.0;
        EliteExactReplayAction best_lead_action;
        for (const auto& trial : lead_trials) {
          if (mgr_->tryMove(node,
                            current_x,
                            current_y,
                            current_seg,
                            trial.x,
                            trial.y,
                            trial.seg_id)) {
            ++stats.legal_trials;
            ++stats.exact_scored;
            const double delta = hpwl.delta(mgr_->getJournal());
            hpwl.reject();
            mgr_->rejectMove();
            if (delta > best_lead_delta) {
              best_lead_delta = delta;
              best_lead_action = EliteExactReplayAction{
                  node, current_x, current_y, current_seg, trial, false};
            } else {
              ++stats.rejected_trials;
            }
          } else {
            ++stats.failed_trials;
          }

          if (mgr_->trySwap(node,
                            current_x,
                            current_y,
                            current_seg,
                            trial.x,
                            trial.y,
                            trial.seg_id)) {
            ++stats.legal_trials;
            ++stats.exact_scored;
            const double delta = hpwl.delta(mgr_->getJournal());
            hpwl.reject();
            mgr_->rejectMove();
            if (delta > best_lead_delta) {
              best_lead_delta = delta;
              best_lead_action = EliteExactReplayAction{
                  node, current_x, current_y, current_seg, trial, true};
            } else {
              ++stats.rejected_trials;
            }
          } else {
            ++stats.failed_trials;
          }
        }

        if (best_lead_delta <= 0.0) {
          continue;
        }

        std::vector<EliteExactReplayAction> accepted_actions = {best_lead_action};
        double accepted_delta = best_lead_delta;

        if (candidate.seed_kind == EliteExactSeedKind::kLateTouched
            && candidate.late_touched_edge_count > 0) {
          const std::vector<const DetailedMgr::LateBroadTouchedCell*> component
              = build_late_touched_component(node);
          if (component.size() > 1) {
            ++stats.beam_components;
            EliteExactBeamState best_state{
                accepted_actions, accepted_delta, action_signature(accepted_actions)};
            std::vector<EliteExactBeamState> frontier = {best_state};
            std::unordered_map<int, EliteExactRankedCell> component_ranked_cache;
            std::unordered_map<int, std::vector<EliteExactTrial>> component_trial_cache;

            auto has_selected_node = [&](const std::vector<EliteExactReplayAction>& actions,
                                         const Node* selected) {
              for (const auto& action : actions) {
                if (action.node == selected) {
                  return true;
                }
              }
              return false;
            };

            auto get_component_ranked_candidate
                = [&](const DetailedMgr::LateBroadTouchedCell& touched)
                -> EliteExactRankedCell* {
              Node* touched_node = touched.node;
              if (touched_node == nullptr || touched_node->getId() < 0) {
                return nullptr;
              }
              const int node_id = touched_node->getId();
              auto cache_it = component_ranked_cache.find(node_id);
              if (cache_it != component_ranked_cache.end()) {
                return &cache_it->second;
              }
              EliteExactRankedCell ranked_candidate;
              if (!make_late_touched_candidate(touched, ranked_candidate)) {
                return nullptr;
              }
              auto [insert_it, inserted]
                  = component_ranked_cache.emplace(node_id, std::move(ranked_candidate));
              (void) inserted;
              return &insert_it->second;
            };

            auto get_component_trials
                = [&](const EliteExactRankedCell& component_candidate)
                -> const std::vector<EliteExactTrial>& {
              const int node_id = component_candidate.node->getId();
              auto cache_it = component_trial_cache.find(node_id);
              if (cache_it != component_trial_cache.end()) {
                ++stats.beam_trial_cache_hits;
                return cache_it->second;
              }
              ++stats.beam_trial_cache_misses;
              std::vector<EliteExactTrial> trials = build_trials(
                  component_candidate,
                  component_candidate.node,
                  mgr_->getReverseCellToSegs(component_candidate.node->getId())[0]
                      ->getSegId(),
                  component_candidate.node->getLeft(),
                  component_candidate.node->getBottom(),
                  std::min(beam_neighbor_trial_cap,
                           compute_effective_topk(component_candidate)));
              stats.generated_candidates += static_cast<int>(trials.size());
              stats.topk_candidates += static_cast<int>(trials.size());
              auto [insert_it, inserted]
                  = component_trial_cache.emplace(node_id, std::move(trials));
              (void) inserted;
              return insert_it->second;
            };

            for (int depth = 1; depth < transaction_cell_cap; ++depth) {
              std::vector<EliteExactBeamState> next_frontier;
              std::unordered_map<uint64_t, size_t> next_index_by_signature;
              for (const auto& state : frontier) {
                const int component_limit
                    = std::min(static_cast<int>(component.size()),
                               component_neighbor_cap);
                for (int component_index = 1; component_index < component_limit;
                     ++component_index) {
                  const auto* touched_neighbor = component[component_index];
                  if (touched_neighbor == nullptr || touched_neighbor->node == nullptr) {
                    continue;
                  }
                  Node* neighbor = touched_neighbor->node;
                  if (neighbor->getId() >= 0
                      && neighbor->getId()
                             < static_cast<int>(committed_node_mark.size())
                      && committed_node_mark[neighbor->getId()] != 0) {
                    continue;
                  }
                  if (has_selected_node(state.actions, neighbor)) {
                    continue;
                  }

                  EliteExactRankedCell* neighbor_candidate
                      = get_component_ranked_candidate(*touched_neighbor);
                  if (neighbor_candidate == nullptr) {
                    continue;
                  }

                  const auto& neighbor_trials
                      = get_component_trials(*neighbor_candidate);
                  stats.beam_branch_trials
                      += static_cast<int>(neighbor_trials.size());
                  for (const auto& trial : neighbor_trials) {
                    for (const bool is_swap : {false, true}) {
                      mgr_->beginComposedTransaction();
                      const bool replay_ok = replay_actions(state.actions);
                      if (!replay_ok) {
                        hpwl.reject();
                        mgr_->rejectMove();
                        ++stats.beam_replay_failures;
                        mgr_->endComposedTransaction();
                        break;
                      }
                      if (mgr_->getJournal().getAffectedNodes().count(neighbor) != 0) {
                        hpwl.reject();
                        mgr_->rejectMove();
                        mgr_->endComposedTransaction();
                        continue;
                      }

                      const int neighbor_seg
                          = mgr_->getReverseCellToSegs(neighbor->getId())[0]
                                ->getSegId();
                      const DbuX neighbor_x = neighbor->getLeft();
                      const DbuY neighbor_y = neighbor->getBottom();
                      const bool ok
                          = is_swap
                                ? mgr_->trySwap(neighbor,
                                                neighbor_x,
                                                neighbor_y,
                                                neighbor_seg,
                                                trial.x,
                                                trial.y,
                                                trial.seg_id)
                                : mgr_->tryMove(neighbor,
                                                neighbor_x,
                                                neighbor_y,
                                                neighbor_seg,
                                                trial.x,
                                                trial.y,
                                                trial.seg_id);
                      if (ok) {
                        ++stats.legal_trials;
                        ++stats.exact_scored;
                        const double total_delta = hpwl.delta(mgr_->getJournal());
                        if (total_delta > state.total_delta) {
                          EliteExactBeamState child_state = state;
                          child_state.actions.push_back(EliteExactReplayAction{
                              neighbor,
                              neighbor_x,
                              neighbor_y,
                              neighbor_seg,
                              trial,
                              is_swap});
                          child_state.total_delta = total_delta;
                          child_state.signature
                              = action_signature(child_state.actions);
                          auto index_it = next_index_by_signature.find(
                              child_state.signature);
                          if (index_it == next_index_by_signature.end()) {
                            next_index_by_signature.emplace(
                                child_state.signature,
                                next_frontier.size());
                            next_frontier.push_back(std::move(child_state));
                            ++stats.beam_candidate_states;
                          } else if (total_delta
                                     > next_frontier[index_it->second].total_delta) {
                            next_frontier[index_it->second] = std::move(child_state);
                          }
                        } else {
                          ++stats.rejected_trials;
                        }
                        hpwl.reject();
                        mgr_->rejectMove();
                      } else {
                        ++stats.failed_trials;
                      }
                      mgr_->endComposedTransaction();
                    }
                  }
                }
              }

              if (next_frontier.empty()) {
                break;
              }

              std::stable_sort(next_frontier.begin(),
                               next_frontier.end(),
                               better_beam_state);
              if (static_cast<int>(next_frontier.size()) > beam_width_cap) {
                next_frontier.resize(beam_width_cap);
              }
              stats.beam_width_peak = std::max(
                  stats.beam_width_peak,
                  static_cast<int>(next_frontier.size()));
              for (const auto& state : next_frontier) {
                stats.beam_depth_peak = std::max(
                    stats.beam_depth_peak,
                    static_cast<int>(state.actions.size()));
                if (state.total_delta > best_state.total_delta) {
                  best_state = state;
                }
              }
              frontier = std::move(next_frontier);
            }

            if (best_state.total_delta > accepted_delta
                && best_state.actions.size() > accepted_actions.size()) {
              accepted_actions = best_state.actions;
              accepted_delta = best_state.total_delta;
            }
          }
        }

        mgr_->beginComposedTransaction();
        const bool replay_ok = replay_actions(accepted_actions);
        if (!replay_ok) {
          hpwl.reject();
          mgr_->rejectMove();
          ++stats.failed_trials;
          ++stats.beam_replay_failures;
          mgr_->endComposedTransaction();
          continue;
        }

        const double replay_delta = hpwl.delta(mgr_->getJournal());
        if (replay_delta <= 0.0) {
          hpwl.reject();
          mgr_->rejectMove();
          mgr_->endComposedTransaction();
          ++stats.rejected_trials;
          continue;
        }

        hpwl.accept();
        mgr_->acceptMove();
        mgr_->endComposedTransaction();
        stats.accepted_delta_dbu += replay_delta;
        if (accepted_actions.size() > 1) {
          ++stats.beam_commits;
          stats.beam_committed_cells
              += static_cast<int>(accepted_actions.size());
          stats.beam_accepted_delta_dbu += replay_delta;
        }
        for (const auto& action : accepted_actions) {
          if (action.is_swap) {
            ++stats.swap_commits;
          } else {
            ++stats.move_commits;
          }
          if (action.node != nullptr && action.node->getId() >= 0
              && action.node->getId()
                     < static_cast<int>(committed_node_mark.size())) {
            committed_node_mark[action.node->getId()] = 1;
          }
        }
      }

      if (expanded_batch) {
        const int batch_commits
            = (stats.move_commits + stats.swap_commits) - batch_commits_before;
        const double batch_delta
            = stats.accepted_delta_dbu - batch_delta_before;
        if (batch_commits == 0 || batch_delta <= 0.0) {
          ++stats.early_stop_zero_commit;
          break;
        }
      }
    }

    curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
    mgr_->getLogger()->info(
        DPL,
        930,
        "Elite exact global pass {:3d}; hpwl is {:.6e}, mapped cells {}, "
        "expanded {}, top-k trials {}, exact scored {}, commits {} moves {} "
        "swaps {}, beam comps {} trials {} commits {} cells {} depth {} "
        "cache hits {} misses {}.",
        pass,
        static_cast<double>(curr_hpwl),
        stats.mapped_cells,
        stats.expanded_mapped_cells,
        stats.topk_candidates,
        stats.exact_scored,
        stats.move_commits + stats.swap_commits,
        stats.move_commits,
        stats.swap_commits,
        stats.beam_components,
        stats.beam_branch_trials,
        stats.beam_commits,
        stats.beam_committed_cells,
        stats.beam_depth_peak,
        hpwl.deltaCacheHits(),
        hpwl.deltaCacheMisses());

    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / static_cast<double>(last_hpwl)
               <= tol) {
      break;
    }
  }

  mgr_->resortSegments();
  stats.cache_hits = static_cast<int64_t>(hpwl.deltaCacheHits());
  stats.cache_misses = static_cast<int64_t>(hpwl.deltaCacheMisses());
  auto* logger = mgr_->getLogger();
  logger->metric("dpl_evolve__elite_exact_global__stored_anchors",
                 stats.stored_anchors);
  logger->metric("dpl_evolve__elite_exact_global__late_touched_cells",
                 stats.late_touched_cells);
  logger->metric("dpl_evolve__elite_exact_global__late_touched_nets",
                 stats.late_touched_nets);
  logger->metric("dpl_evolve__elite_exact_global__late_touched_ranked_cells",
                 stats.late_touched_ranked_cells);
  logger->metric("dpl_evolve__elite_exact_global__anchor_ranked_cells",
                 stats.anchor_ranked_cells);
  logger->metric("dpl_evolve__elite_exact_global__generic_ranked_cells",
                 stats.generic_ranked_cells);
  logger->metric("dpl_evolve__elite_exact_global__mapped_cells",
                 stats.mapped_cells);
  logger->metric("dpl_evolve__elite_exact_global__expanded_mapped_cells",
                 stats.expanded_mapped_cells);
  logger->metric("dpl_evolve__elite_exact_global__generated_candidates",
                 stats.generated_candidates);
  logger->metric("dpl_evolve__elite_exact_global__topk_candidates",
                 stats.topk_candidates);
  logger->metric("dpl_evolve__elite_exact_global__scoring_batches",
                 stats.scoring_batches);
  logger->metric("dpl_evolve__elite_exact_global__expanded_batches",
                 stats.expanded_batches);
  logger->metric("dpl_evolve__elite_exact_global__max_batch_size",
                 stats.max_batch_size);
  logger->metric("dpl_evolve__elite_exact_global__max_effective_topk",
                 stats.max_effective_topk);
  logger->metric("dpl_evolve__elite_exact_global__exact_scored",
                 stats.exact_scored);
  logger->metric("dpl_evolve__elite_exact_global__legal_trials",
                 stats.legal_trials);
  logger->metric("dpl_evolve__elite_exact_global__move_commits",
                 stats.move_commits);
  logger->metric("dpl_evolve__elite_exact_global__swap_commits",
                 stats.swap_commits);
  logger->metric("dpl_evolve__elite_exact_global__rejected_trials",
                 stats.rejected_trials);
  logger->metric("dpl_evolve__elite_exact_global__failed_trials",
                 stats.failed_trials);
  logger->metric("dpl_evolve__elite_exact_global__early_stop_zero_commit",
                 stats.early_stop_zero_commit);
  logger->metric("dpl_evolve__elite_exact_global__early_stop_low_accept_rate",
                 stats.early_stop_low_accept_rate);
  logger->metric("dpl_evolve__elite_exact_global__beam_components",
                 stats.beam_components);
  logger->metric("dpl_evolve__elite_exact_global__beam_branch_trials",
                 stats.beam_branch_trials);
  logger->metric("dpl_evolve__elite_exact_global__beam_candidate_states",
                 stats.beam_candidate_states);
  logger->metric("dpl_evolve__elite_exact_global__beam_commits",
                 stats.beam_commits);
  logger->metric("dpl_evolve__elite_exact_global__beam_committed_cells",
                 stats.beam_committed_cells);
  logger->metric("dpl_evolve__elite_exact_global__beam_replay_failures",
                 stats.beam_replay_failures);
  logger->metric("dpl_evolve__elite_exact_global__beam_depth_peak",
                 stats.beam_depth_peak);
  logger->metric("dpl_evolve__elite_exact_global__beam_width_peak",
                 stats.beam_width_peak);
  logger->metric("dpl_evolve__elite_exact_global__beam_trial_cache_hits",
                 stats.beam_trial_cache_hits);
  logger->metric("dpl_evolve__elite_exact_global__beam_trial_cache_misses",
                 stats.beam_trial_cache_misses);
  logger->metric("dpl_evolve__elite_exact_global__cache_hits",
                 stats.cache_hits);
  logger->metric("dpl_evolve__elite_exact_global__cache_misses",
                 stats.cache_misses);
  logger->metric("dpl_evolve__elite_exact_global__accepted_delta_dbu",
                 stats.accepted_delta_dbu);
  logger->metric("dpl_evolve__elite_exact_global__beam_accepted_delta_dbu",
                 stats.beam_accepted_delta_dbu);

  const double curr_imp
      = (((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.0);
  logger->info(
      DPL,
      931,
      "Elite exact global complete; objective is {:.6e}, improvement is "
      "{:.2f} percent, late touched {} nets {}, anchors {}, mapped {}, "
      "expanded {}, generated {}, top-k {}, exact scored {}, committed {} "
      "moves {} swaps {}, batches {}, beam comps {} trials {} commits {} "
      "cells {} depth {} cache {} / {}, rejected {}, accepted delta {:.0f} "
      "dbu, beam delta {:.0f} dbu.",
      static_cast<double>(curr_hpwl),
      curr_imp,
      stats.late_touched_cells,
      stats.late_touched_nets,
      stats.stored_anchors,
      stats.mapped_cells,
      stats.expanded_mapped_cells,
      stats.generated_candidates,
      stats.topk_candidates,
      stats.exact_scored,
      stats.move_commits + stats.swap_commits,
      stats.move_commits,
      stats.swap_commits,
      stats.scoring_batches,
      stats.beam_components,
      stats.beam_branch_trials,
      stats.beam_commits,
      stats.beam_committed_cells,
      stats.beam_depth_peak,
      stats.cache_hits,
      stats.cache_misses,
      stats.rejected_trials,
      stats.accepted_delta_dbu,
      stats.beam_accepted_delta_dbu);
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
