// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
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
#include "objective/detailed_hpwl.h"
#include "util/utility.h"
#include "utl/Logger.h"

using utl::DPL;

namespace dpl_evolve {

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
  assignmentSelectedMode_ = mgrPtr_->useAcceptedAssignmentPolishForReorder()
                            && mgrPtr_->hasAcceptedAssignmentPolishSegments();
  focusedMode_ = !assignmentSelectedMode_
                 && mgrPtr_->getReorderFocusSegments().empty()
                 && mgrPtr_->hasFocusedSegments();
  focusedSegmentsVisited_ = 0;
  focusedWindowsTried_ = 0;
  focusedWindowsAccepted_ = 0;
  assignmentSegmentsVisited_ = 0;
  assignmentWindowsTried_ = 0;
  assignmentWindowsAccepted_ = 0;
  stickyWindowsSkipped_ = 0;
  stickyNodesGuarded_ = 0;

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
                               (double) curr_hpwl);
    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / (double) last_hpwl <= tol) {
      // std::cout << "Terminating due to low improvement." << std::endl;
      break;
    }
  }
  mgrPtr_->resortSegments();
  const double curr_imp
      = (((init_hpwl - curr_hpwl) / (double) init_hpwl) * 100.);
  mgrPtr_->getLogger()->info(
      DPL,
      305,
      "End of reordering; objective is {:.6e}, improvement is {:.2f} percent.",
      (double) curr_hpwl,
      curr_imp);
  if (assignmentSelectedMode_) {
    mgrPtr_->getLogger()->metric("dpl_evolve__selected_reorder__assignment_mode",
                                 1);
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__input_segments",
        mgrPtr_->getNumAcceptedAssignmentPolishSegments());
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__assignment_nodes_input",
        mgrPtr_->getNumAcceptedAssignmentPolishNodes());
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__visited_segments",
        assignmentSegmentsVisited_);
    mgrPtr_->getLogger()->metric("dpl_evolve__selected_reorder__windows_tried",
                                 assignmentWindowsTried_);
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__windows_accepted",
        assignmentWindowsAccepted_);
    mgrPtr_->getLogger()->info(
        DPL,
        1234,
        "Assignment-selected reorder consumed {} accepted-assignment segments "
        "(windows {}, accepted {}, nodes {}).",
        assignmentSegmentsVisited_,
        assignmentWindowsTried_,
        assignmentWindowsAccepted_,
        mgrPtr_->getNumAcceptedAssignmentPolishNodes());
  } else if (focusedMode_) {
    mgrPtr_->getLogger()->metric("dpl_evolve__selected_reorder__focus_mode", 1);
    mgrPtr_->getLogger()->metric("dpl_evolve__selected_reorder__input_segments",
                                 mgrPtr_->getNumFocusedSegments());
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__visited_segments",
        focusedSegmentsVisited_);
    mgrPtr_->getLogger()->metric("dpl_evolve__selected_reorder__windows_tried",
                                 focusedWindowsTried_);
    mgrPtr_->getLogger()->metric(
        "dpl_evolve__selected_reorder__windows_accepted",
        focusedWindowsAccepted_);
    mgrPtr_->getLogger()->info(
        DPL,
        1226,
        "Focused reorder consumed {} hot segments (windows {}, accepted {}).",
        focusedSegmentsVisited_,
        focusedWindowsTried_,
        focusedWindowsAccepted_);
  }
  mgrPtr_->getLogger()->metric("dpl_evolve__sticky_reorder__windows_skipped",
                               stickyWindowsSkipped_);
  mgrPtr_->getLogger()->metric("dpl_evolve__sticky_reorder__nodes_guarded",
                               stickyNodesGuarded_);
  if (stickyWindowsSkipped_ > 0) {
    mgrPtr_->getLogger()->info(DPL,
                               1231,
                               "Sticky reorder guard skipped {} windows "
                               "covering {} sticky nodes.",
                               stickyWindowsSkipped_,
                               stickyNodesGuarded_);
  }
  if (assignmentSelectedMode_) {
    mgrPtr_->clearAcceptedAssignmentPolish();
  }
  mgrPtr_->clearReorderFocusSegments();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runAssignmentBoundaryPolish(DetailedMgr* mgrPtr,
                                                    const std::string& command)
{
  boost::char_separator<char> separators(" \r\t\n;");
  boost::tokenizer<boost::char_separator<char>> tokens(command, separators);
  std::vector<std::string> args;
  for (const auto& token : tokens) {
    args.push_back(token);
  }
  runAssignmentBoundaryPolish(mgrPtr, args);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runAssignmentBoundaryPolish(
    DetailedMgr* mgrPtr,
    const std::vector<std::string>& args)
{
  mgrPtr_ = mgrPtr;
  arch_ = mgrPtr_->getArchitecture();
  network_ = mgrPtr_->getNetwork();

  assignmentBoundarySegmentsVisited_ = 0;
  assignmentBoundarySeedNodes_ = 0;
  assignmentBoundaryProbes_ = 0;
  assignmentBoundaryExactScored_ = 0;
  assignmentBoundaryAccepts_ = 0;
  assignmentBoundaryMoves_ = 0;
  assignmentBoundarySwaps_ = 0;
  assignmentBoundaryEndpointProbes_ = 0;
  assignmentBoundaryPairProbes_ = 0;
  assignmentBoundaryCrossRowProbes_ = 0;
  assignmentBoundaryDuplicateProbes_ = 0;
  assignmentBoundaryAcceptedDelta_ = 0.0;

  assignmentBoundaryPolish(args);

  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__input_segments",
      mgrPtr_->getNumAcceptedAssignmentPolishSegments());
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__input_nodes",
      mgrPtr_->getNumAcceptedAssignmentPolishNodes());
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__visited_segments",
      assignmentBoundarySegmentsVisited_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__seed_nodes",
      assignmentBoundarySeedNodes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__probes",
      assignmentBoundaryProbes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__duplicate_probes",
      assignmentBoundaryDuplicateProbes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__exact_scored",
      assignmentBoundaryExactScored_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__accepts",
      assignmentBoundaryAccepts_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__accepted_moves",
      assignmentBoundaryMoves_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__accepted_swaps",
      assignmentBoundarySwaps_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__endpoint_probes",
      assignmentBoundaryEndpointProbes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__pair_probes",
      assignmentBoundaryPairProbes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__cross_row_probes",
      assignmentBoundaryCrossRowProbes_);
  mgrPtr_->getLogger()->metric(
      "dpl_evolve__assignment_boundary_polish__accepted_delta_dbu",
      assignmentBoundaryAcceptedDelta_);
  mgrPtr_->getLogger()->info(
      DPL,
      1235,
      "Assignment-boundary polish consumed {} assignment segments "
      "(seed_nodes {}, probes {}, exact_scored {}, accepts {}, "
      "moves {}, swaps {}, accepted_delta {:.2f}).",
      assignmentBoundarySegmentsVisited_,
      assignmentBoundarySeedNodes_,
      assignmentBoundaryProbes_,
      assignmentBoundaryExactScored_,
      assignmentBoundaryAccepts_,
      assignmentBoundaryMoves_,
      assignmentBoundarySwaps_,
      assignmentBoundaryAcceptedDelta_);
  mgrPtr_->clearAcceptedAssignmentPolish();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::assignmentBoundaryPolish(
    const std::vector<std::string>& args)
{
  int maxSegments = 64;
  int maxSeedNodes = 6;
  int maxProbeRows = 1;
  int maxProbesPerNode = 12;
  double minImprovement = 0.0;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-seg" && i + 1 < args.size()) {
      maxSegments = std::atoi(args[++i].c_str());
    } else if (args[i] == "-nodes" && i + 1 < args.size()) {
      maxSeedNodes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-rows" && i + 1 < args.size()) {
      maxProbeRows = std::atoi(args[++i].c_str());
    } else if (args[i] == "-probe" && i + 1 < args.size()) {
      maxProbesPerNode = std::atoi(args[++i].c_str());
    } else if (args[i] == "-gain" && i + 1 < args.size()) {
      minImprovement = std::atof(args[++i].c_str());
    }
  }
  maxSegments = std::max(1, maxSegments);
  maxSeedNodes = std::max(1, maxSeedNodes);
  maxProbeRows = std::max(0, maxProbeRows);
  maxProbesPerNode = std::max(1, maxProbesPerNode);
  minImprovement = std::max(0.0, minImprovement);

  if (!mgrPtr_->hasAcceptedAssignmentPolishSegments()) {
    return;
  }

  mgrPtr_->resortSegments();

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgrPtr_, nullptr);
  double currHpwl = hpwlObj.curr();
  if (currHpwl <= 0.0) {
    return;
  }

  std::vector<int> assignmentSegments
      = mgrPtr_->getAcceptedAssignmentPolishSegments();
  std::sort(assignmentSegments.begin(),
            assignmentSegments.end(),
            [&](const int lhs, const int rhs) {
              return mgrPtr_->getAcceptedAssignmentPolishScore(lhs)
                     > mgrPtr_->getAcceptedAssignmentPolishScore(rhs);
            });
  if (static_cast<int>(assignmentSegments.size()) > maxSegments) {
    assignmentSegments.resize(maxSegments);
  }

  for (const int segId : assignmentSegments) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
      continue;
    }

    DbuX regionLeft{0};
    DbuX regionRight{0};
    if (!mgrPtr_->getAcceptedAssignmentPolishBounds(segId, regionLeft, regionRight)) {
      continue;
    }

    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    const int rowId = segPtr->getRowId();
    mgrPtr_->sortCellsInSeg(segId);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.empty()) {
      continue;
    }

    std::vector<int> seedIndices = collectAssignmentBoundarySeedIndices(
        nodes, segId, regionLeft, regionRight, maxSeedNodes);
    if (seedIndices.empty()) {
      continue;
    }
    ++assignmentBoundarySegmentsVisited_;
    assignmentBoundarySeedNodes_ += static_cast<int>(seedIndices.size());

    bool acceptedInSegment = false;
    for (const int seedIdx : seedIndices) {
      if (seedIdx < 0 || seedIdx >= static_cast<int>(nodes.size())) {
        continue;
      }
      Node* node = nodes[seedIdx];
      if (node == nullptr || node->isFixed() || !node->isStdCell()
          || !node->isPlaced() || arch_->getCellHeightInRows(node) != 1
          || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
        continue;
      }

      std::vector<BoundaryProbe> probes;
      const DbuY sameRowBottom = arch_->getRow(rowId)->getBottom();
      DbuX sameRowLeft = regionLeft;
      DbuX sameRowRight = regionRight - node->getWidth();
      if (sameRowRight >= segPtr->getMinX()) {
        sameRowLeft = std::max(segPtr->getMinX(), sameRowLeft);
        sameRowRight = std::min(segPtr->getMaxX() - node->getWidth(), sameRowRight);
      }
      addBoundaryProbe(
          probes, segId, sameRowLeft, sameRowBottom, false, true, false);
      addBoundaryProbe(
          probes, segId, sameRowRight, sameRowBottom, false, true, false);
      if (seedIdx > 0 && nodes[seedIdx - 1] != nullptr) {
        addBoundaryProbe(probes,
                         segId,
                         nodes[seedIdx - 1]->getLeft(),
                         sameRowBottom,
                         true,
                         false,
                         false);
      }
      if (seedIdx + 1 < static_cast<int>(nodes.size()) && nodes[seedIdx + 1] != nullptr) {
        addBoundaryProbe(probes,
                         segId,
                         nodes[seedIdx + 1]->getLeft(),
                         sameRowBottom,
                         true,
                         false,
                         false);
      }

      for (int rowOffset = 1; rowOffset <= maxProbeRows; ++rowOffset) {
        for (const int targetRow : {rowId - rowOffset, rowId + rowOffset}) {
          if (targetRow < 0 || targetRow >= arch_->getNumRows()) {
            continue;
          }
          const DbuY targetBottom = arch_->getRow(targetRow)->getBottom();
          int rowSegmentAdds = 0;
          for (DetailedSeg* targetSeg : mgrPtr_->getSegsInRow(targetRow)) {
            if (targetSeg == nullptr || node->getGroupId() != targetSeg->getRegId()) {
              continue;
            }
            const bool overlapsRegion
                = targetSeg->getMaxX() > regionLeft && targetSeg->getMinX() < regionRight;
            const bool overlapsNode
                = targetSeg->getMaxX() > node->getLeft()
                  && targetSeg->getMinX() < node->getRight();
            if (!overlapsRegion && !overlapsNode) {
              continue;
            }

            DbuX leftTarget = std::max(targetSeg->getMinX(), regionLeft);
            DbuX rightTarget
                = std::min(targetSeg->getMaxX() - node->getWidth(),
                           regionRight - node->getWidth());
            DbuX alignedCurrent
                = std::max(targetSeg->getMinX(),
                           std::min(targetSeg->getMaxX() - node->getWidth(),
                                    node->getLeft()));
            addBoundaryProbe(probes,
                             targetSeg->getSegId(),
                             alignedCurrent,
                             targetBottom,
                             false,
                             false,
                             true);
            addBoundaryProbe(probes,
                             targetSeg->getSegId(),
                             alignedCurrent,
                             targetBottom,
                             true,
                             false,
                             true);
            addBoundaryProbe(probes,
                             targetSeg->getSegId(),
                             leftTarget,
                             targetBottom,
                             false,
                             true,
                             true);
            addBoundaryProbe(probes,
                             targetSeg->getSegId(),
                             rightTarget,
                             targetBottom,
                             false,
                             true,
                             true);
            if (++rowSegmentAdds >= 2) {
              break;
            }
          }
        }
      }

      int probesScored = 0;
      for (const BoundaryProbe& probe : probes) {
        if (probesScored >= maxProbesPerNode) {
          break;
        }
        ++probesScored;
        if (scoreAssignmentBoundaryProbe(node,
                                         segId,
                                         probe,
                                         hpwlObj,
                                         currHpwl,
                                         minImprovement)) {
          acceptedInSegment = true;
          break;
        }
      }
      if (acceptedInSegment) {
        mgrPtr_->resortSegments();
        break;
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
std::vector<int> DetailedReorderer::collectAssignmentBoundarySeedIndices(
    const std::vector<Node*>& nodes,
    const int segId,
    const DbuX regionLeft,
    const DbuX regionRight,
    const int maxSeeds) const
{
  std::vector<int> seeds;
  if (nodes.empty() || maxSeeds <= 0 || regionRight <= regionLeft) {
    return seeds;
  }

  int firstInside = -1;
  int lastInside = -1;
  for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
    const Node* node = nodes[i];
    if (node == nullptr) {
      continue;
    }
    const bool overlapsRegion
        = node->getRight() > regionLeft && node->getLeft() < regionRight;
    if (overlapsRegion) {
      if (firstInside == -1) {
        firstInside = i;
      }
      lastInside = i;
    }
  }

  auto add_seed = [&](const int idx) {
    if (idx < 0 || idx >= static_cast<int>(nodes.size())) {
      return;
    }
    if (std::find(seeds.begin(), seeds.end(), idx) == seeds.end()) {
      seeds.push_back(idx);
    }
  };

  if (firstInside != -1) {
    add_seed(firstInside);
    add_seed(lastInside);
    add_seed(firstInside - 1);
    add_seed(lastInside + 1);
  } else {
    int bestIdx = -1;
    DbuX bestDist{std::numeric_limits<int>::max()};
    for (int i = 0; i < static_cast<int>(nodes.size()); ++i) {
      const Node* node = nodes[i];
      if (node == nullptr) {
        continue;
      }
      const DbuX dist = std::abs((node->getCenterX() - regionLeft).v)
                        < std::abs((node->getCenterX() - regionRight).v)
                            ? DbuX{std::abs((node->getCenterX() - regionLeft).v)}
                            : DbuX{std::abs((node->getCenterX() - regionRight).v)};
      if (dist < bestDist) {
        bestDist = dist;
        bestIdx = i;
      }
    }
    add_seed(bestIdx);
  }

  for (int i = 0; i < static_cast<int>(nodes.size()) && static_cast<int>(seeds.size()) < maxSeeds;
       ++i) {
    const Node* node = nodes[i];
    if (node == nullptr || !mgrPtr_->isAcceptedAssignmentPolishNode(node)) {
      continue;
    }
    const bool nearBoundary
        = node->getLeft() <= regionLeft || node->getRight() >= regionRight
          || (firstInside != -1 && (i == firstInside || i == lastInside));
    if (nearBoundary) {
      add_seed(i);
    }
  }

  if (static_cast<int>(seeds.size()) > maxSeeds) {
    seeds.resize(maxSeeds);
  }
  return seeds;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::addBoundaryProbe(std::vector<BoundaryProbe>& probes,
                                         const int segId,
                                         const DbuX targetLeft,
                                         const DbuY targetBottom,
                                         const bool useSwap,
                                         const bool endpoint,
                                         const bool crossRow)
{
  if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
    return;
  }
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  if (segPtr == nullptr) {
    return;
  }
  const DbuX clampedLeft
      = std::max(segPtr->getMinX(), std::min(segPtr->getMaxX(), targetLeft));
  for (const BoundaryProbe& probe : probes) {
    if (probe.seg_id == segId && probe.left == clampedLeft
        && probe.bottom == targetBottom && probe.use_swap == useSwap) {
      ++assignmentBoundaryDuplicateProbes_;
      return;
    }
  }
  probes.push_back(
      BoundaryProbe{segId, clampedLeft, targetBottom, useSwap, endpoint, crossRow});
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::scoreAssignmentBoundaryProbe(Node* node,
                                                     const int sourceSegId,
                                                     const BoundaryProbe& probe,
                                                     DetailedHPWL& hpwlObj,
                                                     double& currHpwl,
                                                     const double minImprovement)
{
  if (node == nullptr || probe.seg_id < 0 || probe.seg_id >= mgrPtr_->getNumSegments()) {
    return false;
  }
  ++assignmentBoundaryProbes_;
  assignmentBoundaryEndpointProbes_ += probe.endpoint ? 1 : 0;
  assignmentBoundaryPairProbes_ += probe.use_swap ? 1 : 0;
  assignmentBoundaryCrossRowProbes_ += probe.cross_row ? 1 : 0;

  const bool generated
      = probe.use_swap
            ? mgrPtr_->trySwap(node,
                               node->getLeft(),
                               node->getBottom(),
                               sourceSegId,
                               probe.left,
                               probe.bottom,
                               probe.seg_id)
            : mgrPtr_->tryMove(node,
                               node->getLeft(),
                               node->getBottom(),
                               sourceSegId,
                               probe.left,
                               probe.bottom,
                               probe.seg_id);
  if (!generated) {
    return false;
  }

  ++assignmentBoundaryExactScored_;
  const double delta = hpwlObj.delta(mgrPtr_->getJournal());
  if (delta <= minImprovement || currHpwl - delta > currHpwl) {
    mgrPtr_->rejectMove();
    return false;
  }

  hpwlObj.accept();
  mgrPtr_->acceptMove();
  currHpwl -= delta;
  ++assignmentBoundaryAccepts_;
  assignmentBoundaryMoves_ += probe.use_swap ? 0 : 1;
  assignmentBoundarySwaps_ += probe.use_swap ? 1 : 0;
  assignmentBoundaryAcceptedDelta_ += delta;
  return true;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);
  if (assignmentSelectedMode_) {
    std::vector<int> assignment_segments
        = mgrPtr_->getAcceptedAssignmentPolishSegments();
    std::sort(assignment_segments.begin(),
              assignment_segments.end(),
              [&](const int lhs, const int rhs) {
                return mgrPtr_->getAcceptedAssignmentPolishScore(lhs)
                       > mgrPtr_->getAcceptedAssignmentPolishScore(rhs);
              });
    constexpr int kMaxAssignmentSegments = 96;
    if (static_cast<int>(assignment_segments.size()) > kMaxAssignmentSegments) {
      assignment_segments.resize(kMaxAssignmentSegments);
    }

    for (const int seg_id : assignment_segments) {
      if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
        continue;
      }
      DetailedSeg* segPtr = mgrPtr_->getSegment(seg_id);
      const int rowId = segPtr->getRowId();
      ++assignmentSegmentsVisited_;

      const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
      if (nodes.size() < 2) {
        continue;
      }
      mgrPtr_->sortCellsInSeg(seg_id);

      int j = 0;
      const int n = static_cast<int>(nodes.size());
      const int assignment_window = std::min(windowSize_ + 1, 5);
      while (j < n) {
        while (j < n && arch_->isMultiHeightCell(nodes[j])) {
          ++j;
        }
        const int jstrt = j;
        while (j < n && arch_->isSingleHeightCell(nodes[j])) {
          ++j;
        }
        const int jstop = j - 1;

        for (int i = jstrt; i + assignment_window <= jstop; ++i) {
          int istrt = i;
          const int istop = std::min(jstop, istrt + assignment_window - 1);
          if (istop == jstop) {
            istrt = std::max(jstrt, istop - assignment_window + 1);
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

          reorder(nodes, istrt, istop, leftLimit, rightLimit, seg_id, rowId);
        }
      }
    }
    return;
  }

  if (focusedMode_) {
    for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
      DetailedSeg* segPtr = mgrPtr_->getSegment(s);
      const int segId = segPtr->getSegId();
      if (!mgrPtr_->isFocusedSegment(segId)) {
        continue;
      }
      const int rowId = segPtr->getRowId();
      ++focusedSegmentsVisited_;

      const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
      if (nodes.size() < 2) {
        continue;
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

          reorder(nodes, istrt, istop, leftLimit, rightLimit, segId, rowId);
        }
      }
    }
    return;
  }

  const std::vector<int> focus_segments = mgrPtr_->getReorderFocusSegments();
  std::vector<unsigned char> processed(mgrPtr_->getNumSegments(), 0);
  const int focus_window = std::min(windowSize_ + 1, 5);
  int focused_segments_processed = 0;

  auto process_segment = [&](const int seg_id, const int window_size) {
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
      return;
    }
    DetailedSeg* segPtr = mgrPtr_->getSegment(seg_id);
    const int segId = segPtr->getSegId();
    const int rowId = segPtr->getRowId();

    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      return;
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

      for (int i = jstrt; i + window_size <= jstop; ++i) {
        int istrt = i;
        const int istop = std::min(jstop, istrt + window_size - 1);
        if (istop == jstop) {
          istrt = std::max(jstrt, istop - window_size + 1);
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

        reorder(nodes, istrt, istop, leftLimit, rightLimit, segId, rowId);
      }
    }
  };

  for (const int seg_id : focus_segments) {
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()
        || processed[seg_id] != 0) {
      continue;
    }
    processed[seg_id] = 1;
    ++focused_segments_processed;
    process_segment(seg_id, focus_window);
  }

  for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
    if (processed[s] != 0) {
      continue;
    }
    process_segment(s, windowSize_);
  }

  if (focused_segments_processed > 0) {
    mgrPtr_->getLogger()->info(DPL,
                               912,
                               "Reorder consumed {} focus segments with "
                               "window_size={}",
                               focused_segments_processed,
                               focus_window);
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
                                const int rowId)
{
  const int size = jstop - jstrt + 1;
  if (size <= 0) {
    return;
  }
  if (assignmentSelectedMode_
      && !mgrPtr_->windowTouchesAcceptedAssignmentRegion(
          nodes, jstrt, jstop, segId)) {
    return;
  }
  const bool exact_gated = focusedMode_ || mgrPtr_->isReorderFocusSegment(segId);
  const int sticky_nodes
      = mgrPtr_->countStickyExactNodesInWindow(nodes, jstrt, jstop);
  if (!exact_gated && sticky_nodes > 0) {
    ++stickyWindowsSkipped_;
    stickyNodesGuarded_ += sticky_nodes;
    return;
  }
  if (focusedMode_) {
    ++focusedWindowsTried_;
  }
  if (assignmentSelectedMode_) {
    ++assignmentWindowsTried_;
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
    return;
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
    return;
  }

  // Generate the different permutations.  Evaluate each one and keep
  // the best one.
  //
  // NOTE: The first permutation, which is the original placement,
  // might not generate the original placement since the spacing
  // might be different.  So, just consider the first permutation
  // like all the others.

  double bestCost = cost(nodes, jstrt, jstop);
  const double origCost = bestCost;

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
      const double currCost = cost(nodes, jstrt, jstop);
      if (currCost < bestCost) {
        bestPosn = currPosn;
        bestCost = currCost;

        found = true;
      }
    }
  } while (std::ranges::next_permutation(order).found);

  if (!found) {
    // No improvement.  Restore positions and return.
    for (size_t i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[ndi]);
      mgrPtr_->paintInGrid(ndi);
    }
    return;
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
    } else if (assignmentSelectedMode_) {
      ++assignmentWindowsAccepted_;
    } else if (focusedMode_) {
      ++focusedWindowsAccepted_;
    }
  }
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
