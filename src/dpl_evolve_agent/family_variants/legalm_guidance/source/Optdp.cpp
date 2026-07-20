// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include <cstdint>
#include <algorithm>
#include <limits>
#include <unordered_map>

#include "dpl_evolve/Opendp.h"
#include "odb/util.h"
#include "utl/Logger.h"

// My stuff.
#include "graphics/DplObserver.h"
#include "infrastructure/Objects.h"
#include "infrastructure/architecture.h"
#include "infrastructure/detailed_segment.h"
#include "legalize_shift.h"
#include "objective/detailed_hpwl.h"
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

struct FrontierDpoCandidate
{
  Node* node = nullptr;
  DbuX target_x{0};
  DbuY target_y{0};
  DbuX original_x{0};
  DbuY original_y{0};
  int residual_sites = 0;
  int residual_rows = 0;
  double score = 0.0;
};

struct FrontierDpoStats
{
  int stored_frontier_cells = 0;
  int mapped_cells = 0;
  int attempted_cells = 0;
  int move_trials = 0;
  int legal_trials = 0;
  int accepted_moves = 0;
  int rejected_hpwl_trials = 0;
  int failed_trials = 0;
  int row_target_trials = 0;
  double hpwl_improvement_dbu = 0.0;
};

int nearestRow(Architecture* arch, const DbuY y)
{
  if (arch == nullptr || arch->getNumRows() == 0) {
    return -1;
  }
  int best_row = 0;
  int best_dist = std::abs((arch->getRow(0)->getBottom() - y).v);
  for (int row = 1; row < arch->getNumRows(); ++row) {
    const int dist = std::abs((arch->getRow(row)->getBottom() - y).v);
    if (dist < best_dist) {
      best_dist = dist;
      best_row = row;
    }
  }
  return best_row;
}

bool snapTrialToSegment(DetailedMgr& mgr,
                        Node* node,
                        DbuX& x,
                        const int row,
                        int& target_seg)
{
  if (node == nullptr || row < 0
      || row >= mgr.getArchitecture()->getNumRows()) {
    return false;
  }
  const auto* arch_row = mgr.getArchitecture()->getRow(row);
  const DbuX site_step = arch_row->getSiteSpacing();
  if (site_step.v <= 0) {
    return false;
  }

  std::pair<int, DbuX> best{-1, DbuX{0}};
  int best_abs_delta = std::numeric_limits<int>::max();
  for (int i = 0; i < mgr.getNumSegsInRow(row); ++i) {
    const DetailedSeg* seg = mgr.getSegsInRow(row)[i];
    if (seg == nullptr || seg->getRegId() != node->getGroupId()) {
      continue;
    }
    const DbuX min_x = seg->getMinX();
    const DbuX max_x = seg->getMaxX() - node->getWidth();
    if (max_x < min_x) {
      continue;
    }
    const int raw_sites = divRound((x - arch_row->getLeft()).v, site_step.v);
    DbuX snapped = arch_row->getLeft() + raw_sites * site_step.v;
    snapped = std::max(min_x, std::min(max_x, snapped));
    const int clamped_sites
        = divRound((snapped - arch_row->getLeft()).v, site_step.v);
    snapped = arch_row->getLeft() + clamped_sites * site_step.v;
    if (snapped < min_x || snapped > max_x) {
      continue;
    }
    const int abs_delta = std::abs((snapped - x).v);
    if (abs_delta < best_abs_delta) {
      best = {seg->getSegId(), snapped};
      best_abs_delta = abs_delta;
    }
  }
  if (best.first < 0) {
    return false;
  }
  target_seg = best.first;
  x = best.second;
  return true;
}

void addUniqueTrial(std::vector<std::pair<DbuX, DbuY>>& trials,
                    const DbuX x,
                    const DbuY y)
{
  for (const auto& trial : trials) {
    if (trial.first == x && trial.second == y) {
      return;
    }
  }
  trials.push_back({x, y});
}

void addRowAlignedTrials(std::vector<std::pair<DbuX, DbuY>>& trials,
                         DetailedMgr& mgr,
                         Node* node,
                         const DbuX x,
                         const DbuY y)
{
  Architecture* arch = mgr.getArchitecture();
  const int center_row = nearestRow(arch, y);
  if (center_row < 0) {
    return;
  }
  const int row_span = std::max(1, arch->getCellHeightInRows(node));
  for (int row_delta = 0; row_delta <= 1; ++row_delta) {
    const int rows[2] = {center_row - row_delta, center_row + row_delta};
    for (const int row : rows) {
      if (row < 0 || row + row_span > arch->getNumRows()) {
        continue;
      }
      const DbuY row_y = arch->getRow(row)->getBottom();
      addUniqueTrial(trials, x, row_y);
      if (row_delta == 0) {
        break;
      }
    }
  }
}

FrontierDpoStats consumeLegalmFrontier(
    utl::Logger* logger,
    DetailedMgr& mgr,
    Network* network,
    const std::vector<Opendp::LegalmFrontierCell>& stored_frontier)
{
  FrontierDpoStats stats;
  stats.stored_frontier_cells = static_cast<int>(stored_frontier.size());
  if (stored_frontier.empty() || network == nullptr) {
    return stats;
  }

  std::unordered_map<std::string, Node*> nodes_by_inst_name;
  nodes_by_inst_name.reserve(network->getNumNodes());
  for (auto& node_ptr : network->getNodes()) {
    Node* node = node_ptr.get();
    if (node != nullptr && node->getDbInst() != nullptr) {
      nodes_by_inst_name.emplace(node->getDbInst()->getName(), node);
    }
  }

  std::vector<FrontierDpoCandidate> candidates;
  candidates.reserve(stored_frontier.size());
  for (const auto& item : stored_frontier) {
    const auto node_it = nodes_by_inst_name.find(item.inst_name);
    if (node_it == nodes_by_inst_name.end()) {
      continue;
    }
    Node* node = node_it->second;
    if (node == nullptr || node->isFixed() || !node->isStdCell()
        || !node->isPlaced()
        || mgr.getNumReverseCellToSegs(node->getId()) == 0) {
      continue;
    }
    candidates.push_back({node,
                          DbuX{item.target_x},
                          DbuY{item.target_y},
                          DbuX{item.original_x},
                          DbuY{item.original_y},
                          item.residual_sites,
                          item.residual_rows,
                          item.score});
  }
  stats.mapped_cells = static_cast<int>(candidates.size());
  std::stable_sort(candidates.begin(),
                   candidates.end(),
                   [](const auto& lhs, const auto& rhs) {
                     if (lhs.score != rhs.score) {
                       return lhs.score > rhs.score;
                     }
                     return lhs.node->getId() < rhs.node->getId();
                   });

  constexpr int kMaxFrontierCells = 4000;
  if (static_cast<int>(candidates.size()) > kMaxFrontierCells) {
    candidates.resize(kMaxFrontierCells);
  }

  DetailedHPWL hpwl(network);
  hpwl.init(&mgr, nullptr);
  double current_hpwl = hpwl.curr();
  const int original_move_limit = mgr.getMoveLimit();
  mgr.setMoveLimit(48);
  mgr.resortSegments();

  for (const FrontierDpoCandidate& candidate : candidates) {
    Node* node = candidate.node;
    if (node == nullptr || mgr.getNumReverseCellToSegs(node->getId()) == 0) {
      continue;
    }
    ++stats.attempted_cells;
    std::vector<std::pair<DbuX, DbuY>> trials;
    trials.reserve(10);
    addRowAlignedTrials(
        trials, mgr, node, candidate.target_x, candidate.target_y);
    addRowAlignedTrials(
        trials,
        mgr,
        node,
        DbuX{(candidate.target_x.v + candidate.original_x.v) / 2},
        DbuY{(candidate.target_y.v + candidate.original_y.v) / 2});
    addRowAlignedTrials(
        trials, mgr, node, candidate.original_x, candidate.original_y);
    if (candidate.residual_sites != 0) {
      const int halfway_x = node->getLeft().v
                            - (candidate.residual_sites
                               * node->siteWidth().v)
                                  / 2;
      addRowAlignedTrials(
          trials, mgr, node, DbuX{halfway_x}, candidate.target_y);
    }

    bool accepted_for_cell = false;
    for (const auto& [trial_x, trial_y] : trials) {
      if (accepted_for_cell) {
        break;
      }
      const auto& current_segments = mgr.getReverseCellToSegs(node->getId());
      if (current_segments.empty()) {
        break;
      }
      const int current_seg = current_segments.front()->getSegId();
      const int target_row = nearestRow(mgr.getArchitecture(), trial_y);
      DbuX snapped_x = trial_x;
      int target_seg = -1;
      if (!snapTrialToSegment(mgr, node, snapped_x, target_row, target_seg)) {
        ++stats.failed_trials;
        continue;
      }
      ++stats.move_trials;
      if (target_row >= 0
          && target_row != mgr.getSegment(current_seg)->getRowId()) {
        ++stats.row_target_trials;
      }
      if (!mgr.tryMove(node,
                       node->getLeft(),
                       node->getBottom(),
                       current_seg,
                       snapped_x,
                       mgr.getArchitecture()->getRow(target_row)->getBottom(),
                       target_seg)) {
        ++stats.failed_trials;
        continue;
      }
      ++stats.legal_trials;
      const double delta = hpwl.delta(mgr.getJournal());
      if (delta > 0.0) {
        hpwl.accept();
        mgr.acceptMove();
        current_hpwl -= delta;
        stats.hpwl_improvement_dbu += delta;
        ++stats.accepted_moves;
        accepted_for_cell = true;
      } else {
        mgr.rejectMove();
        ++stats.rejected_hpwl_trials;
      }
    }
  }

  mgr.setMoveLimit(original_move_limit);
  mgr.resortSegments();
  logger->metric("dpl_evolve__frontier_dpo__hpwl_after_internal",
                 current_hpwl);
  return stats;
}
}  // namespace

int Opendp::runLegalmFrontierDpo(DetailedMgr& mgr)
{
  const FrontierDpoStats stats
      = consumeLegalmFrontier(logger_, mgr, network_.get(), legalmFrontier());
  logger_->metric("dpl_evolve__frontier_dpo__stored_frontier_cells",
                  stats.stored_frontier_cells);
  logger_->metric("dpl_evolve__frontier_dpo__mapped_cells",
                  stats.mapped_cells);
  logger_->metric("dpl_evolve__frontier_dpo__attempted_cells",
                  stats.attempted_cells);
  logger_->metric("dpl_evolve__frontier_dpo__move_trials",
                  stats.move_trials);
  logger_->metric("dpl_evolve__frontier_dpo__legal_trials",
                  stats.legal_trials);
  logger_->metric("dpl_evolve__frontier_dpo__accepted_moves",
                  stats.accepted_moves);
  logger_->metric("dpl_evolve__frontier_dpo__rejected_hpwl_trials",
                  stats.rejected_hpwl_trials);
  logger_->metric("dpl_evolve__frontier_dpo__failed_trials",
                  stats.failed_trials);
  logger_->metric("dpl_evolve__frontier_dpo__row_target_trials",
                  stats.row_target_trials);
  logger_->metric("dpl_evolve__frontier_dpo__hpwl_improvement_dbu",
                  stats.hpwl_improvement_dbu);
  logger_->info(DPL,
                1240,
                "LEGALM frontier DPO consumed {} mapped cells from {} stored "
                "frontier records (trials {}, legal {}, accepted {}, exact "
                "HPWL improvement {:.0f} dbu).",
                stats.mapped_cells,
                stats.stored_frontier_cells,
                stats.move_trials,
                stats.legal_trials,
                stats.accepted_moves,
                stats.hpwl_improvement_dbu);
  return stats.accepted_moves;
}

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
  const int frontier_moves = runLegalmFrontierDpo(mgr);
  logger_->metric("dpl_evolve__frontier_dpo__consumer_before_script", 1);
  logger_->metric("dpl_evolve__frontier_dpo__script_followup_enabled", 1);
  logger_->metric("dpl_evolve__frontier_dpo__accepted_before_script",
                  frontier_moves);

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
