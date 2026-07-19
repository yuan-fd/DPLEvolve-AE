// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <functional>
#include <limits>
#include <string>
#include <unordered_map>
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
constexpr int kResidualAnchorRadius = 1;
constexpr int kResidualBlockerRadius = 2;
constexpr int kResidualPackedWindowSize = 8;
constexpr int kResidualPackedCandidateLimit = 2;
constexpr int kResidualPackedPermutationCap = 120;
constexpr int kResidualPackedProbeCapPerWindow = 6;
constexpr int kResidualAssignmentWindowSize = 5;
constexpr int kResidualAssignmentOrderCap = 24;
constexpr int kResidualAssignmentProbeCapPerWindow = 18;
constexpr int kResidualAssignmentHighScoreOrderCap = 48;
constexpr int kResidualAssignmentHighScoreProbeCap = 36;
constexpr int kResidualAssignmentMediumScoreOrderCap = 32;
constexpr int kResidualAssignmentMediumScoreProbeCap = 24;
constexpr int kResidualProducerHighScore = 96;
constexpr int kResidualProducerMediumScore = 32;

uint64_t makeWindowKey(const int segId, const int start, const int stop)
{
  return (static_cast<uint64_t>(static_cast<uint32_t>(segId)) << 32)
         | (static_cast<uint64_t>(static_cast<uint16_t>(start)) << 16)
         | static_cast<uint64_t>(static_cast<uint16_t>(stop));
}

}  // namespace

bool DetailedReorderer::PackedWindowCostMemoKey::operator==(
    const PackedWindowCostMemoKey& other) const
{
  return segId == other.segId && size == other.size && nodeIds == other.nodeIds
         && lefts == other.lefts;
}

size_t DetailedReorderer::PackedWindowCostMemoKeyHash::operator()(
    const PackedWindowCostMemoKey& key) const
{
  size_t hash = std::hash<int>{}(key.segId);
  hash ^= std::hash<int>{}(key.size) + 0x9e3779b9 + (hash << 6) + (hash >> 2);
  for (int i = 0; i < key.size; ++i) {
    hash ^= std::hash<int>{}(key.nodeIds[i]) + 0x9e3779b9 + (hash << 6)
            + (hash >> 2);
    hash ^= std::hash<int>{}(key.lefts[i]) + 0x9e3779b9 + (hash << 6)
            + (hash >> 2);
  }
  return hash;
}

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
  invalidatePackedCostMemo();

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
  residual_window_candidates_ = 0;
  residual_exact_probes_ = 0;
  residual_permutation_candidates_ = 0;
  residual_chain_candidates_ = 0;
  residual_accepted_moves_ = 0;
  residual_packed_windows_ = 0;
  residual_packed_order_candidates_ = 0;
  residual_packed_probes_ = 0;
  residual_packed_accepts_ = 0;
  residual_packed_memo_hits_ = 0;
  residual_packed_memo_misses_ = 0;
  residual_packed_memo_invalidations_ = 0;
  residual_assignment_windows_ = 0;
  residual_assignment_order_candidates_ = 0;
  residual_assignment_probes_ = 0;
  residual_assignment_accepts_ = 0;
  residual_assignment_fallbacks_ = 0;
  residual_assignment_high_score_windows_ = 0;
  residual_assignment_high_score_accepts_ = 0;
  residual_hpwl_gain_ = 0.0;
  residual_packed_hpwl_gain_ = 0.0;
  residual_assignment_hpwl_gain_ = 0.0;
  residual_assignment_high_score_hpwl_gain_ = 0.0;
  residual_fallback_sweep_ = false;

  const bool used_residual = reorderResidualIntervals();
  if (!used_residual) {
    residual_fallback_sweep_ = true;
    reorderAllSegments();
  }

  mgrPtr_->getLogger()->info(
      DPL,
      350,
      "Residual reorder frontier size {:d}, active {:d}, prioritized {:d}, "
      "targeted segments {:d}, candidates {:d}, intervals {:d}, exact probes "
      "{:d}, perm candidates {:d}, chain candidates {:d}, accepted {:d}, "
      "packed windows {:d}, packed orders {:d}, packed probes {:d}, packed "
      "accepts {:d}, assignment windows {:d}, assignment orders {:d}, "
      "assignment probes {:d}, assignment accepts {:d}, assignment fallback "
      "{:d}, assignment high-score windows {:d}, assignment high-score "
      "accepts {:d}, packed memo hit/miss/invalidate {:d}/{:d}/{:d}, exact "
      "HPWL gain {:.2f}, packed HPWL gain {:.2f}, assignment HPWL gain "
      "{:.2f}, assignment high-score HPWL gain {:.2f}, "
      "fallback sweep {:d}.",
      residual_frontier_size_,
      residual_frontier_active_,
      residual_prioritized_cells_,
      residual_targeted_segments_,
      residual_window_candidates_,
      residual_intervals_built_,
      residual_exact_probes_,
      residual_permutation_candidates_,
      residual_chain_candidates_,
      residual_accepted_moves_,
      residual_packed_windows_,
      residual_packed_order_candidates_,
      residual_packed_probes_,
      residual_packed_accepts_,
      residual_assignment_windows_,
      residual_assignment_order_candidates_,
      residual_assignment_probes_,
      residual_assignment_accepts_,
      residual_assignment_fallbacks_,
      residual_assignment_high_score_windows_,
      residual_assignment_high_score_accepts_,
      residual_packed_memo_hits_,
      residual_packed_memo_misses_,
      residual_packed_memo_invalidations_,
      residual_hpwl_gain_,
      residual_packed_hpwl_gain_,
      residual_assignment_hpwl_gain_,
      residual_assignment_high_score_hpwl_gain_,
      residual_fallback_sweep_ ? 1 : 0);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_frontier_size",
                               residual_frontier_size_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_frontier_active",
                               residual_frontier_active_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_prioritized_cells",
      residual_prioritized_cells_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_targeted_segments",
                               residual_targeted_segments_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_window_candidates",
                               residual_window_candidates_);
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
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_packed_windows",
                               residual_packed_windows_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_packed_order_candidates",
                               residual_packed_order_candidates_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_packed_probes",
                               residual_packed_probes_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_packed_accepts",
                               residual_packed_accepts_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_windows",
      residual_assignment_windows_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_order_candidates",
      residual_assignment_order_candidates_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_probes",
      residual_assignment_probes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_accepts",
      residual_assignment_accepts_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_fallbacks",
      residual_assignment_fallbacks_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_high_score_windows",
      residual_assignment_high_score_windows_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_assignment_high_score_accepts",
      residual_assignment_high_score_accepts_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_packed_memo_hits",
                               residual_packed_memo_hits_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_packed_memo_misses",
      residual_packed_memo_misses_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__reorder__residual_packed_memo_invalidations",
      residual_packed_memo_invalidations_);
  mgrPtr_->getLogger()->metric("dpl_evolve__reorder__residual_fallback_sweep",
                               residual_fallback_sweep_ ? 1 : 0);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorderAllSegments()
{
  for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(s);
    const int segId = segPtr->getSegId();
    const int rowId = segPtr->getRowId();

    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(segId);

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

        reorder(nodes,
                istrt,
                istop,
                leftLimit,
                rightLimit,
                segId,
                rowId,
                leftLimit,
                -1,
                false);
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
                          std::max(0, -residual.producer_rank),
                          residual.producer_sites,
                          seen_windows,
                          std::max(windowSize_ + 1, kResidualPackedWindowSize),
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
                              std::max(0, -residual.producer_rank),
                              residual.producer_sites,
                              seen_windows,
                              std::max(windowSize_ + 1,
                                       kResidualPackedWindowSize),
                              kResidualBlockerRadius);
      }
    }
    built = true;
  }

  return built;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<DetailedReorderer::ResidualWindowCandidate>
DetailedReorderer::buildResidualWindowCandidates(
    const std::vector<Node*>& nodes,
    const int segId,
    const int anchorIndex,
    const DbuX targetLeft,
    const int maxWindow,
    const int extraRadius,
    std::unordered_set<uint64_t>& seen_windows)
{
  std::vector<ResidualWindowCandidate> candidates;
  if (anchorIndex < 0 || anchorIndex >= static_cast<int>(nodes.size())
      || !isSingleHeightMovable(nodes[anchorIndex])) {
    return candidates;
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
    return candidates;
  }

  struct CandidateScore
  {
    ResidualWindowCandidate candidate;
    int overlap = 0;
    DbuX distance{0};
    int span = 0;
  };

  std::vector<CandidateScore> scored;
  const DbuX siteWidth = mgrPtr_->getGrid()->getSiteWidth();
  const int cappedWindow = std::min(maxWindow, runStop - runStart + 1);
  for (int size = 2; size <= cappedWindow; ++size) {
    const int startMin
        = std::max(runStart, anchorIndex - size + 1 - extraRadius);
    const int startMax
        = std::min(anchorIndex + extraRadius, runStop - size + 1);
    for (int start = startMin; start <= startMax; ++start) {
      const int stop = start + size - 1;
      if (seen_windows.find(makeWindowKey(segId, start, stop))
          != seen_windows.end()) {
        continue;
      }

      const DbuX windowLeft = nodes[start]->getLeft();
      const DbuX windowRight = nodes[stop]->getRight();
      const bool targetInside = targetLeft >= windowLeft && targetLeft <= windowRight;
      const DbuX distance = targetInside
                                ? DbuX{0}
                                : std::min(abs(targetLeft - windowLeft),
                                           abs(targetLeft - windowRight));
      const int overlap = (start <= anchorIndex && anchorIndex <= stop) ? 1 : 0;
      const int span = stop - start + 1;
      int producerScore
          = std::max(0, overlap * 32 - distance.v / std::max(1, siteWidth.v));
      int producerSpan = span;
      int assignmentOrderCap = kResidualAssignmentOrderCap;
      int assignmentProbeCap = kResidualAssignmentProbeCapPerWindow;
      scored.push_back({ResidualWindowCandidate{start,
                                                stop,
                                                anchorIndex,
                                                producerScore,
                                                producerSpan,
                                                assignmentOrderCap,
                                                assignmentProbeCap},
                        overlap,
                        distance,
                        span});
    }
  }

  std::sort(scored.begin(),
            scored.end(),
            [](const CandidateScore& lhs, const CandidateScore& rhs) {
              if (lhs.overlap != rhs.overlap) {
                return lhs.overlap > rhs.overlap;
              }
              if (lhs.distance != rhs.distance) {
                return lhs.distance < rhs.distance;
              }
              if (lhs.span != rhs.span) {
                return lhs.span > rhs.span;
              }
              if (lhs.candidate.start != rhs.candidate.start) {
                return lhs.candidate.start < rhs.candidate.start;
              }
              return lhs.candidate.stop < rhs.candidate.stop;
            });

  const int limit = std::min(kMaxResidualWindowsPerSegment,
                             std::max(kResidualPackedCandidateLimit,
                                      windowSize_ + 1));
  for (const CandidateScore& score : scored) {
    if (static_cast<int>(candidates.size()) >= limit) {
      break;
    }
    if (!seen_windows
             .insert(makeWindowKey(segId,
                                   score.candidate.start,
                                   score.candidate.stop))
             .second) {
      continue;
    }
    candidates.push_back(score.candidate);
  }

  residual_window_candidates_ += static_cast<int>(candidates.size());
  return candidates;
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
    const int producerScore,
    const int producerSpan,
    std::unordered_set<uint64_t>& seen_windows,
    const int maxWindow,
    const int extraRadius)
{
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  if (segPtr == nullptr) {
    return;
  }

  std::vector<ResidualWindowCandidate> candidates
      = buildResidualWindowCandidates(nodes,
                                      segId,
                                      anchorIndex,
                                      targetLeft,
                                      maxWindow,
                                      extraRadius,
                                      seen_windows);
  if (candidates.empty()) {
    return;
  }

  for (const ResidualWindowCandidate& candidate : candidates) {
    const int start = candidate.start;
    const int stop = candidate.stop;

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

    int assignmentOrderCap = candidate.assignmentOrderCap;
    int assignmentProbeCap = candidate.assignmentProbeCap;
    const int combinedScore
        = producerScore + candidate.producerScore
          - std::max(0, producerSpan - candidate.producerSpan);
    if (combinedScore >= kResidualProducerHighScore) {
      assignmentOrderCap = kResidualAssignmentHighScoreOrderCap;
      assignmentProbeCap = kResidualAssignmentHighScoreProbeCap;
      ++residual_assignment_high_score_windows_;
    } else if (combinedScore >= kResidualProducerMediumScore) {
      assignmentOrderCap = kResidualAssignmentMediumScoreOrderCap;
      assignmentProbeCap = kResidualAssignmentMediumScoreProbeCap;
    }

    ++residual_intervals_built_;
    reorder(nodes,
            start,
            stop,
            leftLimit,
            rightLimit,
            segId,
            rowId,
            targetLeft,
            candidate.anchorIndex - start,
            residualGuided,
            assignmentOrderCap,
            assignmentProbeCap);
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
                                const int assignmentOrderCapOverride,
                                const int assignmentProbeCapOverride)
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

  const bool packedWindow = residualGuided && size >= 3;
  double bestCost
      = memoizedCost(nodes, jstrt, jstop, segId, packedWindow, affectedEdges);
  const double origCost = bestCost;
  const DbuX packedWidth = totalWidth + totalPadding;
  const DbuX maxStart = rightLimit - packedWidth;
  if (maxStart < leftLimit) {
    return;
  }
  const DbuX siteWidth = arch_->getRow(rowId)->getSiteWidth();
  const DbuX assignmentSlack = (rightLimit - leftLimit) - packedWidth;
  const bool assignmentWindow = packedWindow
                                && size <= kResidualAssignmentWindowSize
                                && assignmentSlack >= siteWidth;
  const int assignmentOrderCap
      = assignmentOrderCapOverride > 0 ? assignmentOrderCapOverride
                                       : kResidualAssignmentOrderCap;
  const int assignmentProbeCap
      = assignmentProbeCapOverride > 0 ? assignmentProbeCapOverride
                                       : kResidualAssignmentProbeCapPerWindow;

  std::vector<DbuX> bestPosn(size, DbuX{0});
  std::vector<DbuX> currPosn(size, DbuX{0});
  std::vector<int> order(size, 0);
  for (int i = 0; i < size; i++) {
    order[i] = i;
  }
  int packedOrderCandidates = 0;
  int packedProbeCount = 0;
  if (packedWindow) {
    ++residual_packed_windows_;
  }
  if (assignmentWindow) {
    ++residual_assignment_windows_;
  }

  auto clampStart = [&](const DbuX start) {
    return std::max(leftLimit, std::min(maxStart, start));
  };

  bool found = false;
  bool assignmentAccepted = false;
  if (assignmentWindow) {
    int assignmentOrderCandidates = 0;
    int assignmentProbeCount = 0;
    auto tryAssignmentOrder = [&](const std::vector<int>& assignmentOrder) {
      if (assignmentProbeCount >= assignmentProbeCap) {
        return;
      }
      ++assignmentOrderCandidates;
      ++residual_assignment_order_candidates_;
      if (assignmentOrderCandidates > assignmentOrderCap) {
        return;
      }

      std::vector<DbuX> candidatePosn(size, DbuX{0});
      DbuX cursor = leftLimit;
      for (int pos = 0; pos < size; ++pos) {
        const int ix = assignmentOrder[pos];
        Node* node = nodes[jstrt + ix];
        DbuX target = origLeft[ix];
        if (residualGuided && anchorIndex >= 0 && ix == anchorIndex) {
          target = clampStart(targetLeft);
        }
        DbuX candidate = std::max(cursor + left[ix], target);
        if (!mgrPtr_->alignPos(node, candidate, cursor + left[ix], rightLimit)) {
          return;
        }
        if (candidate + width[ix] + right[ix] > rightLimit) {
          return;
        }
        candidatePosn[ix] = candidate;
        cursor = candidate + width[ix] + right[ix];
      }

      bool dispOkay = true;
      for (int pos = 0; pos < size; ++pos) {
        const int ix = assignmentOrder[pos];
        Node* node = nodes[jstrt + ix];
        mgrPtr_->eraseFromGrid(node);
        node->setLeft(candidatePosn[ix]);
        mgrPtr_->paintInGrid(node);
        const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
        if (dx > mgrPtr_->getMaxDisplacementX()) {
          dispOkay = false;
        }
      }
      if (!dispOkay) {
        return;
      }
      for (int i = 0; i < size; ++i) {
        if (mgrPtr_->hasPlacementViolation(nodes[jstrt + i])) {
          dispOkay = false;
          break;
        }
      }
      if (!dispOkay) {
        return;
      }

      ++residual_exact_probes_;
      ++residual_assignment_probes_;
      ++assignmentProbeCount;
      const double currCost = cost(affectedEdges);
      if (currCost < bestCost) {
        bestPosn = candidatePosn;
        bestCost = currCost;
        found = true;
        assignmentAccepted = true;
      }
    };

    std::vector<int> assignmentOrder(size, 0);
    for (int i = 0; i < size; ++i) {
      assignmentOrder[i] = i;
    }
    do {
      tryAssignmentOrder(assignmentOrder);
      if (assignmentOrderCandidates >= assignmentOrderCap
          || assignmentProbeCount >= assignmentProbeCap) {
        break;
      }
    } while (std::ranges::next_permutation(assignmentOrder).found);

    if (!assignmentAccepted) {
      ++residual_assignment_fallbacks_;
    }
  }

  for (int i = 0; i < size; i++) {
    Node* ndi = nodes[jstrt + i];
    mgrPtr_->eraseFromGrid(ndi);
    ndi->setLeft(origLeft[i]);
    mgrPtr_->paintInGrid(ndi);
  }

  do {
    if (residualGuided) {
      ++residual_permutation_candidates_;
    }
    if (packedWindow) {
      ++packedOrderCandidates;
      ++residual_packed_order_candidates_;
    }
    if (packedWindow && packedProbeCount >= kResidualPackedProbeCapPerWindow) {
      break;
    }
    if (packedWindow && packedOrderCandidates > kResidualPackedPermutationCap) {
      break;
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
      if (residualGuided && shifted) {
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

      if (residualGuided) {
        ++residual_exact_probes_;
      }
      if (packedWindow) {
        ++residual_packed_probes_;
        ++packedProbeCount;
      }
    const double currCost
          = memoizedCost(nodes, jstrt, jstop, segId, packedWindow, affectedEdges);
      if (currCost < bestCost) {
        bestPosn = currPosn;
        bestCost = currCost;
        found = true;
        assignmentAccepted = false;
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

  if (!failed && shifted) {
    const double lastCost
        = memoizedCost(nodes, jstrt, jstop, segId, packedWindow, affectedEdges);
    if (lastCost >= origCost) {
      failed = true;
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

  if (residualGuided) {
    ++residual_accepted_moves_;
    const double acceptedCost
        = memoizedCost(nodes, jstrt, jstop, segId, packedWindow, affectedEdges);
    residual_hpwl_gain_ += (origCost - acceptedCost);
    if (packedWindow) {
      ++residual_packed_accepts_;
      residual_packed_hpwl_gain_ += (origCost - acceptedCost);
    }
    if (assignmentWindow && assignmentAccepted) {
      ++residual_assignment_accepts_;
      residual_assignment_hpwl_gain_ += (origCost - acceptedCost);
      if (assignmentOrderCapOverride >= kResidualAssignmentHighScoreOrderCap) {
        ++residual_assignment_high_score_accepts_;
        residual_assignment_high_score_hpwl_gain_ += (origCost - acceptedCost);
      }
    }
  }

  if (packedWindow) {
    invalidatePackedCostMemo();
  }
}

double DetailedReorderer::memoizedCost(const std::vector<Node*>& nodes,
                                       const int jstrt,
                                       const int jstop,
                                       const int segId,
                                       const bool packedWindow,
                                       const std::vector<const Edge*>& edges)
{
  if (!packedWindow) {
    return cost(edges);
  }

  const int size = jstop - jstrt + 1;
  if (size <= 0 || size > kMaxPackedMemoWindowCells) {
    ++residual_packed_memo_misses_;
    return cost(edges);
  }

  PackedWindowCostMemoKey key;
  key.segId = segId;
  key.size = size;
  for (int i = 0; i < size; ++i) {
    const Node* node = nodes[jstrt + i];
    key.nodeIds[i] = node->getId();
    key.lefts[i] = node->getLeft().v;
  }

  auto it = packed_cost_memo_.find(key);
  if (it != packed_cost_memo_.end()) {
    ++residual_packed_memo_hits_;
    return it->second;
  }

  const double total = cost(edges);
  packed_cost_memo_.emplace(key, total);
  ++residual_packed_memo_misses_;
  return total;
}

void DetailedReorderer::invalidatePackedCostMemo()
{
  if (!packed_cost_memo_.empty()) {
    packed_cost_memo_.clear();
    ++packed_cost_generation_;
    ++residual_packed_memo_invalidations_;
  }
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
