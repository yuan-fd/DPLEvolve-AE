// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <cstdint>
#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/detailed_segment.h"
#include "odb/util.h"
#include "utl/Logger.h"

// My stuff.
#include "graphics/DplObserver.h"
#include "legalize_shift.h"
#include "objective/detailed_hpwl.h"
#include "optimization/detailed.h"
#include "optimization/detailed_global.h"
#include "optimization/detailed_manager.h"
#include "optimization/detailed_reorder.h"
#include "optimization/detailed_vertical.h"

namespace dpl_evolve {
namespace {
struct ResidualSegmentCandidate
{
  int seg_id = -1;
  int anchor_count = 0;
  int exact_anchor_count = 0;
  int weighted_residual = 0;
  int max_residual = 0;
};

struct FrontierCellCandidate
{
  int node_id = -1;
  int row_miss = 0;
  int site_residual = 0;
  unsigned char target_valid = 0;
  int score = 0;
};

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
  logger_->metric(
      "dpl_evolve__improve__inherited_legalm_stage3_exact_handoff_cells",
      legalm_stage3_exact_handoff_cells_);
  logger_->metric(
      "dpl_evolve__improve__inherited_legalm_stage3_exact_handoff_target_misses",
      legalm_stage3_exact_handoff_target_misses_);

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
  dt.improve(mgr);

  std::vector<FrontierCellCandidate> frontier_row_miss_cells;
  std::vector<FrontierCellCandidate> frontier_same_row_cells;
  frontier_row_miss_cells.reserve(96);
  frontier_same_row_cells.reserve(96);
  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      continue;
    }
    if (mgr.getNumReverseCellToSegs(cell->getId()) != 1) {
      continue;
    }

    const int id = cell->getId();
    if (id < 0 || id >= static_cast<int>(legalm_target_valid_.size())
        || legalm_target_valid_[id] == 0) {
      continue;
    }

    const GridPt current = legalGridPt(cell, false);
    const int row_miss = std::abs(current.y.v - legalm_target_rows_[id]);
    const int site_residual = std::abs(current.x.v - legalm_target_sites_[id]);
    const bool exact_anchor = legalm_target_valid_[id] == 2;
    if (row_miss == 0 && site_residual < 2) {
      continue;
    }
    if (!exact_anchor && row_miss == 0 && site_residual < 4) {
      continue;
    }

    FrontierCellCandidate candidate;
    candidate.node_id = id;
    candidate.row_miss = row_miss;
    candidate.site_residual = site_residual;
    candidate.target_valid = legalm_target_valid_[id];
    candidate.score = (exact_anchor ? 1 << 20 : 0)
                      + (std::min(row_miss, 8) << 12)
                      + (std::min(site_residual, 64) << 4);
    if (row_miss > 0) {
      frontier_row_miss_cells.push_back(candidate);
    } else {
      frontier_same_row_cells.push_back(candidate);
    }
  }

  auto frontier_order = [](const FrontierCellCandidate& lhs,
                           const FrontierCellCandidate& rhs) {
    if (lhs.score != rhs.score) {
      return lhs.score > rhs.score;
    }
    if (lhs.row_miss != rhs.row_miss) {
      return lhs.row_miss > rhs.row_miss;
    }
    if (lhs.site_residual != rhs.site_residual) {
      return lhs.site_residual > rhs.site_residual;
    }
    return lhs.node_id < rhs.node_id;
  };
  std::stable_sort(frontier_row_miss_cells.begin(),
                   frontier_row_miss_cells.end(),
                   frontier_order);
  std::stable_sort(frontier_same_row_cells.begin(),
                   frontier_same_row_cells.end(),
                   frontier_order);

  std::vector<FrontierCellCandidate> frontier_cells;
  frontier_cells.reserve(48);
  constexpr int kFrontierCap = 48;
  const int row_miss_quota = std::min<int>(24, frontier_row_miss_cells.size());
  const int same_row_quota = std::min<int>(24, frontier_same_row_cells.size());
  frontier_cells.insert(frontier_cells.end(),
                        frontier_row_miss_cells.begin(),
                        frontier_row_miss_cells.begin() + row_miss_quota);
  frontier_cells.insert(frontier_cells.end(),
                        frontier_same_row_cells.begin(),
                        frontier_same_row_cells.begin() + same_row_quota);
  for (int i = row_miss_quota;
       i < static_cast<int>(frontier_row_miss_cells.size())
       && static_cast<int>(frontier_cells.size()) < kFrontierCap;
       ++i) {
    frontier_cells.push_back(frontier_row_miss_cells[i]);
  }
  for (int i = same_row_quota;
       i < static_cast<int>(frontier_same_row_cells.size())
       && static_cast<int>(frontier_cells.size()) < kFrontierCap;
       ++i) {
    frontier_cells.push_back(frontier_same_row_cells[i]);
  }

  int frontier_exact_anchor_cells = 0;
  int frontier_target_miss_cells = 0;
  for (const FrontierCellCandidate& candidate : frontier_cells) {
    frontier_exact_anchor_cells += candidate.target_valid == 2 ? 1 : 0;
    frontier_target_miss_cells += candidate.row_miss > 0 ? 1 : 0;
  }

  int frontier_vertical_candidates = 0;
  int frontier_global_candidates = 0;
  int frontier_scored_moves = 0;
  int frontier_accepts = 0;
  int frontier_vertical_accepts = 0;
  int frontier_global_accepts = 0;
  double frontier_hpwl_gain = 0.0;
  if (!frontier_cells.empty()) {
    constexpr int kFrontierScoreCap = 96;
    constexpr int kFrontierAcceptCap = 12;

    mgr.resortSegments();
    DetailedGlobalSwap global_consumer(arch_.get(), network_.get());
    DetailedVerticalSwap vertical_consumer(arch_.get(), network_.get());
    global_consumer.init(&mgr);
    vertical_consumer.init(&mgr);

    DetailedHPWL frontier_hpwl(network_.get());
    frontier_hpwl.init(&mgr, nullptr);
    frontier_hpwl.curr();

    auto scoreFrontierMove = [&](const bool generated,
                                 const bool vertical_move) -> bool {
      if (!generated) {
        return false;
      }

      ++frontier_scored_moves;
      const double delta = frontier_hpwl.delta(mgr.getJournal());
      if (delta > 0.0) {
        frontier_hpwl.accept();
        mgr.acceptMove();
        ++frontier_accepts;
        frontier_hpwl_gain += delta;
        if (vertical_move) {
          ++frontier_vertical_accepts;
        } else {
          ++frontier_global_accepts;
        }
        return true;
      }

      mgr.rejectMove();
      return false;
    };

    for (const FrontierCellCandidate& candidate : frontier_cells) {
      if (frontier_scored_moves >= kFrontierScoreCap
          || frontier_accepts >= kFrontierAcceptCap) {
        break;
      }

      Node* cell = network_->getNode(candidate.node_id);
      if (cell == nullptr) {
        continue;
      }

      bool accepted = false;
      if (candidate.row_miss > 0) {
        ++frontier_vertical_candidates;
        accepted = scoreFrontierMove(
            vertical_consumer.generateTargetedMove(&mgr, cell), true);
      }
      if (accepted || frontier_scored_moves >= kFrontierScoreCap
          || frontier_accepts >= kFrontierAcceptCap) {
        continue;
      }

      ++frontier_global_candidates;
      scoreFrontierMove(
          global_consumer.generateTargetedMove(&mgr, cell, false), false);
    }
  }
  logger_->metric("dpl_evolve__improve__frontier_global_vertical_frontier",
                  static_cast<int>(frontier_cells.size()));
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_exact_anchor_cells",
      frontier_exact_anchor_cells);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_target_miss_cells",
      frontier_target_miss_cells);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_vertical_candidates",
      frontier_vertical_candidates);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_global_candidates",
      frontier_global_candidates);
  logger_->metric("dpl_evolve__improve__frontier_global_vertical_scored",
                  frontier_scored_moves);
  logger_->metric("dpl_evolve__improve__frontier_global_vertical_accepts",
                  frontier_accepts);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_vertical_accepts",
      frontier_vertical_accepts);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_global_accepts",
      frontier_global_accepts);
  logger_->metric("dpl_evolve__improve__frontier_global_vertical_hpwl_gain",
                  frontier_hpwl_gain);
  logger_->metric(
      "dpl_evolve__improve__frontier_global_vertical_control_guard",
      1);
  logger_->info(
      utl::DPL,
      1235,
      "Low-residual frontier global/vertical frontier {}, exact anchors {}, "
      "target misses {}, vertical/global candidates {} / {}, scored {}, "
      "accepts {} (vertical/global {} / {}), exact HPWL gain {:.2f}.",
      frontier_cells.size(),
      frontier_exact_anchor_cells,
      frontier_target_miss_cells,
      frontier_vertical_candidates,
      frontier_global_candidates,
      frontier_scored_moves,
      frontier_accepts,
      frontier_vertical_accepts,
      frontier_global_accepts,
      frontier_hpwl_gain);
  logger_->info(utl::DPL,
                1236,
                "Vertical-frontier elite control donor guard enabled {}, "
                "frontier {}, vertical accepts {}, global accepts {}.",
                1,
                frontier_cells.size(),
                frontier_vertical_accepts,
                frontier_global_accepts);

  std::unordered_map<int, ResidualSegmentCandidate> residual_segments;
  int exact_residual_cells = 0;
  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      continue;
    }
    const int id = cell->getId();
    if (id < 0 || id >= static_cast<int>(legalm_target_valid_.size())
        || legalm_target_valid_[id] == 0) {
      continue;
    }
    const bool exact_anchor = legalm_target_valid_[id] == 2;

    const GridPt current = legalGridPt(cell, false);
    const int target_row = legalm_target_rows_[id];
    const int target_site = legalm_target_sites_[id];
    if (current.y.v != target_row) {
      continue;
    }
    const int residual = std::abs(current.x.v - target_site);
    if (residual < 2) {
      continue;
    }
    if (exact_anchor) {
      ++exact_residual_cells;
    }

    for (DetailedSeg* seg : mgr.getReverseCellToSegs(id)) {
      if (seg == nullptr || seg->getRowId() != current.y.v) {
        continue;
      }
      auto& score = residual_segments[seg->getSegId()];
      score.seg_id = seg->getSegId();
      ++score.anchor_count;
      score.exact_anchor_count += exact_anchor ? 1 : 0;
      score.weighted_residual += residual;
      score.max_residual = std::max(score.max_residual, residual);
    }
  }

  std::vector<ResidualSegmentCandidate> ranked_segments;
  ranked_segments.reserve(residual_segments.size());
  for (const auto& [_, score] : residual_segments) {
    if (score.anchor_count > 0) {
      ranked_segments.push_back(score);
    }
  }
  std::stable_sort(ranked_segments.begin(),
                   ranked_segments.end(),
                   [](const ResidualSegmentCandidate& lhs,
                      const ResidualSegmentCandidate& rhs) {
                     if (lhs.exact_anchor_count != rhs.exact_anchor_count) {
                       return lhs.exact_anchor_count > rhs.exact_anchor_count;
                     }
                     if (lhs.weighted_residual != rhs.weighted_residual) {
                       return lhs.weighted_residual > rhs.weighted_residual;
                     }
                     if (lhs.anchor_count != rhs.anchor_count) {
                       return lhs.anchor_count > rhs.anchor_count;
                     }
                     return lhs.seg_id < rhs.seg_id;
                   });
  if (ranked_segments.size() > 24) {
    ranked_segments.resize(24);
  }

  std::vector<int> selected_segment_ids;
  selected_segment_ids.reserve(ranked_segments.size());
  for (const ResidualSegmentCandidate& segment : ranked_segments) {
    selected_segment_ids.push_back(segment.seg_id);
  }
  const bool selected_segment_reorder_armed = !selected_segment_ids.empty();
  const bool txn_scan_armed = !selected_segment_ids.empty()
                              && legalm_stage3_exact_handoff_cells_ > 0
                              && exact_residual_cells > 0;
  std::vector<int> residual_anchor_cell_ids;
  int exact_txn_anchor_cells = 0;
  if (txn_scan_armed) {
    residual_anchor_cell_ids.reserve(ranked_segments.size() * 8);
    const std::unordered_set<int> selected_segment_id_set(
        selected_segment_ids.begin(), selected_segment_ids.end());
    for (const auto& node_ptr : network_->getNodes()) {
      Node* cell = node_ptr.get();
      if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
          || !cell->isStdCell()) {
        continue;
      }
      const int id = cell->getId();
      if (id < 0 || id >= static_cast<int>(legalm_target_valid_.size())
          || legalm_target_valid_[id] != 2) {
        continue;
      }
      const GridPt current = legalGridPt(cell, false);
      if (current.y.v != legalm_target_rows_[id]) {
        continue;
      }
      const int residual = std::abs(current.x.v - legalm_target_sites_[id]);
      if (residual < 2) {
        continue;
      }
      bool in_selected_segment = false;
      for (DetailedSeg* seg : mgr.getReverseCellToSegs(id)) {
        if (seg == nullptr || seg->getRowId() != current.y.v) {
          continue;
        }
        if (selected_segment_id_set.contains(seg->getSegId())) {
          in_selected_segment = true;
          break;
        }
      }
      if (!in_selected_segment) {
        continue;
      }
      residual_anchor_cell_ids.push_back(id);
      ++exact_txn_anchor_cells;
    }
  }
  const bool txn_run_armed = txn_scan_armed && !residual_anchor_cell_ids.empty();

  int scored_windows = 0;
  int accepted_windows = 0;
  double accepted_hpwl_gain = 0.0;
  DetailedReorderer::SelectedSegmentTxnStats txn_stats;
  if (!selected_segment_ids.empty()) {
    DetailedReorderer reorderer(arch_.get(), network_.get());
    reorderer.runSelectedSegments(&mgr,
                                  selected_segment_ids,
                                  3,
                                  96,
                                  12,
                                  scored_windows,
                                  accepted_windows,
                                  accepted_hpwl_gain);
    if (txn_run_armed) {
      reorderer.runSelectedSegmentTransactions(&mgr,
                                               selected_segment_ids,
                                               residual_anchor_cell_ids,
                                               24,
                                               128,
                                               12,
                                               txn_stats);
    }
  }
  logger_->metric("dpl_evolve__improve__selected_segment_residual_segments",
                  static_cast<int>(selected_segment_ids.size()));
  logger_->metric("dpl_evolve__improve__selected_segment_reorder_armed",
                  selected_segment_reorder_armed ? 1 : 0);
  logger_->metric("dpl_evolve__improve__selected_segment_control_guard", 1);
  logger_->metric("dpl_evolve__improve__selected_segment_vertical_frontier_donor",
                  1);
  logger_->metric("dpl_evolve__improve__selected_segment_exact_residual_cells",
                  exact_residual_cells);
  logger_->metric("dpl_evolve__improve__selected_segment_residual_windows",
                  scored_windows);
  logger_->metric("dpl_evolve__improve__selected_segment_residual_accepts",
                  accepted_windows);
  logger_->metric("dpl_evolve__improve__selected_segment_residual_hpwl_gain",
                  accepted_hpwl_gain);
  logger_->metric(
      "dpl_evolve__improve__selected_segment_txn_segments", txn_stats.segments);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_scan_armed",
                  txn_scan_armed ? 1 : 0);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_run_armed",
                  txn_run_armed ? 1 : 0);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_anchor_cells",
                  txn_stats.anchor_cells);
  logger_->metric(
      "dpl_evolve__improve__selected_segment_txn_exact_anchor_cells",
      exact_txn_anchor_cells);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_pair_candidates",
                  txn_stats.pair_candidates);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_cycle_candidates",
                  txn_stats.cycle_candidates);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_scored",
                  txn_stats.scored_transactions);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_accepts",
                  txn_stats.accepted_transactions);
  logger_->metric("dpl_evolve__improve__selected_segment_txn_hpwl_gain",
                  txn_stats.accepted_hpwl_gain);
  logger_->info(utl::DPL,
                1233,
                "Selected-segment residual reorder segments {}, exact cells "
                "{}, reorder armed {}, scored windows {}, accepts {}, exact "
                "HPWL gain {:.2f}.",
                selected_segment_ids.size(),
                exact_residual_cells,
                selected_segment_reorder_armed ? 1 : 0,
                scored_windows,
                accepted_windows,
                accepted_hpwl_gain);
  logger_->info(
      utl::DPL,
      1234,
      "Selected-anchor transactions scan armed {}, run armed {}, segments {}, "
      "anchor cells {}, exact anchor cells {}, pair candidates {}, cycle "
      "candidates {}, scored {}, accepts {}, exact HPWL gain {:.2f}.",
      txn_scan_armed ? 1 : 0,
      txn_run_armed ? 1 : 0,
      txn_stats.segments,
      txn_stats.anchor_cells,
      exact_txn_anchor_cells,
      txn_stats.pair_candidates,
      txn_stats.cycle_candidates,
      txn_stats.scored_transactions,
      txn_stats.accepted_transactions,
      txn_stats.accepted_hpwl_gain);
  logger_->info(utl::DPL,
                1245,
                "Vertical-frontier donor selected-segment guard enabled {}, "
                "segments {}, accepts {}, txn run armed {}.",
                1,
                selected_segment_ids.size(),
                accepted_windows,
                txn_run_armed ? 1 : 0);
  clearGuidedInitialLocations();

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
