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
constexpr size_t kExactProbeTopK = 6;
constexpr size_t kOrderedPairPartnerTopK = 6;
constexpr size_t kOrderedPairProbeTopK = 3;
constexpr int kOrderedPairAuditEvery = 64;
constexpr size_t kEndpointEscapeEdgeTopK = 1;
constexpr size_t kEndpointEscapePartnerTopK = 1;
constexpr size_t kEndpointEscapeProbeTopK = 1;

double pointToIntervalDistance(const double value, const int lo, const int hi)
{
  if (value < lo) {
    return lo - value;
  }
  if (value > hi) {
    return value - hi;
  }
  return 0.0;
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

  // Intensity is a 0..1 knob that tapers congestion-specific behavior. Low
  // intensity no longer forces a legacy fallback because the exact source-edge
  // HPWL consumer still needs to run on low-util designs.
  extra_dpl_intensity_ = std::clamp(combined_score, 0.0, 1.0);
  extra_dpl_alpha_ = extra_dpl_intensity_ * extra_dpl_intensity_;

  if (extra_dpl_intensity_ <= 0.0) {
    mgr_->getLogger()->info(DPL,
                            905,
                            "Extra DPL intensity=0; running exact HPWL-only "
                            "source-edge global swap "
                            "(stdcell_util={:.3f}, util_score={:.2f}, utilmap: "
                            "mean={:.3f} p95={:.3f} "
                            "p99={:.3f} frac>0.80={:.3f} frac>0.90={:.3f}).",
                            stdcell_utilization,
                            util_score,
                            density_stats.mean,
                            density_stats.p95,
                            density_stats.p99,
                            density_stats.frac_gt_080,
                            density_stats.frac_gt_090);
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

  const bool hpwl_only_exact_mode = extra_dpl_alpha_ <= 0.0;
  if (hpwl_only_exact_mode) {
    mgr_->getLogger()->info(
        DPL,
        917,
        "Keeping {} profiling moves as the start state for HPWL-only exact "
        "continuation",
        profiling_journal.size());
  } else {
    // Restore initial state using Journal's built-in undo mechanism
    mgr_->getLogger()->info(DPL,
                            917,
                            "Undoing {} profiling moves to restore initial "
                            "state",
                            profiling_journal.size());
    profiling_journal.undo();
    mgr_->setRngState(rng_state);
  }
  profiling_journal.clear();
  profiling_journal_ = nullptr;
  mgr_->getJournal().clear();  // Clear journal for second pass

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

  curr_hpwl = hpwl_only_exact_mode ? optimal_hpwl
                                   : Utility::hpwl(network_, hpwl_x, hpwl_y);

  const double min_budget_multiplier = hpwl_only_exact_mode
                                           ? 1.0
                                           : std::max(1.0,
                                                      static_cast<double>(
                                                          init_hpwl)
                                                          / std::max(1.0,
                                                                     optimal_hpwl));

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
  std::ranges::stable_sort(candidates,
                           [](const Node* lhs, const Node* rhs) {
                             return lhs->getSourceEdgeHotness()
                                    > rhs->getSourceEdgeHotness();
                           });

  // Wirelength objective.
  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);  // Ignore orientation.
  active_hpwl_obj_ = &hpwlObj;

  double currHpwl = hpwlObj.curr();
  const double initHpwl = currHpwl;
  exact_probe_cells_ = 0;
  exact_probe_candidates_ = 0;
  exact_probe_scored_ = 0;
  exact_probe_generated_ = 0;
  exact_probe_accepted_ = 0;
  exact_probe_accepted_delta_ = 0.0;
  hot_source_edge_cells_ = 0;
  hot_source_edge_generated_ = 0;
  hot_source_edge_accepted_ = 0;
  hot_source_edge_accepted_delta_ = 0.0;
  ordered_pair_stats_ = {};
  endpoint_escape_stats_ = {};

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
    const bool hot_source_edge = ndi->isSourceEdgeHot();
    if (hot_source_edge) {
      hot_source_edge_cells_++;
    }
    // Hybrid move generation: Smart Swap logic
    bool move_generated = false;
    last_move_used_exact_probe_ = false;
    last_move_used_pair_probe_ = false;
    last_move_used_escape_probe_ = false;

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
    if (hot_source_edge
        && (last_move_used_exact_probe_ || last_move_used_pair_probe_)) {
      hot_source_edge_generated_++;
    }

    // Calculate HPWL delta
    double hpwl_delta = hpwlObj.delta(mgr_->getJournal());
    double nextHpwl = currHpwl - hpwl_delta;  // Projected HPWL after this move

    // Calculate congestion improvement (only relevant in second pass)
    double congestion_improvement = 0.0;
    if (!is_profiling_pass_
        && congestion_weight_ != 0.0) {  // Skip zero-weight bookkeeping in
                                         // exact HPWL-only mode.
      congestion_improvement = calculateCongestionImprovement(mgr_->getJournal());
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
      if (last_move_used_exact_probe_) {
        exact_probe_accepted_++;
        exact_probe_accepted_delta_ += hpwl_delta;
      }
      if (last_move_used_pair_probe_) {
        ordered_pair_stats_.accepted++;
        ordered_pair_stats_.accepted_delta += hpwl_delta;
      }
      if (last_move_used_escape_probe_) {
        endpoint_escape_stats_.accepted++;
        endpoint_escape_stats_.accepted_delta += hpwl_delta;
      }
      if (hot_source_edge
          && (last_move_used_exact_probe_ || last_move_used_pair_probe_)) {
        hot_source_edge_accepted_++;
        hot_source_edge_accepted_delta_ += hpwl_delta;
      }

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
  if (exact_probe_scoring_enabled_) {
    mgr_->getLogger()->info(
        DPL,
        927,
        "{} pass exact source-edge scoring: cells={} raw_candidates={} "
        "exact_scored={} generated={} accepted={} accepted_delta={:.2f} "
        "hot_cells={} hot_generated={} hot_accepted={} "
        "hot_accepted_delta={:.2f} "
        "ordered_pair_stage1={} ordered_pair_candidates={} "
        "ordered_pair_scored={} ordered_pair_cached_overlap_edges={} "
        "ordered_pair_audit_gap={} ordered_pair_promoted={} "
        "ordered_pair_accepted={} ordered_pair_accepted_delta={:.2f} "
        "escape_edges={} escape_partners={} escape_candidates={} "
        "escape_scored={} escape_cached_overlap_edges={} "
        "escape_audit_gap={} escape_promoted={} "
        "escape_accepted={} escape_accepted_delta={:.2f}",
        pass_name,
        exact_probe_cells_,
        exact_probe_candidates_,
        exact_probe_scored_,
        exact_probe_generated_,
        exact_probe_accepted_,
        exact_probe_accepted_delta_,
        hot_source_edge_cells_,
        hot_source_edge_generated_,
        hot_source_edge_accepted_,
        hot_source_edge_accepted_delta_,
        ordered_pair_stats_.stage1_exact,
        ordered_pair_stats_.candidates,
        ordered_pair_stats_.scored,
        ordered_pair_stats_.cached_overlap_edges,
        ordered_pair_stats_.audit_gap,
        ordered_pair_stats_.promoted,
        ordered_pair_stats_.accepted,
        ordered_pair_stats_.accepted_delta,
        endpoint_escape_stats_.edges,
        endpoint_escape_stats_.partners,
        endpoint_escape_stats_.candidates,
        endpoint_escape_stats_.scored,
        endpoint_escape_stats_.cached_overlap_edges,
        endpoint_escape_stats_.audit_gap,
        endpoint_escape_stats_.promoted,
        endpoint_escape_stats_.accepted,
        endpoint_escape_stats_.accepted_delta);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__ordered_pair__stage1_exact",
                              ordered_pair_stats_.stage1_exact);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__ordered_pair__candidates",
                              ordered_pair_stats_.candidates);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__ordered_pair__scored",
                              ordered_pair_stats_.scored);
    mgr_->getLogger()->metric(
        "dpl_evolve__source_edge__ordered_pair__cached_overlap_edges",
        ordered_pair_stats_.cached_overlap_edges);
    mgr_->getLogger()->metric(
        "dpl_evolve__source_edge__ordered_pair__audit_gap",
        ordered_pair_stats_.audit_gap);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__ordered_pair__promoted",
                              ordered_pair_stats_.promoted);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__ordered_pair__accepted",
                              ordered_pair_stats_.accepted);
    mgr_->getLogger()->metric(
        "dpl_evolve__source_edge__ordered_pair__accepted_delta",
        ordered_pair_stats_.accepted_delta);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__edges",
                              endpoint_escape_stats_.edges);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__partners",
                              endpoint_escape_stats_.partners);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__candidates",
                              endpoint_escape_stats_.candidates);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__scored",
                              endpoint_escape_stats_.scored);
    mgr_->getLogger()->metric(
        "dpl_evolve__source_edge__escape__cached_overlap_edges",
        endpoint_escape_stats_.cached_overlap_edges);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__audit_gap",
                              endpoint_escape_stats_.audit_gap);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__promoted",
                              endpoint_escape_stats_.promoted);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__escape__accepted",
                              endpoint_escape_stats_.accepted);
    mgr_->getLogger()->metric(
        "dpl_evolve__source_edge__escape__accepted_delta",
        endpoint_escape_stats_.accepted_delta);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__hot_cells",
                              hot_source_edge_cells_);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__hot_generated",
                              hot_source_edge_generated_);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__hot_accepted",
                              hot_source_edge_accepted_);
    mgr_->getLogger()->metric("dpl_evolve__source_edge__hot_accepted_delta",
                              hot_source_edge_accepted_delta_);
  }
  active_hpwl_obj_ = nullptr;
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
  if (!exact_probe_scoring_enabled_ || active_hpwl_obj_ == nullptr) {
    return trySimpleWirelengthOptimalMove(ndi, bbox, si);
  }

  const int current_row_id = mgr_->getSegment(si)->getRowId();
  const DbuY target_center_y{
      (int) std::floor(0.5 * (bbox.yMin() + bbox.yMax())
                       - 0.5 * ndi->getHeight().v)};
  std::vector<int> candidate_rows{current_row_id,
                                  arch_->find_closest_row(target_center_y),
                                  arch_->find_closest_row(DbuY{bbox.yMin()}),
                                  arch_->find_closest_row(DbuY{bbox.yMax()})};
  std::ranges::sort(candidate_rows);
  candidate_rows.erase(std::unique(candidate_rows.begin(), candidate_rows.end()),
                       candidate_rows.end());
  std::ranges::sort(candidate_rows,
                    [&](const int lhs, const int rhs) {
                      const auto lhs_dist = std::abs(
                          arch_->getRow(lhs)->getBottom().v - target_center_y.v);
                      const auto rhs_dist = std::abs(
                          arch_->getRow(rhs)->getBottom().v - target_center_y.v);
                      return lhs_dist < rhs_dist;
                    });
  if (candidate_rows.size() > 3) {
    candidate_rows.resize(3);
  }

  const DbuX center_target{
      (int) std::floor(0.5 * (bbox.xMin() + bbox.xMax())
                       - 0.5 * ndi->getWidth().v)};
  const DbuX left_edge_target{
      (int) std::floor(bbox.xMin() - 0.5 * ndi->getWidth().v)};
  const DbuX right_edge_target{
      (int) std::floor(bbox.xMax() - 0.5 * ndi->getWidth().v)};

  std::vector<MoveProbe> probes;
  probes.reserve(candidate_rows.size() * 3);
  for (const int row_id : candidate_rows) {
    addProbe(ndi, bbox, row_id, left_edge_target, probes);
    addProbe(ndi, bbox, row_id, center_target, probes);
    addProbe(ndi, bbox, row_id, right_edge_target, probes);
  }
  if (probes.empty()) {
    return trySimpleWirelengthOptimalMove(ndi, bbox, si);
  }

  exact_probe_cells_++;
  exact_probe_candidates_ += probes.size();
  std::ranges::sort(
      probes, [](const MoveProbe& lhs, const MoveProbe& rhs) {
        return lhs.heuristic > rhs.heuristic;
      });
  if (probes.size() > kExactProbeTopK) {
    probes.resize(kExactProbeTopK);
  }

  Journal best_journal(mgr_->getGrid(), mgr_);
  Journal stage1_journal(mgr_->getGrid(), mgr_);
  double best_profit = 0.0;
  bool found = false;
  DetailedHPWL::DeltaCache stage1_cache;
  for (const auto& probe : probes) {
    const DbuX curr_x = ndi->getLeft();
    const DbuY curr_y = ndi->getBottom();
    bool legal = mgr_->tryMove(
        ndi, curr_x, curr_y, si, probe.left, probe.bottom, probe.seg_id);
    if (!legal) {
      legal = mgr_->trySwap(
          ndi, curr_x, curr_y, si, probe.left, probe.bottom, probe.seg_id);
    }
    if (!legal) {
      continue;
    }

    exact_probe_scored_++;
    const double hpwl_delta = active_hpwl_obj_->buildDeltaCache(
        mgr_->getJournal(), stage1_cache);
    const bool zero_congestion_weight = congestion_weight_ == 0.0;
    const double congestion_improvement
        = (is_profiling_pass_ || zero_congestion_weight)
              ? 0.0
              : calculateCongestionImprovement(mgr_->getJournal());
    const double combined_profit
        = hpwl_delta + (congestion_weight_ * congestion_improvement);
    if (!found || combined_profit > best_profit) {
      cloneJournal(mgr_->getJournal(), best_journal);
      best_profit = combined_profit;
      found = true;
    }
    cloneJournal(mgr_->getJournal(), stage1_journal);
    if (tryOrderedPairTransaction(ndi,
                                  curr_x,
                                  curr_y,
                                  si,
                                  stage1_journal,
                                  stage1_cache,
                                  combined_profit,
                                  best_journal,
                                  best_profit,
                                  last_move_used_escape_probe_)) {
      found = true;
      last_move_used_pair_probe_ = true;
    }
    mgr_->rejectMove();
  }

  if (!found || best_journal.empty()) {
    return false;
  }
  best_journal.redo();
  cloneJournal(best_journal, mgr_->getJournal());
  last_move_used_exact_probe_ = true;
  exact_probe_generated_++;
  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::trySimpleWirelengthOptimalMove(Node* ndi,
                                                        const odb::Rect& bbox,
                                                        const int si)
{

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
double DetailedGlobalSwap::calculateCongestionImprovement(
    const Journal& journal) const
{
  double congestion_improvement = 0.0;
  if (journal.empty()) {
    return congestion_improvement;
  }
  for (const auto& action_ptr : journal) {
    if (action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
      continue;
    }

    const MoveCellAction* move_action
        = static_cast<const MoveCellAction*>(action_ptr.get());
    Node* moved_cell = move_action->getNode();
    if (!moved_cell || moved_cell->getId() >= congestion_contribution_.size()) {
      continue;
    }

    const auto* grid = mgr_->getGrid();
    const GridX orig_grid_x = grid->gridX(move_action->getOrigLeft());
    const GridY orig_grid_y
        = grid->gridSnapDownY(move_action->getOrigBottom());
    const GridX new_grid_x = grid->gridX(move_action->getNewLeft());
    const GridY new_grid_y = grid->gridSnapDownY(move_action->getNewBottom());

    const int row_site_count = grid->getRowSiteCount().v;
    const int orig_pixel_idx = (orig_grid_y.v * row_site_count) + orig_grid_x.v;
    const int new_pixel_idx = (new_grid_y.v * row_site_count) + new_grid_x.v;

    const float orig_density = grid->getUtilizationDensity(orig_pixel_idx);
    const float new_density = grid->getUtilizationDensity(new_pixel_idx);
    const double cell_cong_contrib = congestion_contribution_[moved_cell->getId()];
    congestion_improvement
        += (orig_density - new_density) * cell_cong_contrib;
  }
  return congestion_improvement;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedGlobalSwap::calculateSourceEdgeImprovement(Node* ndi,
                                                          const odb::Rect& bbox,
                                                          const DbuX left,
                                                          const DbuY bottom) const
{
  const double curr_center_x = ndi->getLeft().v + 0.5 * ndi->getWidth().v;
  const double curr_center_y = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
  const double next_center_x = left.v + 0.5 * ndi->getWidth().v;
  const double next_center_y = bottom.v + 0.5 * ndi->getHeight().v;
  const double curr_dist
      = pointToIntervalDistance(curr_center_x, bbox.xMin(), bbox.xMax())
        + pointToIntervalDistance(curr_center_y, bbox.yMin(), bbox.yMax());
  const double next_dist
      = pointToIntervalDistance(next_center_x, bbox.xMin(), bbox.xMax())
        + pointToIntervalDistance(next_center_y, bbox.yMin(), bbox.yMax());
  return curr_dist - next_dist;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::appendJournal(const Journal& src, Journal& dst) const
{
  for (const auto& action_ptr : src) {
    if (action_ptr == nullptr) {
      continue;
    }
    switch (action_ptr->typeId()) {
      case JournalActionTypeEnum::MOVE_CELL: {
        const auto* move_action
            = static_cast<const MoveCellAction*>(action_ptr.get());
        dst.addAction(MoveCellAction(move_action->getNode(),
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
        dst.addAction(
            UnplaceCellAction(unplace_action->getNode(), unplace_action->wasHold()));
        break;
      }
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::cloneJournal(const Journal& src, Journal& dst) const
{
  dst.clear();
  appendJournal(src, dst);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::tryOrderedPairTransaction(Node* anchor,
                                                   const DbuX source_left,
                                                   const DbuY source_bottom,
                                                   const int source_seg_id,
                                                   const Journal& stage1_journal,
                                                   const DetailedHPWL::DeltaCache& stage1_cache,
                                                   const double stage1_profit,
                                                   Journal& best_journal,
                                                   double& best_profit,
                                                   bool& used_escape)
{
  if (anchor == nullptr || stage1_journal.empty()) {
    return false;
  }

  used_escape = false;
  ordered_pair_stats_.stage1_exact++;
  const int source_row_id = mgr_->getSegment(source_seg_id)->getRowId();

  std::vector<std::pair<double, Node*>> ranked_partners;
  ranked_partners.reserve(anchor->getNumPins() * 2);
  std::unordered_set<int> seen_partners;
  seen_partners.reserve(anchor->getNumPins() * 2);
  for (Pin* pin : anchor->getPins()) {
    if (pin == nullptr || pin->getEdge() == nullptr) {
      continue;
    }
    Edge* edge = pin->getEdge();
    const int pin_count = edge->getNumPins();
    if (pin_count <= 1 || pin_count >= skipNetsLargerThanThis_) {
      continue;
    }
    for (Pin* edge_pin : edge->getPins()) {
      if (edge_pin == nullptr) {
        continue;
      }
      Node* partner = edge_pin->getNode();
      if (partner == nullptr || partner == anchor || partner->isFixed()
          || !partner->isStdCell() || !arch_->isSingleHeightCell(partner)
          || partner->getGroupId() != anchor->getGroupId()
          || mgr_->getNumReverseCellToSegs(partner->getId()) != 1) {
        continue;
      }
      if (!seen_partners.insert(partner->getId()).second) {
        continue;
      }
      const double partner_distance
          = std::abs(partner->getLeft().v - source_left.v)
            + std::abs(partner->getBottom().v - source_bottom.v);
      const double score
          = std::min(partner->getNumPins(), 8) * 16.0 - partner_distance;
      ranked_partners.emplace_back(score, partner);
    }
  }
  std::ranges::sort(ranked_partners,
                    [](const auto& lhs, const auto& rhs) {
                      if (lhs.first != rhs.first) {
                        return lhs.first > rhs.first;
                      }
                      return lhs.second->getId() < rhs.second->getId();
                    });
  if (ranked_partners.size() > kOrderedPairPartnerTopK) {
    ranked_partners.resize(kOrderedPairPartnerTopK);
  }

  bool improved = false;
  Journal stage2_journal(mgr_->getGrid(), mgr_);
  Journal combined_journal(mgr_->getGrid(), mgr_);
  const bool zero_congestion_weight = congestion_weight_ == 0.0;
  std::vector<MoveProbe> ordered_partner_probes;
  ordered_partner_probes.reserve(2);
  for (const auto& [score, partner] : ranked_partners) {
    static_cast<void>(score);
    const int partner_seg_id = mgr_->getReverseCellToSegs(partner->getId())[0]->getSegId();
    ordered_partner_probes.clear();
    const int partner_row_id = mgr_->getSegment(partner_seg_id)->getRowId();
    odb::Rect vacancy_box(source_left.v,
                          source_bottom.v,
                          source_left.v + partner->getWidth().v,
                          source_bottom.v + partner->getHeight().v);
    addProbe(
        partner, vacancy_box, partner_row_id, source_left, ordered_partner_probes);
    addProbe(partner,
             vacancy_box,
             source_row_id,
             source_left,
             ordered_partner_probes);
    if (ordered_partner_probes.empty()) {
      continue;
    }
    std::ranges::sort(ordered_partner_probes,
                      [](const MoveProbe& lhs, const MoveProbe& rhs) {
                        return lhs.heuristic > rhs.heuristic;
                      });
    if (ordered_partner_probes.size() > kOrderedPairProbeTopK) {
      ordered_partner_probes.resize(kOrderedPairProbeTopK);
    }

    ordered_pair_stats_.candidates += ordered_partner_probes.size();
    for (const auto& probe : ordered_partner_probes) {
      bool legal = mgr_->tryMove(partner,
                                 partner->getLeft(),
                                 partner->getBottom(),
                                 partner_seg_id,
                                 probe.left,
                                 probe.bottom,
                                 probe.seg_id);
      if (!legal) {
        legal = mgr_->trySwap(partner,
                              partner->getLeft(),
                              partner->getBottom(),
                              partner_seg_id,
                              probe.left,
                              probe.bottom,
                              probe.seg_id);
      }
      if (!legal) {
        continue;
      }

      ordered_pair_stats_.scored++;
      const Journal& active_stage2_journal = mgr_->getJournal();
      int overlap_edges = 0;
      const double hpwl_delta = active_hpwl_obj_->deltaFromCache(
          stage1_cache, active_stage2_journal, &overlap_edges);
      ordered_pair_stats_.cached_overlap_edges += overlap_edges;
      const bool run_audit
          = (ordered_pair_stats_.scored % kOrderedPairAuditEvery) == 0;
      const bool need_combined_for_profit
          = !is_profiling_pass_ && !zero_congestion_weight;
      bool stage2_cloned = false;
      auto ensure_stage2_journal = [&]() {
        if (!stage2_cloned) {
          cloneJournal(active_stage2_journal, stage2_journal);
          stage2_cloned = true;
        }
      };
      combined_journal.clear();
      if (need_combined_for_profit || run_audit) {
        ensure_stage2_journal();
        cloneJournal(stage1_journal, combined_journal);
        appendJournal(stage2_journal, combined_journal);
      }
      if (run_audit) {
        const double audit_delta = active_hpwl_obj_->delta(combined_journal);
        if (std::abs(audit_delta - hpwl_delta) > 1e-3) {
          ordered_pair_stats_.audit_gap++;
        }
      }
      double combined_profit = hpwl_delta;
      if (need_combined_for_profit) {
        const double congestion_improvement
            = calculateCongestionImprovement(combined_journal);
        combined_profit += congestion_weight_ * congestion_improvement;
      }
      if (combined_profit > best_profit && combined_profit > stage1_profit) {
        if (combined_journal.empty()) {
          ensure_stage2_journal();
          cloneJournal(stage1_journal, combined_journal);
          appendJournal(stage2_journal, combined_journal);
        }
        cloneJournal(combined_journal, best_journal);
        best_profit = combined_profit;
        improved = true;
        ordered_pair_stats_.promoted++;
      }
      mgr_->rejectMove();
    }
  }

  if (!anchor->isSourceEdgeHot()) {
    cloneJournal(stage1_journal, mgr_->getJournal());
    return improved;
  }

  struct EscapeEdge
  {
    double residual = 0.0;
    Edge* edge = nullptr;
    odb::Rect bbox;
  };
  std::vector<EscapeEdge> escape_edges;
  escape_edges.reserve(anchor->getNumPins());
  std::unordered_set<int> seen_escape_edges;
  seen_escape_edges.reserve(anchor->getNumPins());
  for (Pin* pin : anchor->getPins()) {
    if (pin == nullptr || pin->getEdge() == nullptr) {
      continue;
    }
    Edge* edge = pin->getEdge();
    const int pin_count = edge->getNumPins();
    if (pin_count <= 1 || pin_count >= skipNetsLargerThanThis_) {
      continue;
    }
    if (!seen_escape_edges.insert(edge->getId()).second) {
      continue;
    }
    odb::Rect edge_bbox;
    if (!calculateEdgeBB(edge, anchor, edge_bbox)) {
      continue;
    }
    const double residual = calculateSourceEdgeImprovement(
        anchor, edge_bbox, source_left, source_bottom);
    if (residual <= 0.0) {
      continue;
    }
    escape_edges.push_back({residual, edge, edge_bbox});
  }
  std::ranges::sort(escape_edges,
                    [](const auto& lhs, const auto& rhs) {
                      if (lhs.residual != rhs.residual) {
                        return lhs.residual > rhs.residual;
                      }
                      return lhs.edge->getId() < rhs.edge->getId();
                    });
  if (escape_edges.size() > kEndpointEscapeEdgeTopK) {
    escape_edges.resize(kEndpointEscapeEdgeTopK);
  }
  endpoint_escape_stats_.edges += static_cast<int>(escape_edges.size());

  std::vector<std::pair<double, Node*>> escape_partners;
  escape_partners.reserve(anchor->getNumPins());
  std::unordered_set<int> seen_escape_partners;
  seen_escape_partners.reserve(anchor->getNumPins());
  for (const auto& escape_edge : escape_edges) {
    escape_partners.clear();
    seen_escape_partners.clear();
    for (Pin* edge_pin : escape_edge.edge->getPins()) {
      if (edge_pin == nullptr) {
        continue;
      }
      Node* partner = edge_pin->getNode();
      if (partner == nullptr || partner == anchor || partner->isFixed()
          || !partner->isStdCell() || !arch_->isSingleHeightCell(partner)
          || partner->getGroupId() != anchor->getGroupId()
          || mgr_->getNumReverseCellToSegs(partner->getId()) != 1) {
        continue;
      }
      if (!seen_escape_partners.insert(partner->getId()).second) {
        continue;
      }
      const double residual = calculateSourceEdgeImprovement(
          partner, escape_edge.bbox, source_left, source_bottom);
      const double score = residual + std::min(partner->getNumPins(), 8) * 8.0;
      escape_partners.emplace_back(score, partner);
    }
    std::ranges::sort(escape_partners,
                      [](const auto& lhs, const auto& rhs) {
                        if (lhs.first != rhs.first) {
                          return lhs.first > rhs.first;
                        }
                        return lhs.second->getId() < rhs.second->getId();
                      });
    if (escape_partners.size() > kEndpointEscapePartnerTopK) {
      escape_partners.resize(kEndpointEscapePartnerTopK);
    }
    endpoint_escape_stats_.partners += static_cast<int>(escape_partners.size());

    std::vector<MoveProbe> escape_partner_probes;
    escape_partner_probes.reserve(3);
    for (const auto& [escape_score, partner] : escape_partners) {
      static_cast<void>(escape_score);
      const int partner_seg_id
          = mgr_->getReverseCellToSegs(partner->getId())[0]->getSegId();
      escape_partner_probes.clear();

      const DbuX left_target{
          static_cast<int>(std::floor(escape_edge.bbox.xMin()
                                      - 0.5 * partner->getWidth().v))};
      const DbuX center_target{
          static_cast<int>(std::floor(0.5
                                      * (escape_edge.bbox.xMin()
                                         + escape_edge.bbox.xMax())
                                      - 0.5 * partner->getWidth().v))};
      addProbe(partner,
               escape_edge.bbox,
               source_row_id,
               left_target,
               escape_partner_probes);
      addProbe(partner,
               escape_edge.bbox,
               source_row_id,
               center_target,
               escape_partner_probes);
      addProbe(partner,
               escape_edge.bbox,
               mgr_->getSegment(partner_seg_id)->getRowId(),
               left_target,
               escape_partner_probes);
      if (escape_partner_probes.empty()) {
        continue;
      }
      std::ranges::sort(escape_partner_probes,
                        [](const MoveProbe& lhs, const MoveProbe& rhs) {
                          return lhs.heuristic > rhs.heuristic;
                        });
      if (escape_partner_probes.size() > kEndpointEscapeProbeTopK) {
        escape_partner_probes.resize(kEndpointEscapeProbeTopK);
      }

      endpoint_escape_stats_.candidates
          += static_cast<int>(escape_partner_probes.size());
      for (const auto& probe : escape_partner_probes) {
        bool legal = mgr_->tryMove(partner,
                                   partner->getLeft(),
                                   partner->getBottom(),
                                   partner_seg_id,
                                   probe.left,
                                   probe.bottom,
                                   probe.seg_id);
        if (!legal) {
          legal = mgr_->trySwap(partner,
                                partner->getLeft(),
                                partner->getBottom(),
                                partner_seg_id,
                                probe.left,
                                probe.bottom,
                                probe.seg_id);
        }
        if (!legal) {
          continue;
        }

        endpoint_escape_stats_.scored++;
        const Journal& active_stage2_journal = mgr_->getJournal();
        int overlap_edges = 0;
        const double hpwl_delta = active_hpwl_obj_->deltaFromCache(
            stage1_cache, active_stage2_journal, &overlap_edges);
        endpoint_escape_stats_.cached_overlap_edges += overlap_edges;
        const bool run_audit
            = (endpoint_escape_stats_.scored % kOrderedPairAuditEvery) == 0;
        const bool need_combined_for_profit
            = !is_profiling_pass_ && !zero_congestion_weight;
        bool stage2_cloned = false;
        auto ensure_stage2_journal = [&]() {
          if (!stage2_cloned) {
            cloneJournal(active_stage2_journal, stage2_journal);
            stage2_cloned = true;
          }
        };
        combined_journal.clear();
        if (need_combined_for_profit || run_audit) {
          ensure_stage2_journal();
          cloneJournal(stage1_journal, combined_journal);
          appendJournal(stage2_journal, combined_journal);
        }
        if (run_audit) {
          const double audit_delta = active_hpwl_obj_->delta(combined_journal);
          if (std::abs(audit_delta - hpwl_delta) > 1e-3) {
            endpoint_escape_stats_.audit_gap++;
          }
        }
        double combined_profit = hpwl_delta;
        if (need_combined_for_profit) {
          const double congestion_improvement
              = calculateCongestionImprovement(combined_journal);
          combined_profit += congestion_weight_ * congestion_improvement;
        }
        if (combined_profit > best_profit && combined_profit > stage1_profit) {
          if (combined_journal.empty()) {
            ensure_stage2_journal();
            cloneJournal(stage1_journal, combined_journal);
            appendJournal(stage2_journal, combined_journal);
          }
          cloneJournal(combined_journal, best_journal);
          best_profit = combined_profit;
          improved = true;
          used_escape = true;
          endpoint_escape_stats_.promoted++;
        }
        mgr_->rejectMove();
      }
    }
  }

  cloneJournal(stage1_journal, mgr_->getJournal());
  return improved;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::findSegmentForProbe(Node* ndi,
                                             const int row_id,
                                             const DbuX target_left,
                                             int& seg_id,
                                             DbuX& clamped_left) const
{
  seg_id = -1;
  int best_distance = std::numeric_limits<int>::max();
  for (int s = 0; s < mgr_->getNumSegsInRow(row_id); s++) {
    DetailedSeg* seg_ptr = mgr_->getSegsInRow(row_id)[s];
    if (seg_ptr == nullptr || ndi->getGroupId() != seg_ptr->getRegId()) {
      continue;
    }
    const DbuX min_left = seg_ptr->getMinX();
    const DbuX max_left = seg_ptr->getMaxX() - ndi->getWidth();
    if (max_left < min_left) {
      continue;
    }
    DbuX probe_left = target_left;
    if (probe_left < min_left) {
      probe_left = min_left;
    } else if (probe_left > max_left) {
      probe_left = max_left;
    }
    const int distance = std::abs(probe_left.v - target_left.v);
    if (distance < best_distance) {
      best_distance = distance;
      seg_id = seg_ptr->getSegId();
      clamped_left = probe_left;
    }
  }
  return seg_id != -1;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::addProbe(Node* ndi,
                                  const odb::Rect& bbox,
                                  const int row_id,
                                  const DbuX target_left,
                                  std::vector<MoveProbe>& probes) const
{
  int seg_id = -1;
  DbuX clamped_left{0};
  if (!findSegmentForProbe(ndi, row_id, target_left, seg_id, clamped_left)) {
    return;
  }

  const DbuY bottom{arch_->getRow(row_id)->getBottom()};
  for (const auto& existing : probes) {
    if (existing.seg_id == seg_id && existing.left == clamped_left
        && existing.bottom == bottom) {
      return;
    }
  }

  MoveProbe probe;
  probe.left = clamped_left;
  probe.bottom = bottom;
  probe.seg_id = seg_id;
  probe.heuristic
      = calculateSourceEdgeImprovement(ndi, bbox, clamped_left, bottom);
  probes.push_back(probe);
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
  active_hpwl_obj_ = &hpwlObj;
  const bool saved_exact_probe_scoring = exact_probe_scoring_enabled_;
  exact_probe_scoring_enabled_ = false;

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
    const double cong_improvement
        = calculateCongestionImprovement(mgr_->getJournal());

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
    active_hpwl_obj_ = nullptr;
    exact_probe_scoring_enabled_ = saved_exact_probe_scoring;
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
  active_hpwl_obj_ = nullptr;
  exact_probe_scoring_enabled_ = saved_exact_probe_scoring;

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
