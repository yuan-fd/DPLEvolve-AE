// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_global.h"

#include <algorithm>
#include <array>
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

  mgr_->clearHotSegments();

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

  // Intensity is a 0..1 knob that tapers exploration/congestion work, but the
  // exact top-K HPWL descent kernel stays live so dense local hotspots do not
  // get routed back to legacy behavior before they are even scored.
  constexpr double kMinExtraDplIntensity = 0.35;
  extra_dpl_intensity_
      = std::max(kMinExtraDplIntensity, std::clamp(combined_score, 0.0, 1.0));
  extra_dpl_alpha_ = extra_dpl_intensity_ * extra_dpl_intensity_;

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
  mgr_->clearHotSegments();
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
  source_candidates_generated_ = 0;
  source_candidates_exact_scored_ = 0;
  source_candidate_rollbacks_ = 0;
  source_candidate_replays_ = 0;
  source_candidate_replay_failures_ = 0;
  source_candidate_accepted_ = 0;
  source_candidate_accepted_delta_ = 0.0;
  mgr_->clearCriticalNetFrontier();
  std::vector<int> accepted_hot_segments;
  struct CriticalFrontierCandidate
  {
    Node* node = nullptr;
    double delta = 0.0;
    std::vector<int> seg_ids;
  };
  std::vector<CriticalFrontierCandidate> critical_frontier;
  critical_frontier.reserve(256);

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
      move_generated = generateWirelengthOptimalMove(ndi, &hpwlObj);
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
      if (last_move_was_source_topk_) {
        std::vector<Node*> accepted_nodes;
        for (const auto& action_ptr : mgr_->getJournal()) {
          if (action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
            continue;
          }
          const auto* move_action
              = static_cast<const MoveCellAction*>(action_ptr.get());
          if (Node* moved_node = move_action->getNode(); moved_node != nullptr) {
            accepted_nodes.push_back(moved_node);
          }
          for (const int seg_id : move_action->getOrigSegs()) {
            if (seg_id >= 0) {
              accepted_hot_segments.push_back(seg_id);
            }
          }
          for (const int seg_id : move_action->getNewSegs()) {
            if (seg_id >= 0) {
              accepted_hot_segments.push_back(seg_id);
            }
          }
        }
        if (!accepted_nodes.empty()) {
          std::sort(accepted_nodes.begin(),
                    accepted_nodes.end(),
                    [](const Node* lhs, const Node* rhs) {
                      return lhs->getId() < rhs->getId();
                    });
          accepted_nodes.erase(std::unique(accepted_nodes.begin(),
                                           accepted_nodes.end(),
                                           [](const Node* lhs, const Node* rhs) {
                                             return lhs->getId() == rhs->getId();
                                           }),
                               accepted_nodes.end());
          const double per_node_delta
              = hpwl_delta / static_cast<double>(accepted_nodes.size());
          std::vector<int> unique_seg_ids;
          unique_seg_ids.reserve(8);
          for (const auto& action_ptr : mgr_->getJournal()) {
            if (action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
              continue;
            }
            const auto* move_action
                = static_cast<const MoveCellAction*>(action_ptr.get());
            unique_seg_ids.insert(unique_seg_ids.end(),
                                  move_action->getOrigSegs().begin(),
                                  move_action->getOrigSegs().end());
            unique_seg_ids.insert(unique_seg_ids.end(),
                                  move_action->getNewSegs().begin(),
                                  move_action->getNewSegs().end());
          }
          std::sort(unique_seg_ids.begin(), unique_seg_ids.end());
          unique_seg_ids.erase(std::remove_if(unique_seg_ids.begin(),
                                             unique_seg_ids.end(),
                                             [](const int seg_id) {
                                               return seg_id < 0;
                                             }),
                               unique_seg_ids.end());
          unique_seg_ids.erase(std::unique(unique_seg_ids.begin(),
                                           unique_seg_ids.end()),
                               unique_seg_ids.end());
          for (Node* node : accepted_nodes) {
            critical_frontier.push_back({node, per_node_delta, unique_seg_ids});
          }
        }
        source_candidate_accepted_++;
        source_candidate_accepted_delta_ += hpwl_delta;
      }

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

  if (!critical_frontier.empty()) {
    std::sort(critical_frontier.begin(),
              critical_frontier.end(),
              [](const CriticalFrontierCandidate& lhs,
                 const CriticalFrontierCandidate& rhs) {
                if (lhs.delta != rhs.delta) {
                  return lhs.delta > rhs.delta;
                }
                return lhs.node->getId() < rhs.node->getId();
              });
    constexpr int kMaxCriticalNodes = 256;
    if (static_cast<int>(critical_frontier.size()) > kMaxCriticalNodes) {
      critical_frontier.resize(kMaxCriticalNodes);
    }
    std::vector<Node*> critical_nodes;
    critical_nodes.reserve(critical_frontier.size());
    std::vector<int> critical_segments;
    for (const auto& candidate : critical_frontier) {
      critical_nodes.push_back(candidate.node);
      critical_segments.insert(critical_segments.end(),
                               candidate.seg_ids.begin(),
                               candidate.seg_ids.end());
    }
    std::sort(critical_segments.begin(), critical_segments.end());
    critical_segments.erase(std::remove_if(critical_segments.begin(),
                                           critical_segments.end(),
                                           [](const int seg_id) {
                                             return seg_id < 0;
                                           }),
                            critical_segments.end());
    critical_segments.erase(std::unique(critical_segments.begin(),
                                        critical_segments.end()),
                            critical_segments.end());
    mgr_->setHotSegments(std::move(critical_segments));
    mgr_->appendCriticalNetFrontierNodes(critical_nodes);
  } else if (!accepted_hot_segments.empty()) {
    std::sort(accepted_hot_segments.begin(), accepted_hot_segments.end());
    accepted_hot_segments.erase(std::remove_if(accepted_hot_segments.begin(),
                                               accepted_hot_segments.end(),
                                               [](const int seg_id) {
                                                 return seg_id < 0;
                                               }),
                                accepted_hot_segments.end());
    accepted_hot_segments.erase(
        std::unique(accepted_hot_segments.begin(), accepted_hot_segments.end()),
        accepted_hot_segments.end());
    mgr_->setHotSegments(std::move(accepted_hot_segments));
  }

  // Report final statistics
  const double finalDegradation = ((currHpwl - initHpwl) / initHpwl) * 100.0;
  const char* pass_name
      = is_profiling_pass_ ? "Profiling" : "Congestion optimization";
  mgr_->getLogger()->info(DPL,
                          926,
                          "{} source-topk stats: candidates={}, exact_scored={}, "
                          "replays={}, replay_failures={}, rollbacks={}, "
                          "accepts={}, accepted_delta={:.2f}",
                          pass_name,
                          source_candidates_generated_,
                          source_candidates_exact_scored_,
                          source_candidate_replays_,
                          source_candidate_replay_failures_,
                          source_candidate_rollbacks_,
                          source_candidate_accepted_,
                          source_candidate_accepted_delta_);
  mgr_->getLogger()->info(DPL,
                          927,
                          "{} critical-frontier stats: nodes={}, hot_segments={}",
                          pass_name,
                          mgr_->getCriticalNetFrontierNodes().size(),
                          mgr_->getHotSegments().size());
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
bool DetailedGlobalSwap::generateWirelengthOptimalMove(Node* ndi,
                                                       DetailedHPWL* hpwlObj)
{
  last_move_was_source_topk_ = false;
  if (hpwlObj == nullptr) {
    return false;
  }

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
  const int si = mgr_->getReverseCellToSegs(ndi->getId())[0]->getSegId();
  const int width = ndi->getWidth().v;
  const int height = ndi->getHeight().v;
  const int current_center_x = ndi->getCenterX().v;
  const int current_center_y = ndi->getCenterY().v;
  const int bbox_center_x = (bbox.xMin() + bbox.xMax()) / 2;
  const int bbox_center_y = (bbox.yMin() + bbox.yMax()) / 2;
  const int old_dist
      = std::max(0, bbox.xMin() - current_center_x)
        + std::max(0, current_center_x - bbox.xMax())
        + std::max(0, bbox.yMin() - current_center_y)
        + std::max(0, current_center_y - bbox.yMax());

  const int projected_left
      = static_cast<int>(std::floor(0.5 * (bbox.xMin() + bbox.xMax())
                                    - 0.5 * width));
  const int left_anchor = bbox.xMin() - (width / 2);
  const int right_anchor = bbox.xMax() - (width / 2);
  const int projected_bottom
      = static_cast<int>(std::floor(0.5 * (bbox.yMin() + bbox.yMax())
                                    - 0.5 * height));
  const int target_row = arch_->find_closest_row(DbuY{projected_bottom});
  const int row_radius = is_profiling_pass_ ? 1 : (extra_dpl_alpha_ >= 0.35 ? 2 : 1);
  const int top_k = is_profiling_pass_ ? 3 : (extra_dpl_alpha_ >= 0.60 ? 6 : 4);
  const int max_candidates = std::max(top_k, top_k * 4);

  struct Candidate
  {
    DbuX left{0};
    DbuY bottom{0};
    int seg_id = -1;
    int row_id = -1;
    int distance_improvement = 0;
    int center_distance = 0;
    int move_distance = 0;
  };

  std::vector<Candidate> candidates;
  candidates.reserve(max_candidates);
  const std::array<int, 5> row_offsets = {0, -1, 1, -2, 2};
  const std::array<int, 3> raw_anchors = {left_anchor, projected_left, right_anchor};

  for (const int offset : row_offsets) {
    if (std::abs(offset) > row_radius) {
      continue;
    }
    const int row_id = target_row + offset;
    if (row_id < 0 || row_id >= arch_->getNumRows()) {
      continue;
    }

    const DbuY row_bottom = arch_->getRow(row_id)->getBottom();
    if (std::abs((row_bottom - ndi->getBottom()).v) > dispY) {
      continue;
    }

    for (int s = 0; s < mgr_->getNumSegsInRow(row_id); s++) {
      DetailedSeg* seg_ptr = mgr_->getSegsInRow(row_id)[s];
      const int seg_id = seg_ptr->getSegId();
      if (seg_id < 0 || ndi->getGroupId() != mgr_->getSegment(seg_id)->getRegId()) {
        continue;
      }

      for (const int raw_anchor : raw_anchors) {
        DbuX aligned_left{raw_anchor};
        if (!mgr_->alignPos(
                ndi, aligned_left, seg_ptr->getMinX(), seg_ptr->getMaxX())) {
          continue;
        }
        if (aligned_left == ndi->getLeft() && row_bottom == ndi->getBottom()
            && seg_id == si) {
          continue;
        }

        const int dx = std::abs((aligned_left - ndi->getLeft()).v);
        const int dy = std::abs((row_bottom - ndi->getBottom()).v);
        if (dx > dispX || dy > dispY) {
          continue;
        }

        const int cand_center_x = aligned_left.v + (width / 2);
        const int cand_center_y = row_bottom.v + (height / 2);
        const int new_dist
            = std::max(0, bbox.xMin() - cand_center_x)
              + std::max(0, cand_center_x - bbox.xMax())
              + std::max(0, bbox.yMin() - cand_center_y)
              + std::max(0, cand_center_y - bbox.yMax());

        candidates.push_back(Candidate{aligned_left,
                                       row_bottom,
                                       seg_id,
                                       row_id,
                                       old_dist - new_dist,
                                       std::abs(cand_center_x - bbox_center_x)
                                           + std::abs(cand_center_y - bbox_center_y),
                                       dx + dy});
      }
    }
  }

  if (candidates.empty()) {
    return false;
  }

  std::sort(candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              if (lhs.seg_id != rhs.seg_id) {
                return lhs.seg_id < rhs.seg_id;
              }
              if (lhs.row_id != rhs.row_id) {
                return lhs.row_id < rhs.row_id;
              }
              if (lhs.bottom != rhs.bottom) {
                return lhs.bottom < rhs.bottom;
              }
              return lhs.left < rhs.left;
            });
  candidates.erase(std::unique(candidates.begin(),
                               candidates.end(),
                               [](const Candidate& lhs, const Candidate& rhs) {
                                 return lhs.seg_id == rhs.seg_id
                                        && lhs.row_id == rhs.row_id
                                        && lhs.bottom == rhs.bottom
                                        && lhs.left == rhs.left;
                               }),
                   candidates.end());

  std::sort(candidates.begin(),
            candidates.end(),
            [](const Candidate& lhs, const Candidate& rhs) {
              if (lhs.distance_improvement != rhs.distance_improvement) {
                return lhs.distance_improvement > rhs.distance_improvement;
              }
              if (lhs.center_distance != rhs.center_distance) {
                return lhs.center_distance < rhs.center_distance;
              }
              return lhs.move_distance < rhs.move_distance;
            });
  if (candidates.size() > max_candidates) {
    candidates.resize(max_candidates);
  }
  source_candidates_generated_ += candidates.size();

  auto probe_candidate = [&](const Candidate& candidate,
                             double& hpwl_delta,
                             bool& used_swap) -> bool {
    used_swap = false;
    if (mgr_->tryMove(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      candidate.left,
                      candidate.bottom,
                      candidate.seg_id)) {
      hpwl_delta = hpwlObj->delta(mgr_->getJournal());
      ++source_candidates_exact_scored_;
      return true;
    }
    if (mgr_->trySwap(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      candidate.left,
                      candidate.bottom,
                      candidate.seg_id)) {
      hpwl_delta = hpwlObj->delta(mgr_->getJournal());
      used_swap = true;
      ++source_candidates_exact_scored_;
      return true;
    }
    return false;
  };

  const Candidate* best_candidate = nullptr;
  double best_delta = 0.0;
  for (int idx = 0; idx < std::min(top_k, static_cast<int>(candidates.size()));
       idx++) {
    double probe_delta = 0.0;
    bool used_swap = false;
    if (!probe_candidate(candidates[idx], probe_delta, used_swap)) {
      continue;
    }
    mgr_->rejectMove();
    source_candidate_rollbacks_++;
    if (probe_delta > best_delta) {
      best_delta = probe_delta;
      best_candidate = &candidates[idx];
    }
  }

  if (best_candidate == nullptr || best_delta <= 0.0) {
    return false;
  }

  double replay_delta = 0.0;
  bool replay_swap = false;
  if (!probe_candidate(*best_candidate, replay_delta, replay_swap)) {
    source_candidate_replay_failures_++;
    return false;
  }
  if (replay_delta <= 0.0) {
    source_candidate_replay_failures_++;
    mgr_->rejectMove();
    source_candidate_rollbacks_++;
    return false;
  }

  ++source_candidate_replays_;
  last_move_was_source_topk_ = true;
  if (replay_swap) {
    ++swaps_;
  } else {
    ++moves_;
  }
  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generateRandomMove(Node* ndi)
{
  // Generate a random move within the current displacement constraints
  // This is for exploration and power optimization purposes
  last_move_was_source_topk_ = false;

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
  last_move_was_source_topk_ = false;
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);

  // Phase 1: Try wirelength-optimal move (unless we decide to override with
  // exploration)
  if (mgr_->getRandom(1000) >= static_cast<int>(tradeoff_ * 1000)) {
    move_generated = generateWirelengthOptimalMove(ndi, &hpwlObj);
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
