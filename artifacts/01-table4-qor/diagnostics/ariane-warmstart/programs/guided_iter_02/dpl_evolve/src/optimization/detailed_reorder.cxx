// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <cstdlib>
#include <limits>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "objective/detailed_hpwl.h"
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

  uint64_t hpwl_x, hpwl_y;
  int64_t curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  const int64_t init_hpwl = curr_hpwl;
  if (init_hpwl == 0) {
    return;
  }
  frontierSegmentsVisited_ = 0;
  reorderWindowsTried_ = 0;
  reorderWindowsAccepted_ = 0;
  reorderAcceptedGain_ = 0.0;
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
  mgrPtr_->getLogger()->info(DPL,
                             931,
                             "Selected-segment reorder frontier={} windows={} "
                             "accepts={} gain={:.2f}",
                             frontierSegmentsVisited_,
                             reorderWindowsTried_,
                             reorderWindowsAccepted_,
                             reorderAcceptedGain_);
  runAcceptedFootprintClosure();
  mgrPtr_->clearAcceptedMoveState();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  std::vector<unsigned char> visited(mgrPtr_->getNumSegments(), 0);
  for (const int seg_id : mgrPtr_->getHotSegments()) {
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments() || visited[seg_id]) {
      continue;
    }
    visited[seg_id] = 1;
    frontierSegmentsVisited_++;
    reorderSegment(seg_id);
  }

  // Loop over each segment; find single height cells and reorder.
  for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
    if (visited[s]) {
      continue;
    }
    reorderSegment(s);
  }
}

void DetailedReorderer::reorderSegment(const int segId)
{
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  const int rowId = segPtr->getRowId();

  const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
  if (nodes.size() < 2) {
    return;
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
  reorderWindowsTried_++;
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
  const double gain = origCost - bestCost;
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
      return;
    }
  }
  reorderWindowsAccepted_++;
  reorderAcceptedGain_ += std::max(0.0, gain);
  for (int i = jstrt; i <= jstop; i++) {
    mgrPtr_->recordAcceptedNode(nodes[i], segId);
  }
}

void DetailedReorderer::runAcceptedFootprintClosure()
{
  const auto& accepted_nodes = mgrPtr_->getAcceptedNodes();
  if (accepted_nodes.empty()) {
    mgrPtr_->getLogger()->info(
        DPL,
        932,
        "Accepted-footprint closure frontier_nodes=0 frontier_segments=0 "
        "probes=0 exact_scored=0 accepts=0 gain=0.00 runtime_ms=0.0 "
        "delta_per_runtime=0.00");
    return;
  }

  constexpr int kMaxSeedFrontierNodes = 2048;
  constexpr int kMaxProcessedNodes = 3072;
  constexpr int kMaxNeighborRows = 1;
  constexpr int kMaxSegCandidatesPerRow = 3;
  constexpr int kMaxNeighborsPerSeg = 2;
  constexpr int kMaxProbeBudget = 32768;
  constexpr int kMaxNodeVisits = 2;
  constexpr int kMinProbesBeforeStop = 128;
  constexpr int kMaxZeroGainStreak = 256;

  auto byPriority = [](const std::pair<double, Node*>& lhs,
                       const std::pair<double, Node*>& rhs) {
    return lhs.first < rhs.first;
  };

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgrPtr_, nullptr);
  hpwlObj.curr();

  std::vector<std::pair<double, Node*>> ranked_frontier;
  ranked_frontier.reserve(accepted_nodes.size());
  for (Node* node : accepted_nodes) {
    if (node == nullptr || !node->isStdCell() || node->isFixed()
        || !arch_->isSingleHeightCell(node)
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    ranked_frontier.emplace_back(-node->getNumPins(), node);
  }
  std::sort(ranked_frontier.begin(), ranked_frontier.end(), byPriority);

  int frontier_nodes = 0;
  int frontier_segments = 0;
  int probes = 0;
  int exact_scored = 0;
  int accepts = 0;
  double accepted_gain = 0.0;
  int zero_gain_streak = 0;
  const auto start = std::chrono::steady_clock::now();

  std::deque<Node*> frontier_queue;
  std::vector<unsigned char> queued_mask(network_->getNumNodes(), 0);
  std::vector<unsigned char> visit_count(network_->getNumNodes(), 0);
  std::vector<unsigned char> frontier_segment_mask(mgrPtr_->getNumSegments(), 0);

  const auto queueNode = [&](Node* node) {
    if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      return;
    }
    const int node_id = node->getId();
    if (node_id < 0 || node_id >= queued_mask.size()
        || queued_mask[node_id] || visit_count[node_id] >= kMaxNodeVisits) {
      return;
    }
    queued_mask[node_id] = 1;
    frontier_queue.push_back(node);
    const int seg_id = mgrPtr_->getReverseCellToSegs(node_id)[0]->getSegId();
    if (seg_id >= 0 && seg_id < frontier_segment_mask.size()
        && !frontier_segment_mask[seg_id]) {
      frontier_segment_mask[seg_id] = 1;
      frontier_segments++;
    }
  };

  for (const auto& [_, node] : ranked_frontier) {
    if (frontier_queue.size() >= kMaxSeedFrontierNodes) {
      break;
    }
    queueNode(node);
  }

  while (!frontier_queue.empty() && frontier_nodes < kMaxProcessedNodes
         && probes < kMaxProbeBudget) {
    Node* node = frontier_queue.front();
    frontier_queue.pop_front();
    if (node == nullptr) {
      continue;
    }
    const int node_id = node->getId();
    if (node_id < 0 || node_id >= queued_mask.size()) {
      continue;
    }
    queued_mask[node_id] = 0;
    if (visit_count[node_id] >= kMaxNodeVisits
        || mgrPtr_->getNumReverseCellToSegs(node_id) != 1) {
      continue;
    }
    visit_count[node_id]++;
    frontier_nodes++;

    odb::Rect bbox;
    bbox.mergeInit();
    int bbox_points = 0;
    for (Pin* pin : node->getPins()) {
      Edge* edge = pin->getEdge();
      const int num_pins = edge->getNumPins();
      if (num_pins <= 1 || num_pins >= skipNetsLargerThanThis_) {
        continue;
      }
      odb::Rect edge_bbox;
      edge_bbox.mergeInit();
      int count = 0;
      for (Pin* other_pin : edge->getPins()) {
        Node* other = other_pin->getNode();
        if (other == node) {
          continue;
        }
        const DbuX x = other->getCenterX() + other_pin->getOffsetX();
        const DbuY y = other->getCenterY() + other_pin->getOffsetY();
        edge_bbox.set_xlo(std::min(x.v, edge_bbox.xMin()));
        edge_bbox.set_xhi(std::max(x.v, edge_bbox.xMax()));
        edge_bbox.set_ylo(std::min(y.v, edge_bbox.yMin()));
        edge_bbox.set_yhi(std::max(y.v, edge_bbox.yMax()));
        count++;
      }
      if (count == 0) {
        continue;
      }
      bbox.merge(edge_bbox);
      bbox_points++;
    }
    if (bbox_points == 0) {
      continue;
    }

    const int source_seg = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    const int source_row = mgrPtr_->getSegment(source_seg)->getRowId();
    const double bbox_center = 0.5 * (bbox.xMin() + bbox.xMax());

    std::vector<int> candidate_rows;
    candidate_rows.push_back(source_row);
    const int target_row = arch_->find_closest_row(
        DbuY{static_cast<int>(std::floor(0.5 * (bbox.yMin() + bbox.yMax())
                                         - 0.5 * node->getHeight().v))});
    candidate_rows.push_back(target_row);
    for (int delta = 1; delta <= kMaxNeighborRows; delta++) {
      if (target_row - delta >= 0) {
        candidate_rows.push_back(target_row - delta);
      }
      if (target_row + delta < arch_->getNumRows()) {
        candidate_rows.push_back(target_row + delta);
      }
    }
    std::sort(candidate_rows.begin(), candidate_rows.end());
    candidate_rows.erase(std::unique(candidate_rows.begin(), candidate_rows.end()),
                         candidate_rows.end());

    struct ClosureCandidate
    {
      enum class Kind
      {
        Move,
        Swap
      };
      Kind kind = Kind::Move;
      int target_seg = -1;
      DbuX target_x{0};
      DbuY target_y{0};
      double cheap_score = 0.0;
    };

    std::vector<ClosureCandidate> candidates;
    candidates.reserve(48);
    std::unordered_set<uint64_t> seen;
    const std::vector<int> anchors{
        bbox.xMin(),
        static_cast<int>(std::floor(bbox_center - 0.5 * node->getWidth().v)),
        bbox.xMax() - node->getWidth().v,
        node->getLeft().v};

    for (const int row_id : candidate_rows) {
      std::vector<std::pair<double, DetailedSeg*>> ranked_segments;
      for (DetailedSeg* segPtr : mgrPtr_->getSegsInRow(row_id)) {
        if (segPtr == nullptr || segPtr->getRegId() != node->getGroupId()) {
          continue;
        }
        const double seg_center
            = 0.5 * (segPtr->getMinX().v + segPtr->getMaxX().v);
        ranked_segments.emplace_back(std::abs(seg_center - bbox_center), segPtr);
      }
      std::sort(ranked_segments.begin(), ranked_segments.end());
      if (ranked_segments.size() > kMaxSegCandidatesPerRow) {
        ranked_segments.resize(kMaxSegCandidatesPerRow);
      }

      for (const auto& [__, segPtr] : ranked_segments) {
        if (segPtr == nullptr) {
          continue;
        }
        for (const int anchor : anchors) {
          const DbuX clamped_left{std::min(
              std::max(anchor, segPtr->getMinX().v),
              segPtr->getMaxX().v - node->getWidth().v)};
          ClosureCandidate move_candidate;
          move_candidate.kind = ClosureCandidate::Kind::Move;
          move_candidate.target_seg = segPtr->getSegId();
          move_candidate.target_x = clamped_left;
          move_candidate.target_y = arch_->getRow(row_id)->getBottom();
          move_candidate.cheap_score
              = std::abs(node->getCenterX().v - bbox_center)
                - std::abs(clamped_left.v + (0.5 * node->getWidth().v)
                           - bbox_center)
                - 0.1 * std::abs(clamped_left.v - node->getLeft().v)
                - 0.02 * std::abs(move_candidate.target_y.v - node->getBottom().v)
                + ((move_candidate.target_seg == source_seg) ? 4.0 : 0.0);
          const uint64_t move_key
              = (static_cast<uint64_t>(0) << 63)
                | (static_cast<uint64_t>(move_candidate.target_seg & 0x7fffffff)
                   << 32)
                | static_cast<uint32_t>(move_candidate.target_x.v);
          if (seen.insert(move_key).second) {
            candidates.push_back(move_candidate);
          }
        }

        const auto& seg_nodes = mgrPtr_->getCellsInSeg(segPtr->getSegId());
        std::vector<std::pair<double, Node*>> ranked_neighbors;
        ranked_neighbors.reserve(seg_nodes.size());
        for (Node* partner : seg_nodes) {
          if (partner == nullptr || partner == node || !partner->isStdCell()
              || partner->isFixed() || !arch_->isSingleHeightCell(partner)
              || partner->getWidth() != node->getWidth()
              || mgrPtr_->getNumReverseCellToSegs(partner->getId()) != 1) {
            continue;
          }
          ranked_neighbors.emplace_back(
              std::abs(partner->getCenterX().v - bbox_center), partner);
        }
        std::sort(ranked_neighbors.begin(),
                  ranked_neighbors.end(),
                  byPriority);
        if (ranked_neighbors.size() > kMaxNeighborsPerSeg) {
          ranked_neighbors.resize(kMaxNeighborsPerSeg);
        }
        for (const auto& [___, partner] : ranked_neighbors) {
          ClosureCandidate swap_candidate;
          swap_candidate.kind = ClosureCandidate::Kind::Swap;
          swap_candidate.target_seg
              = mgrPtr_->getReverseCellToSegs(partner->getId())[0]->getSegId();
          swap_candidate.target_x = partner->getLeft();
          swap_candidate.target_y = partner->getBottom();
          swap_candidate.cheap_score
              = std::abs(node->getCenterX().v - bbox_center)
                - std::abs(partner->getCenterX().v - bbox_center)
                - 0.05 * std::abs(partner->getLeft().v - node->getLeft().v)
                + 2.0;
          const uint64_t swap_key
              = (static_cast<uint64_t>(1) << 63)
                | (static_cast<uint64_t>(swap_candidate.target_seg & 0x7fffffff)
                   << 32)
                | static_cast<uint32_t>(swap_candidate.target_x.v);
          if (seen.insert(swap_key).second) {
            candidates.push_back(swap_candidate);
          }
        }
      }
    }

    if (candidates.empty()) {
      continue;
    }
    std::sort(candidates.begin(),
              candidates.end(),
              [](const ClosureCandidate& lhs, const ClosureCandidate& rhs) {
                return lhs.cheap_score > rhs.cheap_score;
              });
    if (candidates.size() > 12) {
      candidates.resize(12);
    }

    double best_delta = 0.0;
    int best_idx = -1;
    for (int idx = 0; idx < candidates.size(); idx++) {
      const ClosureCandidate& candidate = candidates[idx];
      const bool ok
          = candidate.kind == ClosureCandidate::Kind::Move
                ? mgrPtr_->tryMove(node,
                                   node->getLeft(),
                                   node->getBottom(),
                                   source_seg,
                                   candidate.target_x,
                                   candidate.target_y,
                                   candidate.target_seg)
                : mgrPtr_->trySwap(node,
                                   node->getLeft(),
                                   node->getBottom(),
                                   source_seg,
                                   candidate.target_x,
                                   candidate.target_y,
                                   candidate.target_seg);
      probes++;
      if (!ok) {
        continue;
      }
      exact_scored++;
      const double delta = hpwlObj.delta(mgrPtr_->getJournal());
      mgrPtr_->rejectMove();
      if (delta > best_delta) {
        best_delta = delta;
        best_idx = idx;
      }
    }

    if (best_idx >= 0 && best_delta > 0.0) {
      const ClosureCandidate& best = candidates[best_idx];
      const bool replay_ok
          = best.kind == ClosureCandidate::Kind::Move
                ? mgrPtr_->tryMove(node,
                                   node->getLeft(),
                                   node->getBottom(),
                                   source_seg,
                                   best.target_x,
                                   best.target_y,
                                   best.target_seg)
                : mgrPtr_->trySwap(node,
                                   node->getLeft(),
                                   node->getBottom(),
                                   source_seg,
                                   best.target_x,
                                   best.target_y,
                                   best.target_seg);
      if (replay_ok) {
        const auto& journal = mgrPtr_->getJournal();
        for (const auto& action_ptr : journal) {
          if (action_ptr == nullptr
              || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
            continue;
          }
          const auto* move_action
              = static_cast<const MoveCellAction*>(action_ptr.get());
          mgrPtr_->recordAcceptedMove(move_action->getNode(),
                                      move_action->getOrigSegs(),
                                      move_action->getNewSegs());
          queueNode(move_action->getNode());
        }
        hpwlObj.accept();
        mgrPtr_->acceptMove();
        accepts++;
        accepted_gain += best_delta;
        zero_gain_streak = 0;
      } else {
        mgrPtr_->rejectMove();
        zero_gain_streak++;
      }
    } else {
      zero_gain_streak++;
    }

    if (probes >= kMinProbesBeforeStop && zero_gain_streak >= kMaxZeroGainStreak) {
      break;
    }
  }

  const auto end = std::chrono::steady_clock::now();
  const double runtime_ms
      = std::chrono::duration<double, std::milli>(end - start).count();
  const double delta_per_runtime
      = runtime_ms > 0.0 ? accepted_gain / runtime_ms : 0.0;
  mgrPtr_->getLogger()->info(
      DPL,
      932,
      "Accepted-footprint closure frontier_nodes={} frontier_segments={} "
      "probes={} exact_scored={} accepts={} gain={:.2f} runtime_ms={:.1f} "
      "delta_per_runtime={:.2f}",
      frontier_nodes,
      frontier_segments,
      probes,
      exact_scored,
      accepts,
      accepted_gain,
      runtime_ms,
      delta_per_runtime);
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
