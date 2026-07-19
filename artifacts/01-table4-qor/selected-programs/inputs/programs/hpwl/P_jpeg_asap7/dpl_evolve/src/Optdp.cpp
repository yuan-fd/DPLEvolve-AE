// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <cstdint>
#include <utility>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "odb/util.h"
#include "utl/Logger.h"

// My stuff.
#include "graphics/DplObserver.h"
#include "legalize_shift.h"
#include "optimization/detailed.h"
#include "optimization/detailed_manager.h"

namespace dpl_evolve {
using utl::DPL;
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
  mgr.setExtraDplEnabled(extra_dpl_enabled_);
  struct ResidualFrontierEntry
  {
    Node* node = nullptr;
    int priority = 0;
  };
  std::vector<ResidualFrontierEntry> residual_frontier;
  residual_frontier.reserve(network_->getNumNodes() / 16);
  for (const auto& node_ptr : network_->getNodes()) {
    Node* node = node_ptr.get();
    if (node == nullptr || node->getType() != Node::CELL || node->isFixed()
        || !node->isStdCell()) {
      continue;
    }
    const int id = node->getId();
    if (id < 0
        || id >= static_cast<int>(source_edge_tension_target_valid_.size())
        || source_edge_tension_target_valid_[id] == 0) {
      continue;
    }
    const int current_row = grid_->gridSnapDownY(node).v;
    const int current_site = grid_->gridX(node).v;
    const int target_row = source_edge_tension_target_rows_[id];
    const int target_site = source_edge_tension_target_sites_[id];
    const int row_miss = std::abs(current_row - target_row);
    const int site_miss = std::abs(current_site - target_site);
    const int miss_priority = row_miss * 8 + site_miss;
    if (miss_priority <= 0) {
      continue;
    }
    residual_frontier.push_back({node, miss_priority});
  }
  std::sort(residual_frontier.begin(),
            residual_frontier.end(),
            [](const ResidualFrontierEntry& lhs, const ResidualFrontierEntry& rhs) {
              if (lhs.priority != rhs.priority) {
                return lhs.priority > rhs.priority;
              }
              return lhs.node->getId() < rhs.node->getId();
            });
  const size_t residual_frontier_cap = std::min(
      residual_frontier.size(),
      static_cast<size_t>(std::max(128, std::min(2048, network_->getNumNodes() / 32))));
  if (residual_frontier.size() > residual_frontier_cap) {
    residual_frontier.resize(residual_frontier_cap);
  }
  std::vector<std::pair<Node*, int>> remapped_frontier;
  remapped_frontier.reserve(residual_frontier.size());
  for (const ResidualFrontierEntry& entry : residual_frontier) {
    remapped_frontier.emplace_back(entry.node, entry.priority);
  }
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
  mgr.setSourceEdgeTensionFrontier(remapped_frontier);
  logger_->metric("dpl_evolve__source_edge_tension__producer_cells_raw",
                  static_cast<int>(residual_frontier.size()));
  logger_->metric("dpl_evolve__source_edge_tension__remapped_cells",
                  mgr.getSourceEdgeTensionFrontierCellCount());
  logger_->metric("dpl_evolve__source_edge_tension__remapped_segments",
                  mgr.getSourceEdgeTensionFrontierSegmentCount());
  logger_->info(DPL,
                308,
                "Source-edge tension frontier produced {} cells, remapped {} "
                "cells across {} segments.",
                static_cast<int>(residual_frontier.size()),
                mgr.getSourceEdgeTensionFrontierCellCount(),
                mgr.getSourceEdgeTensionFrontierSegmentCount());

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
  dt.improve(mgr);

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
