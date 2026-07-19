// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <algorithm>
#include <cmath>
#include <cstring>
#include <string>
#include <vector>

#include "optimization/detailed_manager.h"
#include "utl/Logger.h"
// Detailed placement algorithms.
#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "detailed.h"
#include "optimization/detailed_global.h"
#include "optimization/detailed_global_legacy.h"
#include "optimization/detailed_mis.h"
#include "optimization/detailed_orient.h"
#include "optimization/detailed_random.h"
#include "optimization/detailed_reorder.h"
#include "optimization/detailed_vertical.h"
using utl::DPL;

namespace dpl_evolve {

////////////////////////////////////////////////////////////////////////////////
// Defines.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
// Detailed::improve:
////////////////////////////////////////////////////////////////////////////////
bool Detailed::improve(DetailedMgr& mgr)
{
  mgr_ = &mgr;

  arch_ = mgr.getArchitecture();
  network_ = mgr.getNetwork();

  // Parse the script string and run each command.
  boost::char_separator<char> separators(" \r\t\n", ";");
  boost::tokenizer<boost::char_separator<char>> tokens(params_.script,
                                                       separators);
  std::vector<std::string> args;
  for (auto temp : tokens) {
    if (temp.back() == ';') {
      while (!temp.empty() && temp.back() == ';') {
        temp.resize(temp.size() - 1);
      }
      if (!temp.empty()) {
        args.push_back(temp);
      }
      // Command ended by a semi-colon.
      doDetailedCommand(args);
      args.clear();
    } else {
      args.push_back(temp);
    }
  }
  // Last command; possible if no ending semi-colon.
  doDetailedCommand(args);

  // Note: If cell orientation was not the last script
  // command run, then we should/need to perform
  // orientation to ensure the cells are properly
  // oriented for their respective row assignments.
  // We do not need to do flipping though.
  {
    DetailedOrient orienter(arch_, network_);
    orienter.run(mgr_, "orient -f");
  }

  // Different checks which are useful for debugging.
  mgr.checkRegionAssignment();
  mgr.checkRowAlignment();
  mgr.checkSiteAlignment();
  mgr.checkOverlapInSegments();
  mgr.checkEdgeSpacingInSegments();

  if (mgr.getDisallowOneSiteGaps()) {
    std::vector<std::vector<int>> oneSiteViolations;
    int temp_move_limit = mgr.getMoveLimit();
    mgr.setMoveLimit(10000);
    mgr.getOneSiteGapViolationsPerSegment(oneSiteViolations, true);
    for (int i = 0; i < oneSiteViolations.size(); i++) {
      if (!oneSiteViolations[i].empty()) {
        std::string violating_node_ids = "[";
        for (int nodeId : oneSiteViolations[i]) {
          violating_node_ids += std::to_string(nodeId)
                                + ",]"[nodeId == oneSiteViolations[i].back()];
        }
        mgr_->getLogger()->warn(
            DPL,
            323,
            "One site gap violation in segment {:d} nodes: {}",
            i,
            violating_node_ids);
      }
    }
    mgr.setMoveLimit(temp_move_limit);
  }

  return true;
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void Detailed::doDetailedCommand(std::vector<std::string>& args)
{
  if (args.empty()) {
    return;
  }

  // The first argument is always the command.

  auto logger = mgr_->getLogger();
  std::vector<std::string> run_args = args;
  bool late_broad_capture = false;
  if (strcmp(args[0].c_str(), "gs") == 0) {
    const auto capture_itr = std::find(
        run_args.begin(), run_args.end(), "-late_broad_capture");
    if (capture_itr != run_args.end()) {
      late_broad_capture = true;
      run_args.erase(capture_itr);
      mgr_->clearLateBroadTouchedCells();
      mgr_->setLateBroadCaptureEnabled(true);
    }
  }

  // Print something about what command will run.
  std::string command;
  if (strcmp(args[0].c_str(), "mis") == 0) {
    command = "independent set matching";
  } else if (strcmp(args[0].c_str(), "gs") == 0) {
    command = "global swaps";
  } else if (strcmp(args[0].c_str(), "vs") == 0) {
    command = "vertical swaps";
  } else if (strcmp(args[0].c_str(), "ro") == 0) {
    command = "reordering";
  } else if (strcmp(args[0].c_str(), "orient") == 0) {
    command = "orienting";
  } else if (strcmp(args[0].c_str(), "default") == 0) {
    command = "random improvement";
  } else if (strcmp(args[0].c_str(), "disallow_one_site_gaps") == 0) {
    command = "disallow_one_site_gaps";
  } else {
    logger->error(DPL, 341, "Unknown algorithm {:s}.", args[0]);
  }
  logger->info(DPL, 303, "Running algorithm for {:s}.", command);

  if (strcmp(args[0].c_str(), "mis") == 0) {
    DetailedMis mis(arch_, network_);
    mis.run(mgr_, run_args);
  } else if (strcmp(args[0].c_str(), "gs") == 0) {
    const bool force_exact = std::find(
                                 run_args.begin(), run_args.end(), "-elite_exact")
                             != run_args.end();
    if (mgr_->isExtraDplEnabled() || force_exact) {
      DetailedGlobalSwap gs(arch_, network_);
      gs.run(mgr_, run_args);
    } else {
      legacy::DetailedGlobalSwap gs(arch_, network_);
      gs.run(mgr_, run_args);
    }
  } else if (strcmp(args[0].c_str(), "vs") == 0) {
    DetailedVerticalSwap vs(arch_, network_);
    vs.run(mgr_, run_args);
  } else if (strcmp(args[0].c_str(), "ro") == 0) {
    DetailedReorderer ro(arch_, network_);
    ro.run(mgr_, run_args);
  } else if (strcmp(args[0].c_str(), "orient") == 0) {
    DetailedOrient orienter(arch_, network_);
    orienter.run(mgr_, run_args);
  } else if (strcmp(args[0].c_str(), "default") == 0) {
    DetailedRandom random(arch_, network_);
    random.run(mgr_, run_args);
  } else {
    if (late_broad_capture) {
      mgr_->setLateBroadCaptureEnabled(false);
    }
    return;
  }

  if (late_broad_capture) {
    mgr_->setLateBroadCaptureEnabled(false);
    logger->metric("dpl_evolve__late_broad_global__accepted_transactions",
                   mgr_->lateBroadAcceptedTransactions());
    logger->metric("dpl_evolve__late_broad_global__touched_cells",
                   mgr_->lateBroadTouchedCellCount());
    logger->metric("dpl_evolve__late_broad_global__touched_nets",
                   mgr_->lateBroadTouchedNetCount());
    logger->metric("dpl_evolve__late_broad_global__cell_actions",
                   mgr_->lateBroadCellActions());
    logger->metric("dpl_evolve__late_broad_global__shift_sites",
                   mgr_->lateBroadTotalShiftSites());
    logger->metric("dpl_evolve__late_broad_global__shift_rows",
                   mgr_->lateBroadTotalShiftRows());
    logger->info(DPL,
                 342,
                 "Late broad global capture complete; accepted transactions {}, "
                 "touched cells {}, touched nets {}, cell actions {}, shift {} "
                 "sites {} rows.",
                 mgr_->lateBroadAcceptedTransactions(),
                 mgr_->lateBroadTouchedCellCount(),
                 mgr_->lateBroadTouchedNetCount(),
                 mgr_->lateBroadCellActions(),
                 mgr_->lateBroadTotalShiftSites(),
                 mgr_->lateBroadTotalShiftRows());
  }

  // Different checks which are useful for debugging.
  // mgr_->checkRegionAssignment();
  // mgr_->checkRowAlignment();
  // mgr_->checkSiteAlignment();
  // mgr_->checkOverlapInSegments();
  // mgr_->checkEdgeSpacingInSegments();
}

}  // namespace dpl_evolve
