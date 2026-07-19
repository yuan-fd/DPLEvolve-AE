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
#include <vector>

#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "detailed_manager.h"
#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
#include "infrastructure/Objects.h"
#include "infrastructure/architecture.h"
#include "infrastructure/detailed_segment.h"
#include "util/utility.h"
#include "utl/Logger.h"

using utl::DPL;

namespace dpl_evolve {

namespace {

constexpr int kResidualRowOffsets[] = {0, -1, 1};
constexpr int kMaxResidualSegmentsPerCell = 4;
constexpr int kMaxResidualWindowsPerSegment = 6;
constexpr int kMaxResidualAdjacentWindowCandidates = 4;
constexpr int kMaxResidualAdjacentPairsPerCell = 6;
constexpr int kResidualAnchorRadius = 1;
constexpr int kResidualBlockerRadius = 2;
constexpr int kMaxStagedSweepSegments = 512;
constexpr int kMinStagedSweepSegmentsBeforeStop = 64;
constexpr int kMaxStagedSweepNoGainStreak = 64;

uint64_t makeWindowKey(const int segId, const int start, const int stop)
{
  return (static_cast<uint64_t>(static_cast<uint32_t>(segId)) << 32)
         | (static_cast<uint64_t>(static_cast<uint16_t>(start)) << 16)
         | static_cast<uint64_t>(static_cast<uint16_t>(stop));
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

  uint64_t hpwl_x, hpwl_y;
  int64_t curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  const int64_t init_hpwl = curr_hpwl;
  if (init_hpwl == 0) {
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
                               static_cast<double>(curr_hpwl));
    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / static_cast<double>(last_hpwl)
               <= tol) {
      break;
    }
  }
  mgrPtr_->resortSegments();
  const double curr_imp
      = (((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.);
  mgrPtr_->getLogger()->info(
      DPL,
      305,
      "End of reordering; objective is {:.6e}, improvement is {:.2f} percent.",
      static_cast<double>(curr_hpwl),
      curr_imp);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, 0);

  residual_frontier_size_
      = static_cast<int>(mgrPtr_->getResidualHandoffSpecs().size());
  residual_frontier_active_
      = static_cast<int>(std::count_if(mgrPtr_->getResidualHandoffSpecs().begin(),
                                       mgrPtr_->getResidualHandoffSpecs().end(),
                                       [](const DetailedResidualSpec& spec) {
                                         return spec.active;
                                       }));
  residual_prioritized_cells_ = 0;
  residual_targeted_segments_ = 0;
  residual_intervals_built_ = 0;
  residual_exact_probes_ = 0;
  residual_permutation_candidates_ = 0;
  residual_chain_candidates_ = 0;
  residual_accepted_moves_ = 0;
  residual_hpwl_gain_ = 0.0;
  residual_adjacent_clusters_ = 0;
  residual_adjacent_exact_probes_ = 0;
  residual_adjacent_accepts_ = 0;
  residual_adjacent_rejects_ = 0;
  residual_adjacent_rollbacks_ = 0;
  residual_adjacent_hpwl_gain_ = 0.0;
  staged_sweep_segments_selected_ = 0;
  staged_sweep_segments_attempted_ = 0;
  staged_sweep_segments_accepted_ = 0;
  staged_sweep_segments_rejected_ = 0;
  staged_sweep_segments_rolled_back_ = 0;
  staged_sweep_global_rollbacks_ = 0;
  staged_sweep_windows_built_ = 0;
  staged_sweep_exact_probes_ = 0;
  staged_sweep_permutation_candidates_ = 0;
  staged_sweep_hpwl_gain_ = 0.0;
  staged_sweep_early_stop_ = false;

  const bool used_residual = reorderResidualIntervals();
  if (!used_residual) {
    reorderAllSegments();
  }

  mgrPtr_->getLogger()->info(
      DPL,
      350,
      "Residual reorder frontier size {:d}, active {:d}, prioritized {:d}, "
      "targeted segments {:d}, intervals {:d}, exact probes {:d}, "
      "perm candidates {:d}, chain candidates {:d}, accepted {:d}, "
      "adjacent clusters/probes/accepts/rejects/rollbacks {:d}/{:d}/{:d}/{:d}/{:d}, "
      "exact HPWL gain {:.2f}, adjacent gain {:.2f}, "
      "staged sweep selected/attempted/accepted/rejected/rollbacks {:d}/{:d}/{:d}/{:d}/{:d}, "
      "staged sweep windows/probes/perms {:d}/{:d}/{:d}, "
      "staged sweep gain {:.2f}, global rollback {:d}, early stop {:d}.",
      residual_frontier_size_,
      residual_frontier_active_,
      residual_prioritized_cells_,
      residual_targeted_segments_,
      residual_intervals_built_,
      residual_exact_probes_,
      residual_permutation_candidates_,
      residual_chain_candidates_,
      residual_accepted_moves_,
      residual_adjacent_clusters_,
      residual_adjacent_exact_probes_,
      residual_adjacent_accepts_,
      residual_adjacent_rejects_,
      residual_adjacent_rollbacks_,
      residual_hpwl_gain_,
      residual_adjacent_hpwl_gain_,
      staged_sweep_segments_selected_,
      staged_sweep_segments_attempted_,
      staged_sweep_segments_accepted_,
      staged_sweep_segments_rejected_,
      staged_sweep_segments_rolled_back_,
      staged_sweep_windows_built_,
      staged_sweep_exact_probes_,
      staged_sweep_permutation_candidates_,
      staged_sweep_hpwl_gain_,
      staged_sweep_global_rollbacks_,
      staged_sweep_early_stop_ ? 1 : 0);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_frontier_size",
                               residual_frontier_size_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_frontier_active",
                               residual_frontier_active_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_prioritized_cells",
      residual_prioritized_cells_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_targeted_segments",
                               residual_targeted_segments_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_intervals_built",
                               residual_intervals_built_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_exact_probes",
                               residual_exact_probes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_permutation_candidates",
      residual_permutation_candidates_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_chain_candidates",
                               residual_chain_candidates_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_accepted_moves",
                               residual_accepted_moves_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_adjacent_clusters",
      residual_adjacent_clusters_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_adjacent_exact_probes",
      residual_adjacent_exact_probes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_adjacent_accepts",
      residual_adjacent_accepts_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_adjacent_rejects",
      residual_adjacent_rejects_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_adjacent_rollbacks",
      residual_adjacent_rollbacks_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_segments_selected",
      staged_sweep_segments_selected_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_segments_attempted",
      staged_sweep_segments_attempted_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_segments_accepted",
      staged_sweep_segments_accepted_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_segments_rejected",
      staged_sweep_segments_rejected_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_segments_rolled_back",
      staged_sweep_segments_rolled_back_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_global_rollbacks",
      staged_sweep_global_rollbacks_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__staged_sweep_windows_built",
                               staged_sweep_windows_built_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__staged_sweep_exact_probes",
                               staged_sweep_exact_probes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__staged_sweep_permutation_candidates",
      staged_sweep_permutation_candidates_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__staged_sweep_early_stop",
                               staged_sweep_early_stop_ ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<int> DetailedReorderer::buildStagedSweepSegments() const
{
  std::vector<int> segments;
  segments.reserve(std::min(mgrPtr_->getNumSegments(), kMaxStagedSweepSegments));
  std::vector<uint8_t> seen(mgrPtr_->getNumSegments(), 0);

  auto add_segment = [&](const int segId) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments() || seen[segId] != 0) {
      return;
    }
    seen[segId] = 1;
    segments.push_back(segId);
  };

  for (const auto& residual : mgrPtr_->getResidualHandoffSpecs()) {
    if (!residual.active) {
      continue;
    }
    add_segment(residual.current_seg_id);
    add_segment(residual.target_seg_id);
  }

  for (int segId = 0;
       segId < mgrPtr_->getNumSegments()
       && static_cast<int>(segments.size()) < kMaxStagedSweepSegments;
       ++segId) {
    add_segment(segId);
  }

  return segments;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorderAllSegments()
{
  const std::vector<int> segments = buildStagedSweepSegments();
  staged_sweep_segments_selected_ = static_cast<int>(segments.size());
  std::vector<ResidualWindowSnapshot> sweep_snapshots;
  sweep_snapshots.reserve(segments.size());
  for (const int segment_id : segments) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(segment_id);
    if (segPtr == nullptr) {
      continue;
    }
    const int segId = segPtr->getSegId();
    mgrPtr_->sortCellsInSeg(segId);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.empty()) {
      continue;
    }
    sweep_snapshots.push_back(
        captureResidualWindowSnapshot(nodes, segId, 0, nodes.size() - 1));
  }
  const auto sweep_edges = collectAffectedEdgesForSnapshotSet(sweep_snapshots);
  const double sweep_base_cost = sweep_edges.empty() ? 0.0 : cost(sweep_edges);
  int no_gain_streak = 0;
  for (const int segment_id : segments) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(segment_id);
    if (segPtr == nullptr) {
      continue;
    }
    const int segId = segPtr->getSegId();
    const int rowId = segPtr->getRowId();

    ++staged_sweep_segments_attempted_;
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      ++staged_sweep_segments_rejected_;
      ++no_gain_streak;
      continue;
    }
    mgrPtr_->sortCellsInSeg(segId);
    const auto snapshot
        = captureResidualWindowSnapshot(nodes, segId, 0, nodes.size() - 1);
    const auto affectedEdges
        = collectAffectedEdgesForSnapshotSet(std::vector<ResidualWindowSnapshot>{snapshot});
    if (affectedEdges.empty()) {
      ++staged_sweep_segments_rejected_;
      ++no_gain_streak;
      continue;
    }
    const double baseCost = cost(affectedEdges);

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

      for (int i = jstrt; i + windowSize_ <= jstop; ++i) {
        int istrt = i;
        const int istop = std::min(jstop, istrt + windowSize_ - 1);
        if (istop == jstop) {
          istrt = std::max(jstrt, istop - windowSize_ + 1);
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

        ++staged_sweep_windows_built_;
        reorder(nodes,
                istrt,
                istop,
                leftLimit,
                rightLimit,
                segId,
                rowId,
                leftLimit,
                -1,
                false,
                true,
                TelemetryMode::Sweep);
      }
    }

    if (!isResidualWindowChanged(snapshot)) {
      ++staged_sweep_segments_rejected_;
      ++no_gain_streak;
    } else {
      const double currCost = cost(affectedEdges);
      if (currCost + 1e-3 < baseCost) {
        ++staged_sweep_segments_accepted_;
        staged_sweep_hpwl_gain_ += (baseCost - currCost);
        no_gain_streak = 0;
      } else {
        restoreResidualWindowSnapshot(snapshot);
        ++staged_sweep_segments_rejected_;
        ++staged_sweep_segments_rolled_back_;
        ++no_gain_streak;
      }
    }

    if (staged_sweep_segments_attempted_ >= kMinStagedSweepSegmentsBeforeStop
        && no_gain_streak >= kMaxStagedSweepNoGainStreak) {
      staged_sweep_early_stop_ = true;
      break;
    }
  }

  if (!sweep_edges.empty()) {
    const double sweep_post_cost = cost(sweep_edges);
    if (sweep_post_cost + 1e-3 >= sweep_base_cost) {
      ++staged_sweep_global_rollbacks_;
      staged_sweep_hpwl_gain_ = 0.0;
      for (const auto& snapshot : sweep_snapshots) {
        restoreResidualWindowSnapshot(snapshot);
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::reorderResidualIntervals()
{
  if (residual_frontier_active_ == 0) {
    return false;
  }

  std::unordered_set<uint64_t> seen_windows;
  bool built = false;
  for (const auto& residual : mgrPtr_->getResidualHandoffSpecs()) {
    if (!residual.active || residual.node == nullptr) {
      continue;
    }
    ++residual_prioritized_cells_;
    built |= reorderResidualInterval(residual, seen_windows);
  }
  return built;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::reorderResidualInterval(
    const DetailedResidualSpec& residual,
    std::unordered_set<uint64_t>& seen_windows)
{
  const DbuX siteWidth = mgrPtr_->getGrid()->getSiteWidth();
  const DbuX targetLeft
      = gridToDbu(GridX{std::max(0, residual.target_site)}, siteWidth);
  const int groupId = residual.node->getGroupId();

  std::vector<int> segment_ids;
  segment_ids.reserve(kMaxResidualSegmentsPerCell);
  auto add_segment = [&](const int segId) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
      return;
    }
    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    if (segPtr == nullptr || segPtr->getRegId() != groupId) {
      return;
    }
    if (std::find(segment_ids.begin(), segment_ids.end(), segId)
        == segment_ids.end()) {
      segment_ids.push_back(segId);
    }
  };

  add_segment(residual.current_seg_id);
  add_segment(residual.target_seg_id);
  for (const int row_offset : kResidualRowOffsets) {
    const int rowId = residual.target_row + row_offset;
    if (rowId < 0 || rowId >= mgrPtr_->getNumSingleHeightRows()) {
      continue;
    }
    for (DetailedSeg* segPtr : mgrPtr_->getSegsInRow(rowId)) {
      if (segPtr == nullptr || segPtr->getRegId() != groupId) {
        continue;
      }
      if (targetLeft < segPtr->getMinX() || targetLeft > segPtr->getMaxX()) {
        continue;
      }
      add_segment(segPtr->getSegId());
      break;
    }
  }

  if (segment_ids.empty()) {
    return false;
  }
  if (static_cast<int>(segment_ids.size()) > kMaxResidualSegmentsPerCell) {
    segment_ids.resize(kMaxResidualSegmentsPerCell);
  }

  bool built = false;
  for (const int segId : segment_ids) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    if (segPtr == nullptr) {
      continue;
    }

    mgrPtr_->sortCellsInSeg(segId);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      continue;
    }

    const Node* anchorNode
        = segId == residual.current_seg_id ? residual.node : nullptr;
    const int anchorIndex = findAnchorIndex(nodes, anchorNode, targetLeft);
    if (anchorIndex < 0) {
      continue;
    }

    ++residual_targeted_segments_;
    reorderSegmentWindows(nodes,
                          segId,
                          segPtr->getRowId(),
                          anchorIndex,
                          targetLeft,
                          true,
                          seen_windows,
                          windowSize_ + 1,
                          kResidualAnchorRadius);
    if (segId == residual.current_seg_id) {
      const int blockerIndex = findAnchorIndex(nodes, nullptr, targetLeft);
      if (blockerIndex >= 0 && blockerIndex != anchorIndex) {
        reorderSegmentWindows(nodes,
                              segId,
                              segPtr->getRowId(),
                              blockerIndex,
                              targetLeft,
                              true,
                              seen_windows,
                              windowSize_ + 1,
                              kResidualBlockerRadius);
      }
    }
    built = true;
  }

  if (built) {
    built |= reorderResidualAdjacentChains(residual, segment_ids);
  }

  return built;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::reorderResidualAdjacentChains(
    const DetailedResidualSpec& residual,
    const std::vector<int>& segment_ids)
{
  struct SegmentWindowList
  {
    int segId = -1;
    int rowId = -1;
    std::vector<ResidualWindowCandidate> windows;
  };

  std::vector<SegmentWindowList> segment_windows;
  segment_windows.reserve(segment_ids.size());
  const DbuX targetLeft
      = gridToDbu(GridX{std::max(0, residual.target_site)},
                  mgrPtr_->getGrid()->getSiteWidth());

  for (const int segId : segment_ids) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    if (segPtr == nullptr) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(segId);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      continue;
    }

    const Node* anchorNode
        = segId == residual.current_seg_id ? residual.node : nullptr;
    const int anchorIndex = findAnchorIndex(nodes, anchorNode, targetLeft);
    if (anchorIndex < 0) {
      continue;
    }

    segment_windows.push_back({segId, segPtr->getRowId(), {}});
    buildResidualWindowCandidates(nodes,
                                  segId,
                                  segPtr->getRowId(),
                                  anchorIndex,
                                  targetLeft,
                                  windowSize_ + 1,
                                  segId == residual.current_seg_id
                                      ? kResidualBlockerRadius
                                      : kResidualAnchorRadius,
                                  kMaxResidualAdjacentWindowCandidates,
                                  segment_windows.back().windows);
  }

  if (segment_windows.size() < 2) {
    return false;
  }

  std::vector<ResidualWindowSnapshot> baseline_snapshots;
  baseline_snapshots.reserve(segment_windows.size());
  for (const auto& segment_window : segment_windows) {
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segment_window.segId);
    if (nodes.empty()) {
      continue;
    }
    baseline_snapshots.push_back(
        captureResidualWindowSnapshot(
            nodes, segment_window.segId, 0, static_cast<int>(nodes.size()) - 1));
  }
  const auto baseline_edges = collectAffectedEdgesForSnapshotSet(baseline_snapshots);
  const double baseline_cost
      = baseline_edges.empty() ? 0.0 : cost(baseline_edges);

  bool tried = false;
  int pair_budget = 0;
  int local_adjacent_accepts = 0;
  int local_total_accepts = 0;
  double local_adjacent_gain = 0.0;
  double local_total_gain = 0.0;
  for (int i = 0; i < static_cast<int>(segment_windows.size()); ++i) {
    for (int j = i + 1; j < static_cast<int>(segment_windows.size()); ++j) {
      if (pair_budget >= kMaxResidualAdjacentPairsPerCell) {
        return tried;
      }
      if (segment_windows[i].rowId == segment_windows[j].rowId) {
        continue;
      }
      if (segment_windows[i].windows.empty() || segment_windows[j].windows.empty()) {
        continue;
      }

      for (const auto& first : segment_windows[i].windows) {
        for (const auto& second : segment_windows[j].windows) {
          if (pair_budget >= kMaxResidualAdjacentPairsPerCell) {
            return tried;
          }
          ++pair_budget;
          ++residual_adjacent_clusters_;
          tried = true;

          DetailedSeg* firstSeg = mgrPtr_->getSegment(first.segId);
          DetailedSeg* secondSeg = mgrPtr_->getSegment(second.segId);
          if (firstSeg == nullptr || secondSeg == nullptr) {
            ++residual_adjacent_rejects_;
            continue;
          }

          const std::vector<Node*>& firstNodes = mgrPtr_->getCellsInSeg(first.segId);
          const std::vector<Node*>& secondNodes = mgrPtr_->getCellsInSeg(second.segId);
          auto firstSnapshot
              = captureResidualWindowSnapshot(firstNodes,
                                              first.segId,
                                              first.start,
                                              first.stop);
          auto secondSnapshot
              = captureResidualWindowSnapshot(secondNodes,
                                              second.segId,
                                              second.start,
                                              second.stop);
          if (firstSnapshot.nodes.empty() || secondSnapshot.nodes.empty()) {
            ++residual_adjacent_rejects_;
            continue;
          }

          const auto affectedEdges
              = collectAffectedEdgesForSnapshots(firstSnapshot, &secondSnapshot);
          if (affectedEdges.empty()) {
            ++residual_adjacent_rejects_;
            continue;
          }
          const double baseCost = cost(affectedEdges);

          reorder(firstNodes,
                  first.start,
                  first.stop,
                  first.leftLimit,
                  first.rightLimit,
                  first.segId,
                  first.rowId,
                  targetLeft,
                  first.anchorOffset,
                  true,
                  false,
                  TelemetryMode::Residual);
          reorder(secondNodes,
                  second.start,
                  second.stop,
                  second.leftLimit,
                  second.rightLimit,
                  second.segId,
                  second.rowId,
                  targetLeft,
                  second.anchorOffset,
                  true,
                  false,
                  TelemetryMode::Residual);

          if (!isResidualWindowChanged(firstSnapshot)
              && !isResidualWindowChanged(secondSnapshot)) {
            restoreResidualWindowSnapshot(secondSnapshot);
            restoreResidualWindowSnapshot(firstSnapshot);
            ++residual_adjacent_rejects_;
            continue;
          }

          ++residual_adjacent_exact_probes_;
          const double currCost = cost(affectedEdges);
          if (currCost + 1e-3 < baseCost) {
            ++residual_adjacent_accepts_;
            ++local_adjacent_accepts;
            local_adjacent_gain += (baseCost - currCost);
            residual_accepted_moves_ += 2;
            local_total_accepts += 2;
            local_total_gain += (baseCost - currCost);
          } else {
            restoreResidualWindowSnapshot(secondSnapshot);
            restoreResidualWindowSnapshot(firstSnapshot);
            ++residual_adjacent_rejects_;
          }
        }
      }
    }
  }

  if (!baseline_edges.empty() && local_adjacent_accepts > 0) {
    const double post_chain_cost = cost(baseline_edges);
    if (post_chain_cost + 1e-3 >= baseline_cost) {
      ++residual_adjacent_rollbacks_;
      residual_adjacent_accepts_ -= local_adjacent_accepts;
      residual_accepted_moves_ -= local_total_accepts;
      for (const auto& snapshot : baseline_snapshots) {
        restoreResidualWindowSnapshot(snapshot);
      }
    } else {
      residual_adjacent_hpwl_gain_ += local_adjacent_gain;
      residual_hpwl_gain_ += local_total_gain;
    }
  } else {
    residual_adjacent_hpwl_gain_ += local_adjacent_gain;
    residual_hpwl_gain_ += local_total_gain;
  }

  return tried;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::buildResidualWindowCandidates(
    const std::vector<Node*>& nodes,
    const int segId,
    const int rowId,
    const int anchorIndex,
    const DbuX targetLeft,
    const int maxWindow,
    const int extraRadius,
    const int maxCandidates,
    std::vector<ResidualWindowCandidate>& candidates)
{
  if (anchorIndex < 0 || anchorIndex >= static_cast<int>(nodes.size())
      || !isSingleHeightMovable(nodes[anchorIndex])) {
    return;
  }

  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  if (segPtr == nullptr) {
    return;
  }

  int runStart = anchorIndex;
  while (runStart > 0 && isSingleHeightMovable(nodes[runStart - 1])) {
    --runStart;
  }
  int runStop = anchorIndex;
  while (runStop + 1 < static_cast<int>(nodes.size())
         && isSingleHeightMovable(nodes[runStop + 1])) {
    ++runStop;
  }
  if (runStop - runStart + 1 < 2) {
    return;
  }

  const int cappedWindow = std::min(maxWindow, runStop - runStart + 1);
  for (int size = 2;
       size <= cappedWindow && static_cast<int>(candidates.size()) < maxCandidates;
       ++size) {
    const int startMin
        = std::max(runStart, anchorIndex - size + 1 - extraRadius);
    const int startMax
        = std::min(anchorIndex + extraRadius, runStop - size + 1);
    for (int start = startMin;
         start <= startMax && static_cast<int>(candidates.size()) < maxCandidates;
         ++start) {
      const int stop = start + size - 1;
      const Node* nextPtr = (stop != static_cast<int>(nodes.size()) - 1)
                                ? nodes[stop + 1]
                                : nullptr;
      DbuX rightLimit{segPtr->getMaxX()};
      if (nextPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
        rightLimit = std::min((nextPtr->getLeft() - leftPadding), rightLimit);
      }
      const Node* prevPtr = (start != 0) ? nodes[start - 1] : nullptr;
      DbuX leftLimit{segPtr->getMinX()};
      if (prevPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
        leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
      }

      candidates.push_back({segId,
                            rowId,
                            start,
                            stop,
                            anchorIndex - start,
                            leftLimit,
                            rightLimit});
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorderSegmentWindows(
    const std::vector<Node*>& nodes,
    const int segId,
    const int rowId,
    const int anchorIndex,
    const DbuX targetLeft,
    const bool residualGuided,
    std::unordered_set<uint64_t>& seen_windows,
    const int maxWindow,
    const int extraRadius)
{
  if (anchorIndex < 0 || anchorIndex >= static_cast<int>(nodes.size())
      || !isSingleHeightMovable(nodes[anchorIndex])) {
    return;
  }

  int runStart = anchorIndex;
  while (runStart > 0 && isSingleHeightMovable(nodes[runStart - 1])) {
    --runStart;
  }
  int runStop = anchorIndex;
  while (runStop + 1 < static_cast<int>(nodes.size())
         && isSingleHeightMovable(nodes[runStop + 1])) {
    ++runStop;
  }
  if (runStop - runStart + 1 < 2) {
    return;
  }

  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  if (segPtr == nullptr) {
    return;
  }

  const int cappedWindow = std::min(maxWindow, runStop - runStart + 1);
  int windows_built = 0;
  for (int size = 2; size <= cappedWindow; ++size) {
    const int startMin
        = std::max(runStart, anchorIndex - size + 1 - extraRadius);
    const int startMax
        = std::min(anchorIndex + extraRadius, runStop - size + 1);
    for (int start = startMin; start <= startMax; ++start) {
      if (windows_built >= kMaxResidualWindowsPerSegment) {
        return;
      }
      const int stop = start + size - 1;
      if (!seen_windows.insert(makeWindowKey(segId, start, stop)).second) {
        continue;
      }

      const Node* nextPtr = (stop != static_cast<int>(nodes.size()) - 1)
                                ? nodes[stop + 1]
                                : nullptr;
      DbuX rightLimit{segPtr->getMaxX()};
      if (nextPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
        rightLimit = std::min((nextPtr->getLeft() - leftPadding), rightLimit);
      }
      const Node* prevPtr = (start != 0) ? nodes[start - 1] : nullptr;
      DbuX leftLimit{segPtr->getMinX()};
      if (prevPtr != nullptr) {
        int leftPadding, rightPadding;
        arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
        leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
      }

      ++residual_intervals_built_;
      ++windows_built;
      reorder(nodes,
              start,
              stop,
              leftLimit,
              rightLimit,
              segId,
              rowId,
              targetLeft,
              anchorIndex - start,
              residualGuided);
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder(const std::vector<Node*>& nodes,
                                const int jstrt,
                                const int jstop,
                                const DbuX leftLimit,
                                const DbuX rightLimit,
                                const int segId,
                                const int rowId,
                                const DbuX targetLeft,
                                const int anchorIndex,
                                const bool residualGuided,
                                const bool countResidualTelemetry,
                                const TelemetryMode telemetryMode)
{
  const int size = jstop - jstrt + 1;
  if (size <= 0) {
    return;
  }

  std::vector<DbuX> origLeft(size, DbuX{0});
  for (int i = 0; i < size; i++) {
    origLeft[i] = nodes[jstrt + i]->getLeft();
  }

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
    return;
  }

  const auto affectedEdges = collectAffectedEdges(nodes, jstrt, jstop);
  if (affectedEdges.empty()) {
    return;
  }

  double bestCost = cost(affectedEdges);
  const double origCost = bestCost;
  const DbuX packedWidth = totalWidth + totalPadding;
  const DbuX maxStart = rightLimit - packedWidth;
  if (maxStart < leftLimit) {
    return;
  }
  const DbuX siteWidth = arch_->getRow(rowId)->getSiteWidth();

  std::vector<DbuX> bestPosn(size, DbuX{0});
  std::vector<DbuX> currPosn(size, DbuX{0});
  std::vector<int> order(size, 0);
  for (int i = 0; i < size; i++) {
    order[i] = i;
  }

  auto clampStart = [&](const DbuX start) {
    return std::max(leftLimit, std::min(maxStart, start));
  };

  bool found = false;
  do {
    if (telemetryMode == TelemetryMode::Residual && countResidualTelemetry) {
      ++residual_permutation_candidates_;
    } else if (telemetryMode == TelemetryMode::Sweep) {
      ++staged_sweep_permutation_candidates_;
    }

    int anchorPos = -1;
    DbuX targetStart = leftLimit;
    DbuX targetStartMinus = leftLimit;
    DbuX targetStartPlus = leftLimit;
    if (residualGuided && anchorIndex >= 0 && anchorIndex < size) {
      DbuX prefix{0};
      for (int pos = 0; pos < size; ++pos) {
        if (order[pos] == anchorIndex) {
          anchorPos = pos;
          break;
        }
        const int ix = order[pos];
        prefix += left[ix] + width[ix] + right[ix];
      }
      if (anchorPos >= 0) {
        targetStart = clampStart(targetLeft - left[anchorIndex] - prefix);
        targetStartMinus = clampStart(targetStart - siteWidth);
        targetStartPlus = clampStart(targetStart + siteWidth);
      }
    }

    std::vector<DbuX> startCandidates;
    startCandidates.reserve(5);
    auto pushStart = [&](const DbuX candidate, const bool shifted) {
      if (candidate < leftLimit || candidate > maxStart) {
        return;
      }
      if (std::find(startCandidates.begin(), startCandidates.end(), candidate)
          != startCandidates.end()) {
        return;
      }
      if (telemetryMode == TelemetryMode::Residual && shifted
          && countResidualTelemetry) {
        ++residual_chain_candidates_;
      }
      startCandidates.push_back(candidate);
    };

    pushStart(leftLimit, false);
    pushStart(clampStart(origLeft[order[0]] - left[order[0]]), false);
    if (residualGuided && anchorPos >= 0) {
      pushStart(targetStart, true);
      pushStart(targetStartMinus, true);
      pushStart(targetStartPlus, true);
    }

    for (const DbuX start : startCandidates) {
      bool dispOkay = true;
      DbuX x = start;
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

      if (!dispOkay) {
        continue;
      }

      for (int i = 0; i < size; ++i) {
        if (mgrPtr_->hasPlacementViolation(nodes[jstrt + i])) {
          dispOkay = false;
          break;
        }
      }
      if (!dispOkay) {
        continue;
      }

      if (telemetryMode == TelemetryMode::Residual && countResidualTelemetry) {
        ++residual_exact_probes_;
      } else if (telemetryMode == TelemetryMode::Sweep) {
        ++staged_sweep_exact_probes_;
      }
      const double currCost = cost(affectedEdges);
      if (currCost < bestCost) {
        bestPosn = currPosn;
        bestCost = currCost;
        found = true;
      }
    }
  } while (std::ranges::next_permutation(order).found);

  if (!found) {
    for (int i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[i]);
      mgrPtr_->paintInGrid(ndi);
    }
    return;
  }

  for (int i = 0; i < size; i++) {
    Node* ndi = nodes[jstrt + i];
    mgrPtr_->eraseFromGrid(ndi);
    ndi->setLeft(bestPosn[i]);
    mgrPtr_->paintInGrid(ndi);
  }

  mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);

  bool shifted = false;
  bool failed = false;
  DbuX leftEdge = leftLimit;
  for (int i = 0; i < size; i++) {
    Node* ndi = nodes[jstrt + i];

    DbuX x = ndi->getLeft();
    if (!mgrPtr_->alignPos(ndi, x, leftEdge, rightLimit)) {
      failed = true;
      break;
    }
    if (abs(x - ndi->getLeft()) != 0) {
      shifted = true;
    }
    mgrPtr_->eraseFromGrid(ndi);
    ndi->setLeft(x);
    mgrPtr_->paintInGrid(ndi);
    leftEdge = ndi->getRight();

    const DbuX dx = abs(ndi->getLeft() - ndi->getOrigLeft());
    if (dx > mgrPtr_->getMaxDisplacementX()) {
      failed = true;
      break;
    }
  }

  if (!failed) {
    if (shifted) {
      const double lastCost = cost(affectedEdges);
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
    for (int i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[i]);
      mgrPtr_->paintInGrid(ndi);
    }
    mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
    return;
  }

  if (telemetryMode == TelemetryMode::Residual && bestCost + 1e-3 < origCost) {
    residual_hpwl_gain_ += (origCost - bestCost);
    ++residual_accepted_moves_;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
DetailedReorderer::ResidualWindowSnapshot
DetailedReorderer::captureResidualWindowSnapshot(const std::vector<Node*>& nodes,
                                                 const int segId,
                                                 const int start,
                                                 const int stop)
{
  ResidualWindowSnapshot snapshot;
  snapshot.segId = segId;
  if (start < 0 || stop >= static_cast<int>(nodes.size()) || start > stop) {
    return snapshot;
  }

  snapshot.nodes.reserve(stop - start + 1);
  snapshot.origLeft.reserve(stop - start + 1);
  for (int idx = start; idx <= stop; ++idx) {
    snapshot.nodes.push_back(nodes[idx]);
    snapshot.origLeft.push_back(nodes[idx]->getLeft());
  }
  return snapshot;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::restoreResidualWindowSnapshot(
    const ResidualWindowSnapshot& snapshot)
{
  if (snapshot.segId < 0 || snapshot.nodes.empty()) {
    return;
  }

  for (size_t idx = 0; idx < snapshot.nodes.size(); ++idx) {
    Node* node = snapshot.nodes[idx];
    if (node == nullptr) {
      continue;
    }
    mgrPtr_->eraseFromGrid(node);
    node->setLeft(snapshot.origLeft[idx]);
    mgrPtr_->paintInGrid(node);
  }
  mgrPtr_->sortCellsInSeg(snapshot.segId);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::isResidualWindowChanged(
    const ResidualWindowSnapshot& snapshot) const
{
  for (size_t idx = 0; idx < snapshot.nodes.size(); ++idx) {
    const Node* node = snapshot.nodes[idx];
    if (node != nullptr && node->getLeft() != snapshot.origLeft[idx]) {
      return true;
    }
  }
  return false;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<const Edge*> DetailedReorderer::collectAffectedEdgesForSnapshots(
    const ResidualWindowSnapshot& first,
    const ResidualWindowSnapshot* second)
{
  ++traversal_;

  std::vector<const Edge*> edges;
  edges.reserve((first.nodes.size()
                 + (second != nullptr ? second->nodes.size() : 0))
                * 4);
  auto append_nodes = [&](const std::vector<Node*>& nodes) {
    for (const Node* node : nodes) {
      if (node == nullptr) {
        continue;
      }
      for (int pi = 0; pi < node->getNumPins(); ++pi) {
        const Pin* pin = node->getPins()[pi];
        const Edge* edge = pin->getEdge();
        const int npins = edge->getNumPins();
        if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
          continue;
        }
        if (edgeMask_[edge->getId()] == traversal_) {
          continue;
        }
        edgeMask_[edge->getId()] = traversal_;
        edges.push_back(edge);
      }
    }
  };

  append_nodes(first.nodes);
  if (second != nullptr) {
    append_nodes(second->nodes);
  }
  return edges;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<const Edge*> DetailedReorderer::collectAffectedEdgesForSnapshotSet(
    const std::vector<ResidualWindowSnapshot>& snapshots)
{
  ++traversal_;

  std::vector<const Edge*> edges;
  for (const auto& snapshot : snapshots) {
    for (const Node* node : snapshot.nodes) {
      if (node == nullptr) {
        continue;
      }
      for (int pi = 0; pi < node->getNumPins(); ++pi) {
        const Pin* pin = node->getPins()[pi];
        const Edge* edge = pin->getEdge();
        const int npins = edge->getNumPins();
        if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
          continue;
        }
        if (edgeMask_[edge->getId()] == traversal_) {
          continue;
        }
        edgeMask_[edge->getId()] = traversal_;
        edges.push_back(edge);
      }
    }
  }
  return edges;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<const Edge*> DetailedReorderer::collectAffectedEdges(
    const std::vector<Node*>& nodes,
    const int istrt,
    const int istop)
{
  ++traversal_;

  std::vector<const Edge*> edges;
  edges.reserve((istop - istrt + 1) * 4);
  for (int i = istrt; i <= istop; i++) {
    const Node* ndi = nodes[i];
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
      edges.push_back(edi);
    }
  }
  return edges;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
double DetailedReorderer::cost(const std::vector<const Edge*>& edges) const
{
  double total = 0.0;
  for (const Edge* edge : edges) {
    total += edge->hpwl();
  }
  return total;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::isSingleHeightMovable(const Node* node) const
{
  return node != nullptr && arch_->isSingleHeightCell(node) && !node->isFixed();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
int DetailedReorderer::findAnchorIndex(const std::vector<Node*>& nodes,
                                       const Node* anchorNode,
                                       const DbuX targetLeft) const
{
  if (anchorNode != nullptr) {
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      if (nodes[i] == anchorNode && isSingleHeightMovable(nodes[i])) {
        return i;
      }
    }
  }

  int bestIndex = -1;
  DbuX bestDistance = std::numeric_limits<DbuX>::max();
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    Node* node = nodes[i];
    if (!isSingleHeightMovable(node)) {
      continue;
    }
    const DbuX distance = abs(node->getLeft() - targetLeft);
    if (bestIndex < 0 || distance < bestDistance) {
      bestIndex = i;
      bestDistance = distance;
    }
  }
  return bestIndex;
}

}  // namespace dpl_evolve
