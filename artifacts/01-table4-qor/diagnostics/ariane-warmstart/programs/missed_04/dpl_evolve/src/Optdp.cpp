// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <cstdint>

#include "dpl_evolve/Opendp.h"
#include "odb/util.h"
#include "utl/Logger.h"

// My stuff.
#include "graphics/DplObserver.h"
#include "legalize_shift.h"
#include "optimization/detailed.h"
#include "optimization/detailed_manager.h"
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
  // The Diamond D+A route needs the exact source-topK global-swap lane to be
  // reachable from the normal detailed-improvement script.
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
  // Keep gs -> ro adjacent so accepted source-topK transactions feed the
  // critical frontier consumers before later passes dilute them.
  auto run_stage = [&](const std::string& script) {
    DetailedParams params;
    params.script = script;
    Detailed dt(params);
    dt.improve(mgr);
  };
  auto current_hpwl = [&]() {
    uint64_t hpwl_x = 0;
    uint64_t hpwl_y = 0;
    return Utility::hpwl(network_.get(), hpwl_x, hpwl_y);
  };
  const double dbu_micron = db_->getTech()->getDbUnitsPerMicron();

  if (debug_observer_) {
    logger_->report("Pause before improve placement.");
    debug_observer_->redrawAndPause();
  }

  run_stage("mis -p 10 -t 0.005;");

  int64_t stage_before = current_hpwl();
  run_stage("gs -p 10 -t 0.005;");
  int64_t stage_after = current_hpwl();
  logger_->report(
      "after_exact_gs HPWL checkpoint: {:10.1f} -> {:10.1f} u (delta {:10.1f} u)",
      stage_before / dbu_micron,
      stage_after / dbu_micron,
      (stage_after - stage_before) / dbu_micron);

  stage_before = stage_after;
  run_stage("ro -p 10 -t 0.005;");
  stage_after = current_hpwl();
  logger_->report(
      "after_frontier_ro HPWL checkpoint: {:10.1f} -> {:10.1f} u (delta {:10.1f} u)",
      stage_before / dbu_micron,
      stage_after / dbu_micron,
      (stage_after - stage_before) / dbu_micron);

  const auto& chain_stats = mgr.getDetailedChainStats();
  logger_->report(
      "source_topk_generated={} exact_scored={} replay_attempts={} "
      "replay_failures={} rollbacks={} accepts={} accepted_delta={:.1f} "
      "accepted_nodes={} hot_segments={}",
      chain_stats.source_topk_generated,
      chain_stats.source_topk_exact_scored,
      chain_stats.source_topk_replay_attempts,
      chain_stats.source_topk_replay_failures,
      chain_stats.source_topk_rollbacks,
      chain_stats.source_topk_accepts,
      chain_stats.source_topk_accepted_delta
          / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.accepted_nodes,
      chain_stats.hot_segments);
  logger_->report(
      "selected_reorder_frontier={} windows={} accepts={} gain={:.1f} "
      "critical_micro_start={}/{} gain={:.1f} "
      "critical_net_chain={}/{} gain={:.1f} "
      "exact_closure={}/{} gain={:.1f} "
      "multi_row_residual={}/{} gain={:.1f} "
      "segment_residual_swap={}/{} gain={:.1f}",
      chain_stats.selected_reorder_frontier,
      chain_stats.selected_reorder_windows,
      chain_stats.selected_reorder_accepts,
      chain_stats.selected_reorder_gain
          / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.critical_micro_start_probes,
      chain_stats.critical_micro_start_accepts,
      chain_stats.critical_micro_start_gain
          / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.critical_net_chain_probes,
      chain_stats.critical_net_chain_accepts,
      chain_stats.critical_net_chain_gain
          / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.exact_closure_windows,
      chain_stats.exact_closure_accepts,
      chain_stats.exact_closure_gain / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.multi_row_residual_probes,
      chain_stats.multi_row_residual_accepts,
      chain_stats.multi_row_residual_gain
          / db_->getTech()->getDbUnitsPerMicron(),
      chain_stats.segment_residual_swap_probes,
      chain_stats.segment_residual_swap_accepts,
      chain_stats.segment_residual_swap_gain
          / db_->getTech()->getDbUnitsPerMicron());

  stage_before = stage_after;
  run_stage("vs -p 10 -t 0.005;");
  run_stage("default -p 5 -f 20 -gen rng -obj hpwl -cost (hpwl);");
  if (disallow_one_site_gaps) {
    run_stage("disallow_one_site_gaps;");
  }
  stage_after = current_hpwl();
  logger_->report(
      "after_post_frontier HPWL checkpoint: {:10.1f} -> {:10.1f} u (delta {:10.1f} u)",
      stage_before / dbu_micron,
      stage_after / dbu_micron,
      (stage_after - stage_before) / dbu_micron);

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
