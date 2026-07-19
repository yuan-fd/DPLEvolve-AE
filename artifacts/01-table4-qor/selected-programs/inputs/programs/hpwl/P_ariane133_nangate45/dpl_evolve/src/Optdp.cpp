// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <chrono>
#include <cstdint>
#include <unordered_set>

#include "dpl_evolve/Opendp.h"
#include "odb/util.h"
#include "utl/Logger.h"

// My stuff.
#include "graphics/DplObserver.h"
#include "legalize_shift.h"
#include "optimization/detailed.h"
#include "optimization/detailed_manager.h"
#include "optimization/detailed_reorder.h"
#include "infrastructure/detailed_segment.h"
#include "util/utility.h"

namespace dpl_evolve {
namespace {
void reportHPWL(utl::Logger* logger,
                const double dbu_micron,
                const int64_t hpwlBefore,
                const int64_t hpwlAfter,
                const int64_t hpwlBefore_x,
                const int64_t hpwlAfter_x,
                const int64_t hpwlBefore_y,
                const int64_t hpwlAfter_y)
{
  logger->report("Detailed Improvement Results");
  logger->report("------------------------------------------");
  logger->report("Original HPWL         {:10.1f} u ({:10.1f}, {:10.1f})",
                 hpwlBefore / dbu_micron,
                 hpwlBefore_x / dbu_micron,
                 hpwlBefore_y / dbu_micron);
  logger->report("Final HPWL            {:10.1f} u ({:10.1f}, {:10.1f})",
                 hpwlAfter / dbu_micron,
                 hpwlAfter_x / dbu_micron,
                 hpwlAfter_y / dbu_micron);
  const double hpwl_delta = (hpwlAfter - hpwlBefore) / (double) hpwlBefore;
  const double hpwl_delta_x
      = (hpwlAfter_x - hpwlBefore_x) / (double) hpwlBefore_x;
  const double hpwl_delta_y
      = (hpwlAfter_y - hpwlBefore_y) / (double) hpwlBefore_y;
  logger->report("Delta HPWL            {:10.1f} % ({:10.1f}, {:10.1f})",
                 hpwl_delta * 100.0,
                 hpwl_delta_x * 100.0,
                 hpwl_delta_y * 100.0);
  logger->report("");
}
}  // namespace

////////////////////////////////////////////////////////////////
void Opendp::improvePlacement(const int seed,
                              const int max_displacement_x,
                              const int max_displacement_y)
{
  logger_->report("Detailed placement improvement.");

  odb::WireLengthEvaluator eval(db_->getChip()->getBlock());
  int64_t hpwlBefore_x = 0;
  int64_t hpwlBefore_y = 0;
  const int64_t hpwlBefore = eval.hpwl(hpwlBefore_x, hpwlBefore_y);

  if (hpwlBefore == 0) {
    logger_->report("Skipping detailed improvement since hpwl is zero.");
    return;
  }

  // Get needed information from DB.
  importDb();
  // TODO: adjustNodesOrient() but it's currently causing an unrelated CI
  // failure
  initGrid();

  const bool disallow_one_site_gaps = !odb::hasOneSiteMaster(db_);

  // A manager to track cells.
  DetailedMgr mgr(arch_.get(), network_.get(), grid_.get(), drc_engine_.get());
  mgr.setLogger(logger_);
  mgr.setGlobalSwapParams(global_swap_params_);
  mgr.setExtraDplEnabled(true);
  // Various settings.
  mgr.setSeed(seed);
  mgr.setMaxDisplacement(max_displacement_x, max_displacement_y);
  mgr.setDisallowOneSiteGaps(disallow_one_site_gaps);

  // Legalization.  Doesn't particularly do much.  It only
  // populates the data structures required for detailed
  // improvement.  If it errors or prints a warning when
  // given a legal placement, that likely means there is
  // a bug in my code somewhere.
  ShiftLegalizer lg;
  lg.legalize(mgr);
  setFixedGridCells();

  // Detailed improvement.  Runs through a number of different
  // optimizations aimed at wirelength improvement.  The last
  // call to the random improver can be set to consider things
  // like density, displacement, etc. in addition to wirelength.
  // Everything done through a script string.

  DetailedParams dtParams;
  dtParams.script = "";
  // Maximum independent set matching.
  dtParams.script += "mis -p 10 -t 0.005;";
  // Global swaps.
  dtParams.script += "gs -p 10 -t 0.005;";
  // Vertical swaps.
  dtParams.script += "vs -p 10 -t 0.005;";
  // Small reordering.
  dtParams.script += "ro -p 10 -t 0.005;";
  // Random moves and swaps with hpwl as a cost function.  Use
  // random moves and hpwl objective right now.
  dtParams.script += "default -p 5 -f 20 -gen rng -obj hpwl -cost (hpwl);";

  if (disallow_one_site_gaps) {
    dtParams.script += "disallow_one_site_gaps;";
  }

  if (debug_observer_) {
    logger_->report("Pause before improve placement.");
    debug_observer_->redrawAndPause();
  }

  // Run the script.
  Detailed dt(dtParams);
  mgr.clearAcceptedMoveNodes();
  dt.improve(mgr);

  uint64_t hpwlAfterDpoX = 0;
  uint64_t hpwlAfterDpoY = 0;
  const uint64_t hpwlAfterDpo
      = Utility::hpwl(network_.get(), hpwlAfterDpoX, hpwlAfterDpoY);
  CriticalRowMicroStartStats micro_start_stats;
  runCriticalRowMicroStart(mgr, micro_start_stats);
  uint64_t hpwlAfterMicroStartX = 0;
  uint64_t hpwlAfterMicroStartY = 0;
  const uint64_t hpwlAfterMicroStart
      = Utility::hpwl(network_.get(), hpwlAfterMicroStartX, hpwlAfterMicroStartY);
  FrontierAttributionStats frontier_attribution;
  {
    std::unordered_set<int> base_node_ids;
    std::unordered_set<int> critical_node_ids;
    std::unordered_set<int> merged_node_ids;
    std::unordered_set<int> base_segment_ids;
    std::unordered_set<int> critical_segment_ids;
    std::unordered_set<int> merged_segment_ids;
    std::unordered_set<int> micro_added_segment_ids;
    for (Node* node : mgr.getAcceptedMoveNodes()) {
      if (node == nullptr || mgr.getNumReverseCellToSegs(node->getId()) != 1) {
        continue;
      }
      base_node_ids.insert(node->getId());
      base_segment_ids.insert(
          mgr.getReverseCellToSegs(node->getId())[0]->getSegId());
    }
    for (Node* node : mgr.getCriticalNetFrontierNodes()) {
      if (node == nullptr || mgr.getNumReverseCellToSegs(node->getId()) != 1) {
        continue;
      }
      critical_node_ids.insert(node->getId());
      critical_segment_ids.insert(
          mgr.getReverseCellToSegs(node->getId())[0]->getSegId());
    }
    merged_node_ids = critical_node_ids;
    merged_segment_ids = critical_segment_ids;
    for (Node* node : mgr.getAcceptedMoveNodes()) {
      if (node == nullptr || mgr.getNumReverseCellToSegs(node->getId()) != 1) {
        continue;
      }
      const int node_id = node->getId();
      const int seg_id = mgr.getReverseCellToSegs(node_id)[0]->getSegId();
      merged_node_ids.insert(node_id);
      merged_segment_ids.insert(seg_id);
      if (critical_node_ids.find(node_id) == critical_node_ids.end()) {
        ++frontier_attribution.micro_start_added_nodes;
        micro_added_segment_ids.insert(seg_id);
      }
    }
    frontier_attribution.base_frontier_nodes = base_node_ids.size();
    frontier_attribution.critical_frontier_nodes = critical_node_ids.size();
    frontier_attribution.base_frontier_segments = base_segment_ids.size();
    frontier_attribution.critical_frontier_segments
        = critical_segment_ids.size();
    frontier_attribution.merged_frontier_nodes = merged_node_ids.size();
    frontier_attribution.merged_frontier_segments = merged_segment_ids.size();
    frontier_attribution.micro_start_added_segments
        = micro_added_segment_ids.size();
  }
  DetailedReorderer::ExactClosureStats closure_stats;
  DetailedReorderer::ChainAssignmentStats chain_stats;
  DetailedReorderer::MultiRowTransactionStats multirow_stats;
  DetailedReorderer::ResidualSwapStats residual_swap_stats;
  const auto closure_begin = std::chrono::steady_clock::now();
  DetailedReorderer exact_closure(arch_.get(), network_.get());
  exact_closure.criticalNetChainAssignment(
      &mgr, mgr.getAcceptedMoveNodes(), chain_stats);
  uint64_t hpwlAfterChainX = 0;
  uint64_t hpwlAfterChainY = 0;
  const uint64_t hpwlAfterChain
      = Utility::hpwl(network_.get(), hpwlAfterChainX, hpwlAfterChainY);
  exact_closure.exactLocalClosure(
      &mgr,
      mgr.getAcceptedMoveNodes(),
      closure_stats,
      &frontier_attribution);
  uint64_t hpwlAfterClosureX = 0;
  uint64_t hpwlAfterClosureY = 0;
  const uint64_t hpwlAfterClosure
      = Utility::hpwl(network_.get(), hpwlAfterClosureX, hpwlAfterClosureY);
  exact_closure.multiRowResidualTransactions(
      &mgr, mgr.getAcceptedMoveNodes(), multirow_stats);
  uint64_t hpwlAfterMultirowX = 0;
  uint64_t hpwlAfterMultirowY = 0;
  const uint64_t hpwlAfterMultirow
      = Utility::hpwl(network_.get(), hpwlAfterMultirowX, hpwlAfterMultirowY);
  exact_closure.segmentLocalResidualSwaps(
      &mgr, mgr.getAcceptedMoveNodes(), residual_swap_stats);
  mgr.clearHotSegments();
  mgr.clearCriticalNetFrontier();
  const auto closure_end = std::chrono::steady_clock::now();
  const double closure_ms
      = std::chrono::duration<double, std::milli>(closure_end - closure_begin)
            .count();
  uint64_t hpwlAfterResidualSwapX = 0;
  uint64_t hpwlAfterResidualSwapY = 0;
  const uint64_t hpwlAfterResidualSwap = Utility::hpwl(
      network_.get(), hpwlAfterResidualSwapX, hpwlAfterResidualSwapY);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_frontier_cells",
                  closure_stats.frontier_cells);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_frontier_segments",
                  closure_stats.frontier_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_base_frontier_nodes",
                  closure_stats.base_frontier_nodes);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_critical_frontier_nodes",
      closure_stats.critical_frontier_nodes);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_merged_frontier_nodes",
                  closure_stats.merged_frontier_nodes);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_micro_start_added_nodes",
      closure_stats.micro_start_added_nodes);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_base_frontier_segments",
      closure_stats.base_frontier_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_critical_frontier_segments",
      closure_stats.critical_frontier_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_merged_frontier_segments",
      closure_stats.merged_frontier_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_micro_start_added_segments",
      closure_stats.micro_start_added_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_windows_generated",
                  closure_stats.windows_generated);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_windows_selected",
                  closure_stats.windows_selected);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_windows_capped",
                  closure_stats.windows_capped);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_windows_evaluated",
                  closure_stats.windows_evaluated);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_windows_on_micro_start_segments",
      closure_stats.windows_on_micro_start_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_windows_on_base_segments",
      closure_stats.windows_on_base_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_scored",
                  closure_stats.exact_scored);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_accepts",
                  closure_stats.accepts);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_rollbacks",
                  closure_stats.rollbacks);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_accepts_on_micro_start_segments",
      closure_stats.accepts_on_micro_start_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_accepts_on_base_segments",
      closure_stats.accepts_on_base_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_accepted_gain",
                  closure_stats.accepted_gain);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_gain_on_micro_start_segments",
      closure_stats.gain_on_micro_start_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_exact_gain_on_base_segments",
      closure_stats.gain_on_base_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_early_stopped",
                  closure_stats.early_stopped ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__post_dpo_exact_ms", closure_ms);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_before_exact",
                  hpwlAfterChain);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_after_exact",
                  hpwlAfterClosure);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_frontier_cells",
                  multirow_stats.frontier_cells);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_frontier_segments",
                  multirow_stats.frontier_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_seeds_selected",
                  multirow_stats.seeds_selected);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_windows",
                  multirow_stats.transaction_windows);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_probes",
                  multirow_stats.probes);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_exact_scored",
                  multirow_stats.exact_scored);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_accepts",
                  multirow_stats.accepts);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_rollbacks",
                  multirow_stats.rollbacks);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_first_step_rejects",
                  multirow_stats.first_step_rejects);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_second_step_rejects",
                  multirow_stats.second_step_rejects);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_accepted_gain",
                  multirow_stats.accepted_gain);
  logger_->metric("dpl_evolve__pipeline__post_dpo_multirow_early_stopped",
                  multirow_stats.early_stopped ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_before_multirow",
                  hpwlAfterClosure);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_after_multirow",
                  hpwlAfterMultirow);
  logger_->metric("dpl_evolve__pipeline__post_dpo_residual_swap_frontier_cells",
                  residual_swap_stats.frontier_cells);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_frontier_segments",
      residual_swap_stats.frontier_segments);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_seeds_selected",
      residual_swap_stats.seeds_selected);
  logger_->metric("dpl_evolve__pipeline__post_dpo_residual_swap_windows",
                  residual_swap_stats.swap_windows);
  logger_->metric("dpl_evolve__pipeline__post_dpo_residual_swap_probes",
                  residual_swap_stats.probes);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_exact_scored",
      residual_swap_stats.exact_scored);
  logger_->metric("dpl_evolve__pipeline__post_dpo_residual_swap_accepts",
                  residual_swap_stats.accepts);
  logger_->metric("dpl_evolve__pipeline__post_dpo_residual_swap_rollbacks",
                  residual_swap_stats.rollbacks);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_failed_swaps",
      residual_swap_stats.failed_swaps);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_accepted_gain",
      residual_swap_stats.accepted_gain);
  logger_->metric(
      "dpl_evolve__pipeline__post_dpo_residual_swap_early_stopped",
      residual_swap_stats.early_stopped ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_before_residual_swap",
                  hpwlAfterMultirow);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_after_residual_swap",
                  hpwlAfterResidualSwap);
  logger_->metric("dpl_evolve__pipeline__micro_start_frontier_cells",
                  micro_start_stats.frontier_cells);
  logger_->metric("dpl_evolve__pipeline__micro_start_frontier_segments",
                  micro_start_stats.frontier_segments);
  logger_->metric("dpl_evolve__pipeline__micro_start_seeds_selected",
                  micro_start_stats.seeds_selected);
  logger_->metric("dpl_evolve__pipeline__micro_start_candidate_moves",
                  micro_start_stats.candidate_moves);
  logger_->metric("dpl_evolve__pipeline__micro_start_probes",
                  micro_start_stats.probes);
  logger_->metric("dpl_evolve__pipeline__micro_start_exact_scored",
                  micro_start_stats.exact_scored);
  logger_->metric("dpl_evolve__pipeline__micro_start_accepts",
                  micro_start_stats.accepts);
  logger_->metric("dpl_evolve__pipeline__micro_start_failed_moves",
                  micro_start_stats.failed_moves);
  logger_->metric("dpl_evolve__pipeline__micro_start_rollbacks",
                  micro_start_stats.rollbacks);
  logger_->metric("dpl_evolve__pipeline__micro_start_risk_filtered",
                  micro_start_stats.risk_filtered);
  logger_->metric("dpl_evolve__pipeline__micro_start_critical_target_accepts",
                  micro_start_stats.critical_target_accepts);
  logger_->metric("dpl_evolve__pipeline__micro_start_base_target_accepts",
                  micro_start_stats.base_target_accepts);
  logger_->metric("dpl_evolve__pipeline__micro_start_hot_only_target_accepts",
                  micro_start_stats.hot_only_target_accepts);
  logger_->metric("dpl_evolve__pipeline__micro_start_appended_nodes",
                  micro_start_stats.appended_nodes);
  logger_->metric("dpl_evolve__pipeline__micro_start_appended_segments",
                  micro_start_stats.appended_segments);
  logger_->metric("dpl_evolve__pipeline__micro_start_accepted_gain",
                  micro_start_stats.accepted_gain);
  logger_->metric("dpl_evolve__pipeline__micro_start_adjusted_accepted_gain",
                  micro_start_stats.adjusted_accepted_gain);
  logger_->metric("dpl_evolve__pipeline__micro_start_accepted_risk_penalty",
                  micro_start_stats.accepted_risk_penalty);
  logger_->metric("dpl_evolve__pipeline__micro_start_early_stopped",
                  micro_start_stats.early_stopped ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_frontier_cells",
                  chain_stats.frontier_cells);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_frontier_segments",
                  chain_stats.frontier_segments);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_seeds_selected",
                  chain_stats.seeds_selected);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_windows",
                  chain_stats.chain_windows);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_probes",
                  chain_stats.probes);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_exact_scored",
                  chain_stats.exact_scored);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_accepts",
                  chain_stats.accepts);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_rollbacks",
                  chain_stats.rollbacks);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_depth2_rejects",
                  chain_stats.depth2_rejects);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_depth3_probes",
                  chain_stats.depth3_probes);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_depth3_rejects",
                  chain_stats.depth3_rejects);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_accepted_gain",
                  chain_stats.accepted_gain);
  logger_->metric("dpl_evolve__pipeline__post_dpo_chain_early_stopped",
                  chain_stats.early_stopped ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_before_chain",
                  hpwlAfterMicroStart);
  logger_->metric("dpl_evolve__pipeline__post_dpo_hpwl_after_chain",
                  hpwlAfterChain);
  logger_->report(
      "DPL-Evolve critical-row micro-start: frontier cells {}, segments {}, "
      "seeds {}, candidate moves {}, probes {}, accepts {}, rollbacks {}, "
      "risk filtered {}, target accepts critical/base/hot-only {}/{}/{}, "
      "gain raw/adjusted {:.1f}/{:.1f}, accepted risk {:.1f}, appended nodes "
      "{}, segments {}, HPWL {:.1f} -> {:.1f}.",
      micro_start_stats.frontier_cells,
      micro_start_stats.frontier_segments,
      micro_start_stats.seeds_selected,
      micro_start_stats.candidate_moves,
      micro_start_stats.probes,
      micro_start_stats.accepts,
      micro_start_stats.rollbacks,
      micro_start_stats.risk_filtered,
      micro_start_stats.critical_target_accepts,
      micro_start_stats.base_target_accepts,
      micro_start_stats.hot_only_target_accepts,
      micro_start_stats.accepted_gain,
      micro_start_stats.adjusted_accepted_gain,
      micro_start_stats.accepted_risk_penalty,
      micro_start_stats.appended_nodes,
      micro_start_stats.appended_segments,
      static_cast<double>(hpwlAfterDpo),
      static_cast<double>(hpwlAfterMicroStart));
  logger_->report(
      "DPL-Evolve post-DPO exact closure: frontier cells {}, segments {}, "
      "windows {}/{}/{} (generated/selected/evaluated), accepts {}, rollbacks "
      "{}, gain {:.1f}, micro-start added nodes/segments {}/{}, "
      "selected windows micro/base {}/{}, accepts micro/base {}/{}, "
      "quality accepts micro/base {}/{}, gain micro/base {:.1f}/{:.1f}, "
      "HPWL {:.1f} -> {:.1f}, {:.2f} ms.",
      closure_stats.frontier_cells,
      closure_stats.frontier_segments,
      closure_stats.windows_generated,
      closure_stats.windows_selected,
      closure_stats.windows_evaluated,
      closure_stats.accepts,
      closure_stats.rollbacks,
      closure_stats.accepted_gain,
      closure_stats.micro_start_added_nodes,
      closure_stats.micro_start_added_segments,
      closure_stats.windows_on_micro_start_segments,
      closure_stats.windows_on_base_segments,
      closure_stats.accepts_on_micro_start_segments,
      closure_stats.accepts_on_base_segments,
      closure_stats.accepts_on_micro_start_segments,
      closure_stats.accepts_on_base_segments,
      closure_stats.gain_on_micro_start_segments,
      closure_stats.gain_on_base_segments,
      static_cast<double>(hpwlAfterChain),
      static_cast<double>(hpwlAfterClosure),
      closure_ms);
  logger_->report(
      "DPL-Evolve critical-net chain assignment: frontier cells {}, segments "
      "{}, seeds {}, chain windows {}, probes {}, accepts {}, rollbacks {}, "
      "depth3 probes {}, gain {:.1f}, HPWL {:.1f} -> {:.1f}.",
      chain_stats.frontier_cells,
      chain_stats.frontier_segments,
      chain_stats.seeds_selected,
      chain_stats.chain_windows,
      chain_stats.probes,
      chain_stats.accepts,
      chain_stats.rollbacks,
      chain_stats.depth3_probes,
      chain_stats.accepted_gain,
      static_cast<double>(hpwlAfterMicroStart),
      static_cast<double>(hpwlAfterChain));
  logger_->report(
      "DPL-Evolve post-DPO multi-row transactions: frontier cells {}, "
      "segments {}, seeds {}, windows {}, probes {}, accepts {}, rollbacks "
      "{}, gain {:.1f}, HPWL {:.1f} -> {:.1f}.",
      multirow_stats.frontier_cells,
      multirow_stats.frontier_segments,
      multirow_stats.seeds_selected,
      multirow_stats.transaction_windows,
      multirow_stats.probes,
      multirow_stats.accepts,
      multirow_stats.rollbacks,
      multirow_stats.accepted_gain,
      static_cast<double>(hpwlAfterClosure),
      static_cast<double>(hpwlAfterMultirow));
  logger_->report(
      "DPL-Evolve post-DPO residual swaps: frontier cells {}, segments {}, "
      "seeds {}, windows {}, probes {}, accepts {}, rollbacks {}, failed "
      "swaps {}, gain {:.1f}, HPWL {:.1f} -> {:.1f}.",
      residual_swap_stats.frontier_cells,
      residual_swap_stats.frontier_segments,
      residual_swap_stats.seeds_selected,
      residual_swap_stats.swap_windows,
      residual_swap_stats.probes,
      residual_swap_stats.accepts,
      residual_swap_stats.rollbacks,
      residual_swap_stats.failed_swaps,
      residual_swap_stats.accepted_gain,
      static_cast<double>(hpwlAfterMultirow),
      static_cast<double>(hpwlAfterResidualSwap));

  if (debug_observer_) {
    logger_->report("Pause after improve placement.");
    debug_observer_->redrawAndPause();
  }

  // Write solution back.
  updateDbInstLocations();

  // Get final hpwl.
  int64_t hpwlAfter_x = 0;
  int64_t hpwlAfter_y = 0;
  const int64_t hpwlAfter = eval.hpwl(hpwlAfter_x, hpwlAfter_y);
  const double dbu_micron = db_->getTech()->getDbUnitsPerMicron();
  reportHPWL(logger_,
             dbu_micron,
             hpwlBefore,
             hpwlAfter,
             hpwlBefore_x,
             hpwlAfter_x,
             hpwlBefore_y,
             hpwlAfter_y);
}

////////////////////////////////////////////////////////////////
}  // namespace dpl_evolve
