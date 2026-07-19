// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

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
#include "detailed_manager.h"
#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
#include "infrastructure/Objects.h"
#include "infrastructure/architecture.h"
#include "infrastructure/detailed_segment.h"
#include "objective/detailed_hpwl.h"
#include "util/journal.h"
#include "util/utility.h"
#include "utl/Logger.h"

using utl::DPL;

namespace dpl_evolve {

namespace {
constexpr int kMaxAcceptedWindowReplayStoredSeeds = 48;
constexpr int kMaxAcceptedWindowReplaySeeds = 32;
constexpr int kMaxAcceptedWindowReplayMicroSeeds = 12;
constexpr int kMaxAcceptedWindowReplayCascadeSeeds = 12;
constexpr int kResidualReplayMaxCells = 7;
constexpr int kResidualReplayMaxShiftCandidates = 4;
constexpr int kResidualReplayCascadeHalo = 2;
constexpr int kResidualReplayStrongCascadeHalo = 3;
constexpr int kResidualReplayStrongCascadeMaxCells = 8;
constexpr int kResidualReplayCascadeShiftCandidates = 5;
constexpr int kResidualReplayStrongCascadeShiftCandidates = 6;
constexpr double kResidualReplayStrongCascadeDeltaThreshold = 4000.0;
constexpr double kResidualReplayDispPenaltyScale = 0.5;

void appendJournal(const Journal& source, Journal& dest)
{
  for (const auto& action_ptr : source) {
    if (action_ptr == nullptr
        || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
      continue;
    }
    const auto* move_action = static_cast<const MoveCellAction*>(action_ptr.get());
    dest.addAction(MoveCellAction(move_action->getNode(),
                                  move_action->getOrigLeft(),
                                  move_action->getOrigBottom(),
                                  move_action->getNewLeft(),
                                  move_action->getNewBottom(),
                                  move_action->wasPlaced(),
                                  move_action->getOrigSegs(),
                                  move_action->getNewSegs()));
  }
}

struct PackWindowCandidate
{
  double score = 0.0;
  double local_score = 0.0;
  int start = 0;
  int stop = 0;
  DbuX leftLimit{0};
  DbuX rightLimit{0};
  int endpoint_cells = 0;
  int source_edge_cells = 0;
  int boundary_source_edge_cells = 0;
  int source_edge_segment_weight = 0;
  uint64_t endpoint_hpwl = 0;
  uint64_t source_edge_hpwl = 0;
  uint64_t boundary_source_edge_hpwl = 0;
};

void insertTopWindow(std::vector<PackWindowCandidate>& windows,
                     const PackWindowCandidate& candidate,
                     const int max_windows)
{
  windows.push_back(candidate);
  std::sort(windows.begin(),
            windows.end(),
            [](const PackWindowCandidate& lhs, const PackWindowCandidate& rhs) {
              if (lhs.score != rhs.score) {
                return lhs.score > rhs.score;
              }
              if (lhs.source_edge_cells != rhs.source_edge_cells) {
                return lhs.source_edge_cells > rhs.source_edge_cells;
              }
              if (lhs.boundary_source_edge_cells
                  != rhs.boundary_source_edge_cells) {
                return lhs.boundary_source_edge_cells
                       > rhs.boundary_source_edge_cells;
              }
              if (lhs.endpoint_cells != rhs.endpoint_cells) {
                return lhs.endpoint_cells > rhs.endpoint_cells;
              }
              if (lhs.boundary_source_edge_hpwl
                  != rhs.boundary_source_edge_hpwl) {
                return lhs.boundary_source_edge_hpwl
                       > rhs.boundary_source_edge_hpwl;
              }
              if (lhs.source_edge_hpwl != rhs.source_edge_hpwl) {
                return lhs.source_edge_hpwl > rhs.source_edge_hpwl;
              }
              if (lhs.endpoint_hpwl != rhs.endpoint_hpwl) {
                return lhs.endpoint_hpwl > rhs.endpoint_hpwl;
              }
              return lhs.local_score > rhs.local_score;
            });
  if (static_cast<int>(windows.size()) > max_windows) {
    windows.resize(max_windows);
  }
}

std::vector<int> buildShiftCandidates(const int slack_sites,
                                      const int max_shift_candidates)
{
  std::vector<int> shifts;
  auto add_shift = [&](int shift) {
    shift = std::max(0, std::min(slack_sites, shift));
    if (std::find(shifts.begin(), shifts.end(), shift) == shifts.end()) {
      shifts.push_back(shift);
    }
  };

  add_shift(0);
  add_shift(slack_sites);
  if (max_shift_candidates > 2 && slack_sites > 0) {
    for (int idx = 1; idx + 1 < max_shift_candidates; idx++) {
      add_shift((idx * slack_sites) / (max_shift_candidates - 1));
    }
  }
  std::sort(shifts.begin(), shifts.end());
  return shifts;
}

void restoreWindowPositions(DetailedMgr* mgrPtr,
                            const std::vector<Node*>& nodes,
                            const int jstrt,
                            const std::vector<DbuX>& orig_left)
{
  for (int i = 0; i < static_cast<int>(orig_left.size()); i++) {
    Node* ndi = nodes[jstrt + i];
    mgrPtr->eraseFromGrid(ndi);
    ndi->setLeft(orig_left[i]);
    mgrPtr->paintInGrid(ndi);
  }
}

std::vector<Node*> collectWindowNodes(const std::vector<Node*>& nodes,
                                      const int start,
                                      const int stop)
{
  if (start < 0 || stop < start || stop >= static_cast<int>(nodes.size())) {
    return {};
  }
  return std::vector<Node*>(nodes.begin() + start, nodes.begin() + stop + 1);
}
}  // namespace

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
DetailedReorderer::DetailedReorderer(Architecture* arch, Network* network)
    : arch_(arch), network_(network)
{
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::run(DetailedMgr* mgrPtr, const std::string& command)
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

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::run(DetailedMgr* mgrPtr,
                            const std::vector<std::string>& args)
{
  // Given the arguments, figure out which routine to run to do the reordering.

  mgrPtr_ = mgrPtr;

  windowSize_ = 3;

  int passes = 1;
  double tol = 0.01;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-w" && i + 1 < args.size()) {
      windowSize_ = std::atoi(args[++i].c_str());
    } else if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    }
  }
  windowSize_ = std::min(4, std::max(2, windowSize_));
  tol = std::max(tol, 0.01);

  mgrPtr_->resortSegments();
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init();
  hpwl_obj.curr();
  activeHpwlObj_ = &hpwl_obj;

  uint64_t hpwl_x, hpwl_y;
  int64_t curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  const int64_t init_hpwl = curr_hpwl;
  if (init_hpwl == 0) {
    activeHpwlObj_ = nullptr;
    return;
  }
  for (int p = 1; p <= passes; p++) {
    const int64_t last_hpwl = curr_hpwl;

    reorder();

    curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);

    mgrPtr_->getLogger()->info(DPL,
                               304,
                               "Pass {:3d} of reordering; objective is {:.6e}.",
                               p,
                               (double) curr_hpwl);
    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / (double) last_hpwl <= tol) {
      // std::cout << "Terminating due to low improvement." << std::endl;
      break;
    }
  }
  activeHpwlObj_ = nullptr;
  mgrPtr_->resortSegments();
  const double curr_imp
      = (((init_hpwl - curr_hpwl) / (double) init_hpwl) * 100.);
  mgrPtr_->getLogger()->info(
      DPL,
      305,
      "End of reordering; objective is {:.6e}, improvement is {:.2f} percent.",
      (double) curr_hpwl,
      curr_imp);
  mgrPtr_->getLogger()->info(
      DPL,
      931,
      "Hot-segment reorder handoff: prioritized_segments={}, "
      "visited_hot_segments={}, base_window={}, hot_window={}, "
      "hot_seed_segments={}, hot_expanded_segments={}, hot_passes={}, "
      "hot_replay_frontier={}, "
      "hot_exact_windows_scored={}, hot_exact_edges_scored={}, "
      "hot_windows_attempted={}, hot_windows_accepted={}, "
      "hot_windows_rejected={}, hot_rollback_failures={}, "
      "hot_accepted_delta={}, micro_pack_frontier={}, "
      "micro_pack_passes={}, micro_pack_windows={}, "
      "micro_pack_endpoint_windows={}, "
      "micro_pack_boundary_source_edge_windows={}, "
      "micro_pack_source_edge_windows={}, "
      "micro_pack_probes={}, micro_pack_accepts={}, "
      "micro_pack_rollback_failures={}, micro_pack_edges_scored={}, "
      "micro_pack_accepted_delta={}.",
      prioritizedSegmentsLastPass_,
      visitedHotSegmentsLastPass_,
      windowSize_,
      hotWindowSizeLastPass_,
      hotSeedSegmentsLastPass_,
      hotExpandedSegmentsLastPass_,
      hotPolishPassesLastRun_,
      hotReplayFrontierLastPass_,
      hotExactWindowsScoredLastRun_,
      hotExactEdgesScoredLastRun_,
      hotWindowsAttemptedLastRun_,
      hotWindowsAcceptedLastRun_,
      hotWindowsRejectedLastRun_,
      hotReplayFailuresLastRun_,
      hotAcceptedDeltaLastRun_,
      hotMicroPackFrontierLastPass_,
      hotMicroPackPassesLastRun_,
      hotMicroPackWindowsLastRun_,
      hotMicroPackEndpointWindowsLastRun_,
      hotMicroPackBoundarySourceEdgeWindowsLastRun_,
      hotMicroPackSourceEdgeWindowsLastRun_,
      hotMicroPackProbesLastRun_,
      hotMicroPackAcceptsLastRun_,
      hotMicroPackReplayFailuresLastRun_,
      hotMicroPackEdgesScoredLastRun_,
      hotMicroPackAcceptedDeltaLastRun_);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::noteAcceptedWindowReplaySeed(
    const std::vector<Node*>& nodes,
    const int segId,
    const DbuX leftLimit,
    const DbuX rightLimit,
    const double acceptedDelta,
    const int endpointCells,
    const int sourceEdgeCells,
    const int sourceEdgeSegmentWeight,
    const bool fromMicroPack)
{
  if (nodes.size() < 3 || segId < 0 || acceptedDelta <= 0.0) {
    return;
  }
  for (Node* node : nodes) {
    if (node == nullptr) {
      return;
    }
  }

  AcceptedWindowReplaySeed seed;
  seed.nodes = nodes;
  seed.segId = segId;
  seed.leftLimit = leftLimit;
  seed.rightLimit = rightLimit;
  seed.acceptedDelta = acceptedDelta;
  seed.endpointCells = endpointCells;
  seed.sourceEdgeCells = sourceEdgeCells;
  seed.sourceEdgeSegmentWeight = sourceEdgeSegmentWeight;
  seed.fromMicroPack = fromMicroPack;
  seed.priority
      = acceptedDelta + 120.0 * static_cast<double>(endpointCells)
        + 180.0 * static_cast<double>(sourceEdgeCells)
        + 60.0 * static_cast<double>(std::min(sourceEdgeSegmentWeight, 4))
        + (fromMicroPack ? 90.0 : 0.0);

  const auto same_seed = [&](const AcceptedWindowReplaySeed& existing) {
    if (existing.segId != seed.segId
        || existing.fromMicroPack != seed.fromMicroPack
        || existing.nodes.size() != seed.nodes.size()) {
      return false;
    }
    for (size_t idx = 0; idx < seed.nodes.size(); idx++) {
      if (existing.nodes[idx] != seed.nodes[idx]) {
        return false;
      }
    }
    return true;
  };

  auto existing_it = std::find_if(acceptedWindowReplaySeeds_.begin(),
                                  acceptedWindowReplaySeeds_.end(),
                                  same_seed);
  if (existing_it != acceptedWindowReplaySeeds_.end()) {
    if (existing_it->priority >= seed.priority) {
      return;
    }
    *existing_it = seed;
  } else {
    acceptedWindowReplaySeeds_.push_back(seed);
  }

  std::stable_sort(
      acceptedWindowReplaySeeds_.begin(),
      acceptedWindowReplaySeeds_.end(),
      [](const AcceptedWindowReplaySeed& lhs,
         const AcceptedWindowReplaySeed& rhs) {
        if (lhs.priority != rhs.priority) {
          return lhs.priority > rhs.priority;
        }
        if (lhs.acceptedDelta != rhs.acceptedDelta) {
          return lhs.acceptedDelta > rhs.acceptedDelta;
        }
        return lhs.nodes.size() > rhs.nodes.size();
      });
  if (static_cast<int>(acceptedWindowReplaySeeds_.size())
      > kMaxAcceptedWindowReplayStoredSeeds) {
    acceptedWindowReplaySeeds_.resize(kMaxAcceptedWindowReplayStoredSeeds);
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runAcceptedWindowResidualReplay()
{
  std::vector<const AcceptedWindowReplaySeed*> selected_seeds;
  selected_seeds.reserve(
      std::min(static_cast<int>(acceptedWindowReplaySeeds_.size()),
               kMaxAcceptedWindowReplaySeeds));
  for (const AcceptedWindowReplaySeed& seed : acceptedWindowReplaySeeds_) {
    if (!seed.fromMicroPack
        || static_cast<int>(selected_seeds.size())
               >= kMaxAcceptedWindowReplayMicroSeeds) {
      continue;
    }
    selected_seeds.push_back(&seed);
  }
  for (const AcceptedWindowReplaySeed& seed : acceptedWindowReplaySeeds_) {
    if (static_cast<int>(selected_seeds.size()) >= kMaxAcceptedWindowReplaySeeds) {
      break;
    }
    const bool already_selected
        = std::find(selected_seeds.begin(), selected_seeds.end(), &seed)
          != selected_seeds.end();
    if (!already_selected) {
      selected_seeds.push_back(&seed);
    }
  }

  residualReplaySeedCountLastRun_
      = static_cast<int>(selected_seeds.size());
  residualReplayHotSeedCountLastRun_ = 0;
  residualReplayMicroSeedCountLastRun_ = 0;
  residualReplayPass1AcceptsLastRun_ = 0;
  residualReplayCascadeSeedCountLastRun_ = 0;
  residualReplayExpandedCascadeSeedsLastRun_ = 0;
  residualReplayCascadeProbesLastRun_ = 0;
  residualReplayCascadeAcceptsLastRun_ = 0;
  residualReplayRollbackRejectsLastRun_ = 0;
  if (selected_seeds.empty() || activeHpwlObj_ == nullptr) {
    return;
  }
  for (const AcceptedWindowReplaySeed* seed_ptr : selected_seeds) {
    const AcceptedWindowReplaySeed& seed = *seed_ptr;
    if (seed.fromMicroPack) {
      residualReplayMicroSeedCountLastRun_++;
    } else {
      residualReplayHotSeedCountLastRun_++;
    }
  }

  const auto buildInsertionOrder = [](const int size,
                                      const int mover_idx,
                                      const int target_idx) {
    std::vector<int> order;
    order.reserve(size);
    for (int idx = 0; idx < size; idx++) {
      if (idx != mover_idx) {
        order.push_back(idx);
      }
    }
    order.insert(order.begin() + target_idx, mover_idx);
    return order;
  };

  auto insert_seed = [&](std::vector<AcceptedWindowReplaySeed>& seeds,
                         AcceptedWindowReplaySeed seed,
                         const int max_seeds) {
    const auto same_seed = [&](const AcceptedWindowReplaySeed& existing) {
      if (existing.segId != seed.segId
          || existing.fromMicroPack != seed.fromMicroPack
          || existing.nodes.size() != seed.nodes.size()) {
        return false;
      }
      for (size_t idx = 0; idx < seed.nodes.size(); idx++) {
        if (existing.nodes[idx] != seed.nodes[idx]) {
          return false;
        }
      }
      return true;
    };

    auto existing_it = std::find_if(seeds.begin(), seeds.end(), same_seed);
    if (existing_it != seeds.end()) {
      if (existing_it->priority >= seed.priority) {
        return;
      }
      *existing_it = std::move(seed);
    } else {
      seeds.push_back(std::move(seed));
    }

    std::stable_sort(
        seeds.begin(),
        seeds.end(),
        [](const AcceptedWindowReplaySeed& lhs,
           const AcceptedWindowReplaySeed& rhs) {
          if (lhs.priority != rhs.priority) {
            return lhs.priority > rhs.priority;
          }
          if (lhs.acceptedDelta != rhs.acceptedDelta) {
            return lhs.acceptedDelta > rhs.acceptedDelta;
          }
          return lhs.nodes.size() > rhs.nodes.size();
        });
    if (static_cast<int>(seeds.size()) > max_seeds) {
      seeds.resize(max_seeds);
    }
  };

  auto replay_seed = [&](const AcceptedWindowReplaySeed& seed,
                         const int halo_cells,
                         const int max_cells,
                         const int max_shift_candidates,
                         int* pass_probe_counter,
                         int* pass_accept_counter,
                         AcceptedWindowReplaySeed* cascade_seed_out) {
    if (seed.nodes.empty()) {
      residualReplayFailuresLastRun_++;
      return;
    }

    const auto& seed_segs = mgrPtr_->getReverseCellToSegs(seed.nodes.front()->getId());
    if (seed_segs.empty() || seed_segs.front() == nullptr) {
      residualReplayFailuresLastRun_++;
      return;
    }
    const int segId = seed_segs.front()->getSegId();
    if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
      residualReplayFailuresLastRun_++;
      return;
    }

    mgrPtr_->sortCellsInSeg(segId);
    const std::vector<Node*>& seg_nodes = mgrPtr_->getCellsInSeg(segId);
    if (seg_nodes.size() < 3) {
      return;
    }

    std::unordered_set<int> seed_ids;
    for (Node* node : seed.nodes) {
      if (node != nullptr) {
        seed_ids.insert(node->getId());
      }
    }
    int first = static_cast<int>(seg_nodes.size());
    int last = -1;
    for (int idx = 0; idx < static_cast<int>(seg_nodes.size()); idx++) {
      if (seed_ids.find(seg_nodes[idx]->getId()) != seed_ids.end()) {
        first = std::min(first, idx);
        last = std::max(last, idx);
      }
    }
    if (last < first) {
      residualReplayFailuresLastRun_++;
      return;
    }

    int start = std::max(0, first - halo_cells);
    int stop = std::min(static_cast<int>(seg_nodes.size()) - 1, last + halo_cells);
    while (stop - start + 1 > max_cells) {
      if (first - start > stop - last) {
        start++;
      } else {
        stop--;
      }
    }
    const int size = stop - start + 1;
    if (size < 3) {
      return;
    }

    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    const Node* nextPtr
        = (stop + 1 < static_cast<int>(seg_nodes.size())) ? seg_nodes[stop + 1]
                                                          : nullptr;
    DbuX rightLimit{segPtr->getMaxX()};
    if (nextPtr != nullptr) {
      int leftPadding, rightPadding;
      arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
      rightLimit = std::min(nextPtr->getLeft() - leftPadding, rightLimit);
    }
    const Node* prevPtr = (start > 0) ? seg_nodes[start - 1] : nullptr;
    DbuX leftLimit{segPtr->getMinX()};
    if (prevPtr != nullptr) {
      int leftPadding, rightPadding;
      arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
      leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
    }

    std::vector<Node*> window_nodes = collectWindowNodes(seg_nodes, start, stop);
    std::vector<DbuX> curr_left(size, DbuX{0});
    std::vector<DbuX> left_padding(size, DbuX{0});
    std::vector<DbuX> right_padding(size, DbuX{0});
    std::vector<DbuX> widths(size, DbuX{0});
    DbuX packed_width{0};
    int current_max_disp = 0;
    for (int idx = 0; idx < size; idx++) {
      Node* node = window_nodes[idx];
      curr_left[idx] = node->getLeft();
      arch_->getCellPadding(node, left_padding[idx], right_padding[idx]);
      widths[idx] = node->getWidth();
      packed_width += widths[idx] + left_padding[idx] + right_padding[idx];
      current_max_disp
          = std::max(current_max_disp,
                     std::abs((node->getLeft() - node->getOrigLeft()).v));
    }
    if (rightLimit - leftLimit < packed_width) {
      return;
    }

    residualReplayWindowsLastRun_++;
    const DbuX siteWidth = arch_->getRow(segPtr->getRowId())->getSiteWidth();
    const int slack_sites
        = ((rightLimit - leftLimit - packed_width) / siteWidth).v;
    const std::vector<int> shift_candidates
        = buildShiftCandidates(slack_sites, max_shift_candidates);
    const std::vector<int> window_segments{segId};

    Journal bestJournal(mgrPtr_->getGrid(), mgrPtr_);
    double best_delta = 0.0;
    double best_score = 0.0;
    bool found = false;

    auto evaluate_order = [&](const std::vector<int>& order) {
      if (order.size() != static_cast<size_t>(size)) {
        return;
      }
      for (const int shift_sites : shift_candidates) {
        DbuX x = leftLimit + shift_sites * siteWidth;
        bool failed = false;
        int candidate_max_disp = current_max_disp;
        for (int slot = 0; slot < size; slot++) {
          const int idx = order[slot];
          Node* node = window_nodes[idx];
          x += left_padding[idx];
          mgrPtr_->eraseFromGrid(node);
          node->setLeft(x);
          mgrPtr_->paintInGrid(node);
          x += widths[idx];
          x += right_padding[idx];
          candidate_max_disp = std::max(
              candidate_max_disp,
              std::abs((node->getLeft() - node->getOrigLeft()).v));
          if (std::abs((node->getLeft() - node->getOrigLeft()).v)
              > mgrPtr_->getMaxDisplacementX()) {
            failed = true;
          }
        }

        if (!failed) {
          DbuX left = leftLimit;
          for (int slot = 0; slot < size; slot++) {
            const int idx = order[slot];
            Node* node = window_nodes[idx];
            DbuX aligned = node->getLeft();
            if (!mgrPtr_->alignPos(node, aligned, left, rightLimit)) {
              failed = true;
              break;
            }
            mgrPtr_->eraseFromGrid(node);
            node->setLeft(aligned);
            mgrPtr_->paintInGrid(node);
            left = node->getRight();
            candidate_max_disp = std::max(
                candidate_max_disp,
                std::abs((node->getLeft() - node->getOrigLeft()).v));
            if (std::abs((node->getLeft() - node->getOrigLeft()).v)
                > mgrPtr_->getMaxDisplacementX()) {
              failed = true;
              break;
            }
          }
        }

        if (!failed) {
          for (Node* node : window_nodes) {
            if (mgrPtr_->hasPlacementViolation(node)) {
              failed = true;
              break;
            }
          }
        }

        if (failed) {
          restoreWindowPositions(mgrPtr_, window_nodes, 0, curr_left);
          residualReplayRollbackRejectsLastRun_++;
          continue;
        }

        Journal candidate(mgrPtr_->getGrid(), mgrPtr_);
        for (int idx = 0; idx < size; idx++) {
          Node* node = window_nodes[idx];
          if (node->getLeft() == curr_left[idx]) {
            continue;
          }
          candidate.addAction(MoveCellAction(node,
                                             curr_left[idx],
                                             node->getBottom(),
                                             node->getLeft(),
                                             node->getBottom(),
                                             true,
                                             window_segments,
                                             window_segments));
        }
        if (candidate.empty()) {
          restoreWindowPositions(mgrPtr_, window_nodes, 0, curr_left);
          continue;
        }

        residualReplayProbesLastRun_++;
        if (pass_probe_counter != nullptr) {
          (*pass_probe_counter)++;
        }
        int scored_edges = 0;
        const double curr_delta = activeHpwlObj_->delta(candidate, &scored_edges);
        residualReplayEdgesScoredLastRun_ += scored_edges;

        const int extra_disp = std::max(0, candidate_max_disp - current_max_disp);
        const double score = curr_delta
                             - kResidualReplayDispPenaltyScale
                                   * static_cast<double>(extra_disp);
        if (curr_delta <= 0.0) {
          residualReplayRollbackRejectsLastRun_++;
        } else if (score <= 0.0) {
          residualReplayLowConversionRejectsLastRun_++;
          residualReplayRollbackRejectsLastRun_++;
        } else if (score > best_score
                   || (score == best_score && curr_delta > best_delta)) {
          best_score = score;
          best_delta = curr_delta;
          bestJournal.clear();
          appendJournal(candidate, bestJournal);
          found = true;
        } else {
          residualReplayRollbackRejectsLastRun_++;
        }

        restoreWindowPositions(mgrPtr_, window_nodes, 0, curr_left);
      }
    };

    std::vector<int> base_order(size, 0);
    for (int idx = 0; idx < size; idx++) {
      base_order[idx] = idx;
    }
    evaluate_order(base_order);
    for (int mover_idx = 0; mover_idx < size; mover_idx++) {
      for (int target_idx = 0; target_idx < size; target_idx++) {
        if (target_idx == mover_idx) {
          continue;
        }
        evaluate_order(buildInsertionOrder(size, mover_idx, target_idx));
      }
    }

    restoreWindowPositions(mgrPtr_, window_nodes, 0, curr_left);
    if (!found || bestJournal.empty() || best_delta <= 0.0) {
      return;
    }

    bestJournal.redo();
    mgrPtr_->sortCellsInSeg(segId, start, stop + 1);
    activeHpwlObj_->delta(bestJournal);
    activeHpwlObj_->accept();
    residualReplayAcceptsLastRun_++;
    if (pass_accept_counter != nullptr) {
      (*pass_accept_counter)++;
    }
    residualReplayAcceptedDeltaLastRun_ += static_cast<int64_t>(best_delta);

    if (cascade_seed_out != nullptr) {
      AcceptedWindowReplaySeed accepted_seed;
      accepted_seed.nodes = collectWindowNodes(mgrPtr_->getCellsInSeg(segId), start, stop);
      std::stable_sort(accepted_seed.nodes.begin(),
                       accepted_seed.nodes.end(),
                       [](Node* lhs, Node* rhs) {
                         return lhs->getLeft() < rhs->getLeft();
                       });
      accepted_seed.segId = segId;
      accepted_seed.leftLimit = leftLimit;
      accepted_seed.rightLimit = rightLimit;
      accepted_seed.acceptedDelta = best_delta;
      accepted_seed.endpointCells = seed.endpointCells;
      accepted_seed.sourceEdgeSegmentWeight
          = mgrPtr_->getSourceEdgeHotSegmentWeight(segId);
      accepted_seed.fromMicroPack = seed.fromMicroPack;
      accepted_seed.sourceEdgeCells = 0;
      for (Node* node : accepted_seed.nodes) {
        accepted_seed.sourceEdgeCells += static_cast<int>(
            mgrPtr_->isSourceEdgeHotCell(node->getId()));
      }
      accepted_seed.priority
          = best_delta + 120.0 * static_cast<double>(accepted_seed.endpointCells)
            + 180.0 * static_cast<double>(accepted_seed.sourceEdgeCells)
            + 60.0
                  * static_cast<double>(
                      std::min(accepted_seed.sourceEdgeSegmentWeight, 4))
            + (accepted_seed.fromMicroPack ? 90.0 : 0.0);
      *cascade_seed_out = std::move(accepted_seed);
    }
  };

  std::vector<AcceptedWindowReplaySeed> cascade_seed_storage;
  for (const AcceptedWindowReplaySeed* seed_ptr : selected_seeds) {
    AcceptedWindowReplaySeed cascade_seed;
    replay_seed(*seed_ptr,
                1,
                kResidualReplayMaxCells,
                kResidualReplayMaxShiftCandidates,
                nullptr,
                &residualReplayPass1AcceptsLastRun_,
                &cascade_seed);
    if (!cascade_seed.nodes.empty() && cascade_seed.acceptedDelta > 0.0) {
      insert_seed(cascade_seed_storage,
                  std::move(cascade_seed),
                  kMaxAcceptedWindowReplayCascadeSeeds);
    }
  }

  residualReplayCascadeSeedCountLastRun_
      = static_cast<int>(cascade_seed_storage.size());
  for (const AcceptedWindowReplaySeed& cascade_seed : cascade_seed_storage) {
    const bool strong_seed = cascade_seed.fromMicroPack
                             || cascade_seed.sourceEdgeCells > 0
                             || cascade_seed.sourceEdgeSegmentWeight >= 2
                             || cascade_seed.acceptedDelta
                                    >= kResidualReplayStrongCascadeDeltaThreshold;
    if (strong_seed) {
      residualReplayExpandedCascadeSeedsLastRun_++;
    }
    replay_seed(cascade_seed,
                strong_seed ? kResidualReplayStrongCascadeHalo
                            : kResidualReplayCascadeHalo,
                strong_seed ? kResidualReplayStrongCascadeMaxCells
                            : kResidualReplayMaxCells,
                strong_seed ? kResidualReplayStrongCascadeShiftCandidates
                            : kResidualReplayCascadeShiftCandidates,
                &residualReplayCascadeProbesLastRun_,
                &residualReplayCascadeAcceptsLastRun_,
                nullptr);
  }

  mgrPtr_->getLogger()->info(
      DPL,
      937,
      "Accepted-window residual replay: seeds={}, hot_seeds={}, "
      "micro_pack_seeds={}, replay_windows={}, exact_probes={}, "
      "accepts={}, pass1_probes={}, pass1_accepts={}, cascade_seeds={}, expanded_cascade_seeds={}, pass2_probes={}, "
      "pass2_accepts={}, rollback_rejects={}, "
      "low_conversion_rejects={}, seed_failures={}, "
      "exact_edges_scored={}, accepted_delta={}.",
      residualReplaySeedCountLastRun_,
      residualReplayHotSeedCountLastRun_,
      residualReplayMicroSeedCountLastRun_,
      residualReplayWindowsLastRun_,
      residualReplayProbesLastRun_,
      residualReplayAcceptsLastRun_,
      residualReplayProbesLastRun_ - residualReplayCascadeProbesLastRun_,
      residualReplayPass1AcceptsLastRun_,
      residualReplayCascadeSeedCountLastRun_,
      residualReplayExpandedCascadeSeedsLastRun_,
      residualReplayCascadeProbesLastRun_,
      residualReplayCascadeAcceptsLastRun_,
      residualReplayRollbackRejectsLastRun_,
      residualReplayLowConversionRejectsLastRun_,
      residualReplayFailuresLastRun_,
      residualReplayEdgesScoredLastRun_,
      residualReplayAcceptedDeltaLastRun_);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  std::vector<int> segment_order;
  std::vector<uint8_t> is_hot(mgrPtr_->getNumSegments(), 0);
  prioritizedSegmentsLastPass_ = 0;
  visitedHotSegmentsLastPass_ = 0;
  hotWindowSizeLastPass_ = 4;
  hotSeedSegmentsLastPass_ = 0;
  hotExpandedSegmentsLastPass_ = 0;
  hotReplayFrontierLastPass_ = 0;
  hotPolishPassesLastRun_ = 0;
  hotExactWindowsScoredLastRun_ = 0;
  hotExactEdgesScoredLastRun_ = 0;
  hotWindowsAttemptedLastRun_ = 0;
  hotWindowsAcceptedLastRun_ = 0;
  hotWindowsRejectedLastRun_ = 0;
  hotReplayFailuresLastRun_ = 0;
  hotAcceptedDeltaLastRun_ = 0;
  hotMicroPackFrontierLastPass_ = 0;
  hotMicroPackPassesLastRun_ = 0;
  hotMicroPackWindowsLastRun_ = 0;
  hotMicroPackEndpointWindowsLastRun_ = 0;
  hotMicroPackBoundarySourceEdgeWindowsLastRun_ = 0;
  hotMicroPackSourceEdgeWindowsLastRun_ = 0;
  hotMicroPackProbesLastRun_ = 0;
  hotMicroPackAcceptsLastRun_ = 0;
  hotMicroPackReplayFailuresLastRun_ = 0;
  hotMicroPackEdgesScoredLastRun_ = 0;
  hotMicroPackAcceptedDeltaLastRun_ = 0;
  acceptedWindowReplaySeeds_.clear();
  residualReplaySeedCountLastRun_ = 0;
  residualReplayHotSeedCountLastRun_ = 0;
  residualReplayMicroSeedCountLastRun_ = 0;
  residualReplayWindowsLastRun_ = 0;
  residualReplayProbesLastRun_ = 0;
  residualReplayAcceptsLastRun_ = 0;
  residualReplayPass1AcceptsLastRun_ = 0;
  residualReplayCascadeSeedCountLastRun_ = 0;
  residualReplayExpandedCascadeSeedsLastRun_ = 0;
  residualReplayCascadeProbesLastRun_ = 0;
  residualReplayCascadeAcceptsLastRun_ = 0;
  residualReplayRollbackRejectsLastRun_ = 0;
  residualReplayLowConversionRejectsLastRun_ = 0;
  residualReplayFailuresLastRun_ = 0;
  residualReplayEdgesScoredLastRun_ = 0;
  residualReplayAcceptedDeltaLastRun_ = 0;

  for (const int segId : mgrPtr_->getHotSegments()) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments() || is_hot[segId] != 0) {
      continue;
    }
    is_hot[segId] = 1;
    prioritizedSegmentsLastPass_++;
    segment_order.push_back(segId);
  }
  for (int segId = 0; segId < mgrPtr_->getNumSegments(); segId++) {
    if (is_hot[segId] == 0) {
      segment_order.push_back(segId);
    }
  }

  const int hot_window = 4;
  for (const int segId : segment_order) {
    if (is_hot[segId] != 0) {
      visitedHotSegmentsLastPass_++;
    }
    reorderSegment(segId,
                   is_hot[segId] != 0 ? hot_window : windowSize_,
                   false);
  }
  runHotPolishPasses();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<int> DetailedReorderer::expandHotFrontier(
    const std::vector<int>& seed_segments,
    const int max_segments)
{
  if (seed_segments.empty() || max_segments <= 0) {
    return {};
  }

  std::vector<int> frontier;
  std::vector<uint8_t> seen(mgrPtr_->getNumSegments(), 0);
  auto add_segment = [&](const int segId) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments() || seen[segId] != 0
        || static_cast<int>(frontier.size()) >= max_segments) {
      return;
    }
    seen[segId] = 1;
    frontier.push_back(segId);
  };

  for (const int segId : seed_segments) {
    add_segment(segId);
  }
  for (const int segId : seed_segments) {
    if (static_cast<int>(frontier.size()) >= max_segments) {
      break;
    }
    DetailedSeg* seg = mgrPtr_->getSegment(segId);
    const auto& row_segments = mgrPtr_->getSegsInRow(seg->getRowId());
    const auto it = std::find(row_segments.begin(), row_segments.end(), seg);
    if (it == row_segments.end()) {
      continue;
    }
    const int idx = static_cast<int>(std::distance(row_segments.begin(), it));
    if (idx > 0) {
      add_segment(row_segments[idx - 1]->getSegId());
    }
    if (idx + 1 < static_cast<int>(row_segments.size())) {
      add_segment(row_segments[idx + 1]->getSegId());
    }
  }
  return frontier;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runHotPolishPasses()
{
  const auto& hot_segments = mgrPtr_->getHotSegments();
  hotSeedSegmentsLastPass_ = static_cast<int>(hot_segments.size());
  if (hot_segments.empty()) {
    return;
  }

  constexpr int kMaxHotSegments = 384;
  constexpr int kMaxHotPolishPasses = 3;
  std::vector<int> frontier = expandHotFrontier(hot_segments, kMaxHotSegments);
  hotExpandedSegmentsLastPass_ = static_cast<int>(frontier.size());
  hotReplayFrontierLastPass_ = hotExpandedSegmentsLastPass_;
  if (frontier.empty()) {
    return;
  }

  uint64_t hpwl_x = 0;
  uint64_t hpwl_y = 0;
  std::vector<int> all_accepted_segments;
  std::vector<uint8_t> all_accepted_mask(mgrPtr_->getNumSegments(), 0);
  for (int pass = 1; pass <= kMaxHotPolishPasses; pass++) {
    const int64_t before = Utility::hpwl(network_, hpwl_x, hpwl_y);
    const int accepted_before = hotWindowsAcceptedLastRun_;
    const int replay_failures_before = hotReplayFailuresLastRun_;
    const int exact_windows_before = hotExactWindowsScoredLastRun_;
    const int exact_edges_before = hotExactEdgesScoredLastRun_;
    std::vector<int> accepted_segments;
    std::vector<uint8_t> accepted_mask(mgrPtr_->getNumSegments(), 0);
    for (const int segId : frontier) {
      if (!reorderSegment(segId, hotWindowSizeLastPass_, true)) {
        continue;
      }
      if (accepted_mask[segId] == 0) {
        accepted_mask[segId] = 1;
        accepted_segments.push_back(segId);
      }
      if (all_accepted_mask[segId] == 0) {
        all_accepted_mask[segId] = 1;
        all_accepted_segments.push_back(segId);
      }
    }
    const int64_t after = Utility::hpwl(network_, hpwl_x, hpwl_y);
    const int64_t delta = before - after;
    hotPolishPassesLastRun_++;

    std::vector<int> next_frontier
        = expandHotFrontier(accepted_segments, kMaxHotSegments);
    hotReplayFrontierLastPass_ = static_cast<int>(next_frontier.size());

    mgrPtr_->getLogger()->info(
        DPL,
        932,
        "Hot-only reorder pass {}: segments={}, replay_frontier={}, "
        "window={}, hpwl_delta={}, exact_windows_scored={}, "
        "exact_edges_scored={}, accepted_windows={}, "
        "accepted_segments={}, rollback_failures={}.",
        pass,
        frontier.size(),
        hotReplayFrontierLastPass_,
        hotWindowSizeLastPass_,
        delta,
        hotExactWindowsScoredLastRun_ - exact_windows_before,
        hotExactEdgesScoredLastRun_ - exact_edges_before,
        hotWindowsAcceptedLastRun_ - accepted_before,
        accepted_segments.size(),
        hotReplayFailuresLastRun_ - replay_failures_before);

    if (delta <= 0 || accepted_segments.empty()) {
      break;
    }
    frontier = std::move(next_frontier);
    if (frontier.empty()) {
      break;
    }
  }

  if (!all_accepted_segments.empty()) {
    runHotMicroPackPasses(all_accepted_segments);
  }
  runAcceptedWindowResidualReplay();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runHotMicroPackPasses(
    const std::vector<int>& seed_segments)
{
  constexpr int kMicroPackWindow = 5;
  constexpr int kMaxMicroPackSegments = 96;
  constexpr int kMaxMicroPackPasses = 2;
  constexpr int kMaxWindowsPerSegment = 2;
  constexpr int kMaxShiftCandidates = 5;

  std::vector<int> frontier
      = expandHotFrontier(seed_segments, kMaxMicroPackSegments);
  std::stable_sort(
      frontier.begin(),
      frontier.end(),
      [&](const int lhs, const int rhs) {
        const int lhs_weight = mgrPtr_->getSourceEdgeHotSegmentWeight(lhs);
        const int rhs_weight = mgrPtr_->getSourceEdgeHotSegmentWeight(rhs);
        if (lhs_weight != rhs_weight) {
          return lhs_weight > rhs_weight;
        }
        return mgrPtr_->getNumCellsInSeg(lhs) > mgrPtr_->getNumCellsInSeg(rhs);
      });
  hotMicroPackFrontierLastPass_ = static_cast<int>(frontier.size());
  if (frontier.empty()) {
    return;
  }

  uint64_t hpwl_x = 0;
  uint64_t hpwl_y = 0;
  for (int pass = 1; pass <= kMaxMicroPackPasses; pass++) {
    const int64_t before = Utility::hpwl(network_, hpwl_x, hpwl_y);
    const int windows_before = hotMicroPackWindowsLastRun_;
    const int probes_before = hotMicroPackProbesLastRun_;
    const int accepts_before = hotMicroPackAcceptsLastRun_;
    const int rollback_failures_before = hotMicroPackReplayFailuresLastRun_;
    const int edges_before = hotMicroPackEdgesScoredLastRun_;
    const int64_t accepted_delta_before = hotMicroPackAcceptedDeltaLastRun_;
    const int endpoint_windows_before = hotMicroPackEndpointWindowsLastRun_;
    const int boundary_source_edge_windows_before
        = hotMicroPackBoundarySourceEdgeWindowsLastRun_;
    const int source_edge_windows_before = hotMicroPackSourceEdgeWindowsLastRun_;

    std::vector<int> accepted_segments;
    std::vector<uint8_t> accepted_mask(mgrPtr_->getNumSegments(), 0);
    for (const int segId : frontier) {
      if (!microPackSegment(
              segId, kMicroPackWindow, kMaxWindowsPerSegment, kMaxShiftCandidates)) {
        continue;
      }
      if (accepted_mask[segId] == 0) {
        accepted_mask[segId] = 1;
        accepted_segments.push_back(segId);
      }
    }

    const int64_t after = Utility::hpwl(network_, hpwl_x, hpwl_y);
    const int64_t delta = before - after;
    hotMicroPackPassesLastRun_++;

    std::vector<int> next_frontier
        = expandHotFrontier(accepted_segments, kMaxMicroPackSegments);
    std::stable_sort(
        next_frontier.begin(),
        next_frontier.end(),
        [&](const int lhs, const int rhs) {
          const int lhs_weight = mgrPtr_->getSourceEdgeHotSegmentWeight(lhs);
          const int rhs_weight = mgrPtr_->getSourceEdgeHotSegmentWeight(rhs);
          if (lhs_weight != rhs_weight) {
            return lhs_weight > rhs_weight;
          }
          return mgrPtr_->getNumCellsInSeg(lhs) > mgrPtr_->getNumCellsInSeg(rhs);
        });
    hotMicroPackFrontierLastPass_ = static_cast<int>(next_frontier.size());

    mgrPtr_->getLogger()->info(
        DPL,
        934,
        "Hot micro-pack pass {}: segments={}, replay_frontier={}, "
        "window={}, hpwl_delta={}, exact_windows_scored={}, "
        "endpoint_windows={}, boundary_source_edge_windows={}, "
        "source_edge_windows={}, "
        "exact_probes={}, exact_edges_scored={}, accepted_segments={}, "
        "rollback_failures={}, accepted_delta={}.",
        pass,
        frontier.size(),
        hotMicroPackFrontierLastPass_,
        kMicroPackWindow,
        delta,
        hotMicroPackWindowsLastRun_ - windows_before,
        hotMicroPackEndpointWindowsLastRun_ - endpoint_windows_before,
        hotMicroPackBoundarySourceEdgeWindowsLastRun_
            - boundary_source_edge_windows_before,
        hotMicroPackSourceEdgeWindowsLastRun_ - source_edge_windows_before,
        hotMicroPackProbesLastRun_ - probes_before,
        hotMicroPackEdgesScoredLastRun_ - edges_before,
        hotMicroPackAcceptsLastRun_ - accepts_before,
        hotMicroPackReplayFailuresLastRun_ - rollback_failures_before,
        hotMicroPackAcceptedDeltaLastRun_ - accepted_delta_before);

    if (delta <= 0 || accepted_segments.empty() || next_frontier.empty()) {
      break;
    }
    frontier = std::move(next_frontier);
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::reorderSegment(const int segId,
                                       const int windowSize,
                                       const bool collectHotMetrics)
{
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  const int rowId = segPtr->getRowId();
  bool segment_accepted = false;

  const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
  if (nodes.size() < 2) {
    return false;
  }
  mgrPtr_->sortCellsInSeg(segId);

  int j = 0;
  const int n = (int) nodes.size();
  while (j < n) {
    while (j < n && arch_->isMultiHeightCell(nodes[j])) {
      ++j;
    }
    const int jstrt = j;
    while (j < n && arch_->isSingleHeightCell(nodes[j])) {
      ++j;
    }
    const int jstop = j - 1;

    for (int i = jstrt; i + windowSize <= jstop; ++i) {
      int istrt = i;
      const int istop = std::min(jstop, istrt + windowSize - 1);
      if (istop == jstop) {
        istrt = std::max(jstrt, istop - windowSize + 1);
      }

      const Node* nextPtr = (istop != n - 1) ? nodes[istop + 1] : nullptr;
      DbuX rightLimit{segPtr->getMaxX()};
      if (nextPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
        rightLimit = std::min((nextPtr->getLeft() - leftPadding), rightLimit);
      }
      const Node* prevPtr = (istrt != 0) ? nodes[istrt - 1] : nullptr;
      DbuX leftLimit{segPtr->getMinX()};
      if (prevPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
        leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
      }

      bool replay_failed = false;
      double accepted_delta = 0.0;
      if (collectHotMetrics) {
        hotWindowsAttemptedLastRun_++;
      }
      const bool accepted = reorder(nodes,
                                    istrt,
                                    istop,
                                    leftLimit,
                                    rightLimit,
                                    segId,
                                    rowId,
                                    collectHotMetrics ? &replay_failed
                                                      : nullptr,
                                    collectHotMetrics ? &accepted_delta
                                                      : nullptr);
      if (!collectHotMetrics) {
        continue;
      }
      if (accepted) {
        hotWindowsAcceptedLastRun_++;
        segment_accepted = true;
        int source_edge_cells = 0;
        for (int idx = istrt; idx <= istop; idx++) {
          source_edge_cells += static_cast<int>(
              mgrPtr_->isSourceEdgeHotCell(nodes[idx]->getId()));
        }
        noteAcceptedWindowReplaySeed(
            collectWindowNodes(nodes, istrt, istop),
            segId,
            leftLimit,
            rightLimit,
            accepted_delta,
            static_cast<int>(istrt == jstrt) + static_cast<int>(istop == jstop),
            source_edge_cells,
            mgrPtr_->getSourceEdgeHotSegmentWeight(segId),
            false);
      } else {
        hotWindowsRejectedLastRun_++;
        if (replay_failed) {
          hotReplayFailuresLastRun_++;
        }
      }
    }
  }
  return segment_accepted;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::microPackSegment(const int segId,
                                         const int windowSize,
                                         const int maxWindows,
                                         const int maxShiftCandidates)
{
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
  if (nodes.size() < static_cast<size_t>(windowSize)) {
    return false;
  }
  mgrPtr_->sortCellsInSeg(segId);

  std::vector<PackWindowCandidate> top_windows;
  std::vector<PackWindowCandidate> prioritized_windows;
  int j = 0;
  const int n = static_cast<int>(nodes.size());
  while (j < n) {
    while (j < n && arch_->isMultiHeightCell(nodes[j])) {
      ++j;
    }
    const int jstrt = j;
    while (j < n && arch_->isSingleHeightCell(nodes[j])) {
      ++j;
    }
    const int jstop = j - 1;
    if (jstop - jstrt + 1 < windowSize) {
      continue;
    }

    for (int istrt = jstrt; istrt + windowSize - 1 <= jstop; istrt++) {
      const int istop = istrt + windowSize - 1;
      const Node* nextPtr = (istop + 1 < n) ? nodes[istop + 1] : nullptr;
      DbuX rightLimit{segPtr->getMaxX()};
      if (nextPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
        rightLimit = std::min(nextPtr->getLeft() - leftPadding, rightLimit);
      }
      const Node* prevPtr = (istrt > 0) ? nodes[istrt - 1] : nullptr;
      DbuX leftLimit{segPtr->getMinX()};
      if (prevPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
        leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
      }

      PackWindowCandidate candidate;
      candidate.local_score = cost(nodes, istrt, istop);
      candidate.start = istrt;
      candidate.stop = istop;
      candidate.leftLimit = leftLimit;
      candidate.rightLimit = rightLimit;
      candidate.endpoint_cells = static_cast<int>(istrt == jstrt)
                                 + static_cast<int>(istop == jstop);
      candidate.source_edge_segment_weight
          = mgrPtr_->getSourceEdgeHotSegmentWeight(segId);

      std::unordered_set<int> endpoint_edges;
      std::unordered_set<int> source_edge_edges;
      std::unordered_set<int> boundary_source_edge_edges;
      for (int offset = 0; offset < windowSize; offset++) {
        Node* ndi = nodes[istrt + offset];
        const bool source_edge_cell = mgrPtr_->isSourceEdgeHotCell(ndi->getId());
        if (source_edge_cell) {
          candidate.source_edge_cells++;
        }
        const bool endpoint_cell = (offset == 0 || offset == windowSize - 1);
        if (!endpoint_cell && !source_edge_cell) {
          continue;
        }
        for (int pin_idx = 0; pin_idx < ndi->getNumPins(); pin_idx++) {
          const Pin* pin = ndi->getPins()[pin_idx];
          const Edge* edge = pin->getEdge();
          if (edge == nullptr) {
            continue;
          }
          const int npins = edge->getNumPins();
          if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
            continue;
          }
          const uint64_t edge_hpwl = activeHpwlObj_ != nullptr
                                         ? activeHpwlObj_->edgeHpwl(edge)
                                         : edge->hpwl();
          if (endpoint_cell && endpoint_edges.insert(edge->getId()).second) {
            candidate.endpoint_hpwl += edge_hpwl;
          }
          if (source_edge_cell
              && source_edge_edges.insert(edge->getId()).second) {
            candidate.source_edge_hpwl += edge_hpwl;
          }
          if (endpoint_cell && source_edge_cell
              && boundary_source_edge_edges.insert(edge->getId()).second) {
            candidate.boundary_source_edge_hpwl += edge_hpwl;
          }
        }
        if (endpoint_cell && source_edge_cell) {
          candidate.boundary_source_edge_cells++;
        }
      }

      const int capped_segment_weight
          = std::min(candidate.source_edge_segment_weight, 4);
      candidate.score = candidate.local_score
                        + 0.04
                              * static_cast<double>(
                                  candidate.boundary_source_edge_hpwl)
                        + 0.015 * static_cast<double>(candidate.endpoint_hpwl)
                        + 0.008
                              * static_cast<double>(candidate.source_edge_hpwl)
                        + 650.0 * candidate.boundary_source_edge_cells
                        + 180.0 * candidate.source_edge_cells
                        + 120.0 * candidate.endpoint_cells
                        + 40.0 * capped_segment_weight;

      insertTopWindow(top_windows, candidate, maxWindows);
      if (candidate.boundary_source_edge_cells > 0) {
        insertTopWindow(prioritized_windows,
                        candidate,
                        std::min(maxWindows, 2));
      }
    }
  }

  if (top_windows.empty()) {
    return false;
  }

  std::vector<PackWindowCandidate> windows_to_evaluate;
  windows_to_evaluate.reserve(maxWindows);
  auto append_unique_window = [&](const PackWindowCandidate& candidate) {
    const auto dup_it = std::find_if(
        windows_to_evaluate.begin(),
        windows_to_evaluate.end(),
        [&](const PackWindowCandidate& existing) {
          return existing.start == candidate.start && existing.stop == candidate.stop;
        });
    if (dup_it == windows_to_evaluate.end()) {
      windows_to_evaluate.push_back(candidate);
    }
  };

  const int reserved_priority_windows
      = prioritized_windows.empty()
            ? 0
            : std::min(maxWindows,
                       1 + static_cast<int>(
                               mgrPtr_->getSourceEdgeHotSegmentWeight(segId) >= 2));
  for (int i = 0;
       i < reserved_priority_windows
       && i < static_cast<int>(prioritized_windows.size());
       i++) {
    append_unique_window(prioritized_windows[i]);
  }
  for (const PackWindowCandidate& candidate : top_windows) {
    if (static_cast<int>(windows_to_evaluate.size()) >= maxWindows) {
      break;
    }
    append_unique_window(candidate);
  }
  if (windows_to_evaluate.empty()) {
    windows_to_evaluate = top_windows;
  }

  const DbuX siteWidth = arch_->getRow(segPtr->getRowId())->getSiteWidth();
  const std::vector<int> window_segments{segId};
  Journal segment_best(mgrPtr_->getGrid(), mgrPtr_);
  double segment_best_delta = 0.0;
  PackWindowCandidate best_seed_window;
  bool have_best_seed_window = false;

  for (const PackWindowCandidate& window : windows_to_evaluate) {
    hotMicroPackWindowsLastRun_++;
    if (window.endpoint_cells > 0) {
      hotMicroPackEndpointWindowsLastRun_++;
    }
    if (window.boundary_source_edge_cells > 0) {
      hotMicroPackBoundarySourceEdgeWindowsLastRun_++;
    }
    if (window.source_edge_cells > 0 || window.source_edge_segment_weight > 0) {
      hotMicroPackSourceEdgeWindowsLastRun_++;
    }
    const int size = window.stop - window.start + 1;

    std::vector<DbuX> orig_left(size, DbuX{0});
    std::vector<DbuX> left_padding(size, DbuX{0});
    std::vector<DbuX> right_padding(size, DbuX{0});
    std::vector<DbuX> widths(size, DbuX{0});
    DbuX packed_width{0};
    for (int i = 0; i < size; i++) {
      Node* ndi = nodes[window.start + i];
      orig_left[i] = ndi->getLeft();
      arch_->getCellPadding(ndi, left_padding[i], right_padding[i]);
      widths[i] = ndi->getWidth();
      packed_width += widths[i] + left_padding[i] + right_padding[i];
    }
    if (window.rightLimit - window.leftLimit < packed_width) {
      restoreWindowPositions(mgrPtr_, nodes, window.start, orig_left);
      continue;
    }

    const int slack_sites
        = ((window.rightLimit - window.leftLimit - packed_width) / siteWidth).v;
    const std::vector<int> shift_candidates
        = buildShiftCandidates(slack_sites, maxShiftCandidates);
    std::vector<int> order(size, 0);
    for (int i = 0; i < size; i++) {
      order[i] = i;
    }

    Journal best_window(mgrPtr_->getGrid(), mgrPtr_);
    double best_window_delta = 0.0;
    do {
      for (const int shift_sites : shift_candidates) {
        DbuX x = window.leftLimit + shift_sites * siteWidth;
        bool failed = false;
        std::vector<Node*> ordered_nodes(size, nullptr);

        for (int slot = 0; slot < size; slot++) {
          const int ix = order[slot];
          Node* ndi = nodes[window.start + ix];
          ordered_nodes[slot] = ndi;
          x += left_padding[ix];
          mgrPtr_->eraseFromGrid(ndi);
          ndi->setLeft(x);
          mgrPtr_->paintInGrid(ndi);
          x += widths[ix];
          x += right_padding[ix];

          const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
          if (dx > mgrPtr_->getMaxDisplacementX()) {
            failed = true;
          }
        }

        if (!failed) {
          DbuX left = window.leftLimit;
          for (Node* ndi : ordered_nodes) {
            DbuX xi = ndi->getLeft();
            if (!mgrPtr_->alignPos(ndi, xi, left, window.rightLimit)) {
              failed = true;
              break;
            }
            mgrPtr_->eraseFromGrid(ndi);
            ndi->setLeft(xi);
            mgrPtr_->paintInGrid(ndi);
            left = ndi->getRight();

            const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
            if (dx > mgrPtr_->getMaxDisplacementX()) {
              failed = true;
              break;
            }
          }
        }

        if (!failed) {
          for (int i = 0; i < size; i++) {
            if (mgrPtr_->hasPlacementViolation(nodes[window.start + i])) {
              failed = true;
              break;
            }
          }
        }

        if (failed) {
          continue;
        }

        Journal candidate(mgrPtr_->getGrid(), mgrPtr_);
        for (int i = 0; i < size; i++) {
          Node* ndi = nodes[window.start + i];
          if (ndi->getLeft() == orig_left[i]) {
            continue;
          }
          candidate.addAction(MoveCellAction(ndi,
                                             orig_left[i],
                                             ndi->getBottom(),
                                             ndi->getLeft(),
                                             ndi->getBottom(),
                                             true,
                                             window_segments,
                                             window_segments));
        }
        if (candidate.empty()) {
          continue;
        }

        hotMicroPackProbesLastRun_++;
        int scored_edges = 0;
        const double curr_delta = activeHpwlObj_->delta(candidate, &scored_edges);
        hotMicroPackEdgesScoredLastRun_ += scored_edges;
        if (curr_delta > best_window_delta) {
          best_window_delta = curr_delta;
          best_window.clear();
          appendJournal(candidate, best_window);
        }
      }
    } while (std::ranges::next_permutation(order).found);

    restoreWindowPositions(mgrPtr_, nodes, window.start, orig_left);
    if (best_window_delta > segment_best_delta && !best_window.empty()) {
      segment_best_delta = best_window_delta;
      segment_best.clear();
      appendJournal(best_window, segment_best);
      best_seed_window = window;
      have_best_seed_window = true;
    }
  }

  if (segment_best.empty() || segment_best_delta <= 0.0) {
    return false;
  }

  segment_best.redo();
  mgrPtr_->sortCellsInSeg(segId);
  activeHpwlObj_->delta(segment_best);
  activeHpwlObj_->accept();
  hotMicroPackAcceptsLastRun_++;
  hotMicroPackAcceptedDeltaLastRun_ += static_cast<int64_t>(segment_best_delta);
  if (have_best_seed_window) {
    noteAcceptedWindowReplaySeed(
        collectWindowNodes(
            nodes, best_seed_window.start, best_seed_window.stop),
        segId,
        best_seed_window.leftLimit,
        best_seed_window.rightLimit,
        segment_best_delta,
        best_seed_window.endpoint_cells,
        best_seed_window.source_edge_cells,
        best_seed_window.source_edge_segment_weight,
        true);
  }
  return true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::reorder(const std::vector<Node*>& nodes,
                                const int jstrt,
                                const int jstop,
                                const DbuX leftLimit,
                                const DbuX rightLimit,
                                const int segId,
                                const int rowId,
                                bool* replayFailed,
                                double* acceptedDelta)
{
  if (replayFailed != nullptr) {
    *replayFailed = false;
  }
  if (acceptedDelta != nullptr) {
    *acceptedDelta = 0.0;
  }
  const bool use_exact_scoring = replayFailed != nullptr && activeHpwlObj_ != nullptr;
  const int size = jstop - jstrt + 1;
  if (size <= 0) {
    return false;
  }
  // XXX: Node positions still doubles!
  std::unordered_map<const Node*, DbuX> origLeft;
  for (int i = 0; i < size; i++) {
    const Node* ndi = nodes[jstrt + i];
    origLeft[ndi] = ndi->getLeft();
  }

  // Changed...  I want to work entirely with the left edge of
  // the cells.  If there is not enough space to satisfy
  // the cell widths _and_ the padding, then don't do anything.
  DbuX totalPadding{0};
  DbuX totalWidth{0};
  std::vector<DbuX> right(size, DbuX{0});
  std::vector<DbuX> left(size, DbuX{0});
  std::vector<DbuX> width(size, DbuX{0});
  for (int i = 0; i < size; i++) {
    const Node* ndi = nodes[jstrt + i];
    arch_->getCellPadding(ndi, left[i], right[i]);
    width[i] = ndi->getWidth();
    totalPadding += (left[i] + right[i]);
    totalWidth += width[i];
  }
  if (rightLimit - leftLimit < totalWidth + totalPadding) {
    // We do not have enough space, so abort.
    return false;
  }

  // We might have more space than required.  Space cells out
  // somewhat evenly by adding extra space to the padding.
  const DbuX spacePerCell
      = ((rightLimit - leftLimit) - (totalWidth + totalPadding)) / size;
  const DbuX siteWidth = arch_->getRow(0)->getSiteWidth();
  const int sitePerCellTotal = (spacePerCell / siteWidth).v;
  const int sitePerCellRight = (sitePerCellTotal >> 1);
  const int sitePerCellLeft = sitePerCellTotal - sitePerCellRight;
  for (int i = 0; i < size; i++) {
    if (totalWidth + totalPadding + sitePerCellRight * siteWidth
        < rightLimit - leftLimit) {
      totalPadding += sitePerCellRight * siteWidth;
      right[i] += sitePerCellRight * siteWidth;
    }
    if (totalWidth + totalPadding + sitePerCellLeft * siteWidth
        < rightLimit - leftLimit) {
      totalPadding += sitePerCellLeft * siteWidth;
      left[i] += sitePerCellLeft * siteWidth;
    }
  }
  if (rightLimit - leftLimit < totalWidth + totalPadding) {
    // We do not have enough space, so abort.
    return false;
  }

  // Generate the different permutations.  Evaluate each one and keep
  // the best one.
  //
  // NOTE: The first permutation, which is the original placement,
  // might not generate the original placement since the spacing
  // might be different.  So, just consider the first permutation
  // like all the others.

  double bestCost = use_exact_scoring ? 0.0 : cost(nodes, jstrt, jstop);
  const double origCost = bestCost;
  double bestExactDelta = 0.0;
  Journal bestJournal(mgrPtr_->getGrid(), mgrPtr_);
  const std::vector<int> window_segments{segId};
  bool exact_window_scored = false;
  int exact_edges_scored = 0;

  std::vector<DbuX> bestPosn(size, DbuX{0});  // Current positions.
  std::vector<DbuX> currPosn(size, DbuX{0});  // Current positions.
  std::vector<int> order(size, 0);            // For generating permutations.
  for (int i = 0; i < size; i++) {
    order[i] = i;
  }
  bool found = false;
  do {
    // Position the cells.
    bool dispOkay = true;
    DbuX x = leftLimit;
    for (int i = 0; i < size; i++) {
      const int ix = order[i];
      Node* ndi = nodes[jstrt + ix];
      x += left[ix];
      currPosn[ix] = x;
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(currPosn[ix]);
      mgrPtr_->paintInGrid(ndi);
      x += width[ix];
      x += right[ix];

      const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
      if (dx > mgrPtr_->getMaxDisplacementX()) {
        dispOkay = false;
      }
    }
    if (dispOkay) {
      if (use_exact_scoring) {
        bool failed = false;
        DbuX left = leftLimit;
        for (int i = 0; i < size; i++) {
          Node* ndi = nodes[jstrt + i];

          DbuX x = ndi->getLeft();
          if (!mgrPtr_->alignPos(ndi, x, left, rightLimit)) {
            failed = true;
            break;
          }
          mgrPtr_->eraseFromGrid(ndi);
          ndi->setLeft(x);
          mgrPtr_->paintInGrid(ndi);
          left = ndi->getRight();

          const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
          if (dx > mgrPtr_->getMaxDisplacementX()) {
            failed = true;
            break;
          }
        }
        if (!failed) {
          for (int i = 0; i < size; i++) {
            if (mgrPtr_->hasPlacementViolation(nodes[jstrt + i])) {
              failed = true;
              break;
            }
          }
        }
        if (!failed) {
          Journal candidate(mgrPtr_->getGrid(), mgrPtr_);
          for (int i = 0; i < size; i++) {
            Node* ndi = nodes[jstrt + i];
            const DbuX orig_left = origLeft[ndi];
            if (ndi->getLeft() == orig_left) {
              continue;
            }
            candidate.addAction(MoveCellAction(ndi,
                                               orig_left,
                                               ndi->getBottom(),
                                               ndi->getLeft(),
                                               ndi->getBottom(),
                                               true,
                                               window_segments,
                                               window_segments));
          }
          if (!candidate.empty()) {
            int scored_edges = 0;
            const double currDelta
                = activeHpwlObj_->delta(candidate, &scored_edges);
            exact_window_scored = true;
            exact_edges_scored += scored_edges;
            if (currDelta > bestExactDelta) {
              bestExactDelta = currDelta;
              bestJournal.clear();
              appendJournal(candidate, bestJournal);
              found = true;
            }
          }
        }
      } else {
        const double currCost = cost(nodes, jstrt, jstop);
        if (currCost < bestCost) {
          bestPosn = currPosn;
          bestCost = currCost;
          found = true;
        }
      }
    }
  } while (std::ranges::next_permutation(order).found);

  if (use_exact_scoring && exact_window_scored) {
    hotExactWindowsScoredLastRun_++;
    hotExactEdgesScoredLastRun_ += exact_edges_scored;
  }

  if (!found) {
    // No improvement.  Restore positions and return.
    for (size_t i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[ndi]);
      mgrPtr_->paintInGrid(ndi);
    }
    return false;
  }

  if (use_exact_scoring) {
    for (int i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[ndi]);
      mgrPtr_->paintInGrid(ndi);
    }
    if (bestJournal.empty() || bestExactDelta <= 0.0) {
      return false;
    }
    bestJournal.redo();
    mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
    activeHpwlObj_->delta(bestJournal);
    activeHpwlObj_->accept();
    hotAcceptedDeltaLastRun_ += static_cast<int64_t>(bestExactDelta);
    if (acceptedDelta != nullptr) {
      *acceptedDelta = bestExactDelta;
    }
    return true;
  }

  // Put cells at their best positions.
  for (int i = 0; i < size; i++) {
    Node* ndi = nodes[jstrt + i];
    mgrPtr_->eraseFromGrid(ndi);
    ndi->setLeft(bestPosn[i]);
    mgrPtr_->paintInGrid(ndi);
  }

  // Need to resort.
  mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);

  // Check that cells are site aligned and fix if needed.
  {
    bool shifted = false;
    bool failed = false;
    DbuX left = leftLimit;
    for (int i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];

      DbuX x = ndi->getLeft();
      if (!mgrPtr_->alignPos(ndi, x, left, rightLimit)) {
        failed = true;
        break;
      }
      if (abs(x - ndi->getLeft()) != 0) {
        shifted = true;
      }
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(x);
      mgrPtr_->paintInGrid(ndi);
      left = ndi->getRight();

      const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
      if (dx > mgrPtr_->getMaxDisplacementX()) {
        failed = true;
        break;
      }
    }
    if (!failed) {
      // This implies everything got site aligned within the specified
      // interval.  However, we might have shifted something.
      if (shifted) {
        // Recost.  The shifting might have changed the cost.
        const double lastCost = cost(nodes, jstrt, jstop);
        if (lastCost >= origCost) {
          failed = true;
        }
      }
    }
    if (!failed) {
      for (int i = 0; i < size; i++) {
        if (mgrPtr_->hasPlacementViolation(nodes[jstrt + i])) {
          failed = true;
          break;
        }
      }
    }

    if (failed) {
      // Restore original placement.
      for (int i = 0; i < size; i++) {
        Node* ndi = nodes[jstrt + i];
        mgrPtr_->eraseFromGrid(ndi);
        ndi->setLeft(origLeft[ndi]);
        mgrPtr_->paintInGrid(ndi);
      }
      mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
      if (replayFailed != nullptr) {
        *replayFailed = true;
      }
      return false;
    }
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedReorderer::cost(const std::vector<Node*>& nodes,
                               const int istrt,
                               const int istop)
{
  // Compute hpwl for the specified sequence of cells.

  ++traversal_;

  double cost = 0.;
  for (int i = istrt; i <= istop; i++) {
    const Node* ndi = nodes[i];
    if (mgrPtr_->hasPlacementViolation(ndi)) {
      return std::numeric_limits<double>::max();
    }

    for (int pi = 0; pi < ndi->getNumPins(); pi++) {
      const Pin* pini = ndi->getPins()[pi];

      const Edge* edi = pini->getEdge();

      const int npins = edi->getNumPins();
      if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
        continue;
      }
      if (edgeMask_[edi->getId()] == traversal_) {
        continue;
      }
      edgeMask_[edi->getId()] = traversal_;

      DbuX xmin = std::numeric_limits<DbuX>::max();
      DbuX xmax = std::numeric_limits<DbuX>::min();
      for (int pj = 0; pj < edi->getNumPins(); pj++) {
        const Pin* pinj = edi->getPins()[pj];

        const Node* ndj = pinj->getNode();

        const DbuX x = ndj->getCenterX() + pinj->getOffsetX();

        xmin = std::min(xmin, x);
        xmax = std::max(xmax, x);
      }
      cost += (xmax - xmin).v;
    }
  }
  return cost;
}

}  // namespace dpl_evolve
