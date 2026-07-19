// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <set>
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
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runSelectedSegments(DetailedMgr* mgrPtr,
                                            const std::vector<int>& segment_ids,
                                            const int window_size,
                                            const int window_cap,
                                            const int accept_stop,
                                            int& scored_windows,
                                            int& accepted_windows,
                                            double& accepted_hpwl_gain)
{
  mgrPtr_ = mgrPtr;
  windowSize_ = std::min(4, std::max(2, window_size));
  mgrPtr_->resortSegments();
  reorder(segment_ids,
          window_cap,
          accept_stop,
          scored_windows,
          accepted_windows,
          accepted_hpwl_gain);
  mgrPtr_->resortSegments();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runSelectedSegmentTransactions(
    DetailedMgr* mgrPtr,
    const std::vector<int>& segment_ids,
    const std::vector<int>& anchor_cell_ids,
    const int segment_cap,
    const int transaction_cap,
    const int accept_stop,
    SelectedSegmentTxnStats& stats)
{
  mgrPtr_ = mgrPtr;
  mgrPtr_->resortSegments();

  stats = {};
  if (segment_cap <= 0 || transaction_cap <= 0 || accept_stop <= 0) {
    return;
  }

  const int capped_segments
      = std::min(segment_cap, static_cast<int>(segment_ids.size()));
  const int pair_cap = std::max(1, transaction_cap * 3 / 4);
  const int cycle_cap = std::max(1, transaction_cap - pair_cap);

  for (int seg_index = 0; seg_index < capped_segments
                          && stats.scored_transactions < transaction_cap
                          && stats.accepted_transactions < accept_stop;
       ++seg_index) {
    const int seg_id = segment_ids[seg_index];
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
      continue;
    }

    DetailedSeg* seg_ptr = mgrPtr_->getSegment(seg_id);
    if (seg_ptr == nullptr) {
      continue;
    }
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
    if (nodes.size() < 2) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(seg_id);

    std::vector<int> anchor_indices;
    anchor_indices.reserve(anchor_cell_ids.size());
    for (const int node_id : anchor_cell_ids) {
      if (node_id < 0 || node_id >= network_->getNumNodes()) {
        continue;
      }
      Node* node = network_->getNode(node_id);
      if (node == nullptr || findNodeSegment(node) != seg_id) {
        continue;
      }
      const auto it = std::find(nodes.begin(), nodes.end(), node);
      if (it == nodes.end()) {
        continue;
      }
      anchor_indices.push_back(static_cast<int>(it - nodes.begin()));
    }
    if (anchor_indices.empty()) {
      continue;
    }
    ++stats.segments;
    stats.anchor_cells += static_cast<int>(anchor_indices.size());

    std::vector<std::vector<TxnMove>> txn_moves;
    buildPairTransactionCandidates(
        nodes, anchor_indices, pair_cap, txn_moves, stats.pair_candidates);
    buildCycleTransactionCandidates(
        nodes, anchor_indices, cycle_cap, txn_moves, stats.cycle_candidates);

    for (const auto& moves : txn_moves) {
      if (stats.scored_transactions >= transaction_cap
          || stats.accepted_transactions >= accept_stop) {
        break;
      }
      evaluateTransaction(moves, seg_id, seg_ptr->getRowId(), stats);
    }
  }

  mgrPtr_->resortSegments();
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  // Loop over each segment; find single height cells and reorder.
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

      // Single height cells in [jstrt,jstop].
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
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder(const std::vector<int>& segment_ids,
                                const int window_cap,
                                const int accept_stop,
                                int& scored_windows,
                                int& accepted_windows,
                                double& accepted_hpwl_gain)
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  scored_windows = 0;
  accepted_windows = 0;
  accepted_hpwl_gain = 0.0;

  for (const int segId : segment_ids) {
    if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
      continue;
    }
    DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
    if (segPtr == nullptr) {
      continue;
    }
    const int rowId = segPtr->getRowId();
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(segId);
    if (nodes.size() < 2) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(segId);

    int j = 0;
    const int n = static_cast<int>(nodes.size());
    while (j < n && accepted_windows < accept_stop
           && scored_windows < window_cap) {
      while (j < n && arch_->isMultiHeightCell(nodes[j])) {
        ++j;
      }
      const int jstrt = j;
      while (j < n && arch_->isSingleHeightCell(nodes[j])) {
        ++j;
      }
      const int jstop = j - 1;

      for (int i = jstrt; i + windowSize_ <= jstop && accepted_windows < accept_stop
                      && scored_windows < window_cap;
           ++i) {
        int istrt = i;
        const int istop = std::min(jstop, istrt + windowSize_ - 1);
        if (istop == jstop) {
          istrt = std::max(jstrt, istop - windowSize_ + 1);
        }

        const Node* nextPtr = (istop != n - 1) ? nodes[istop + 1] : nullptr;
        DbuX rightLimit{segPtr->getMaxX()};
        if (nextPtr != nullptr) {
          int leftPadding = 0;
          int rightPadding = 0;
          arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
          rightLimit = std::min((nextPtr->getLeft() - leftPadding), rightLimit);
        }
        const Node* prevPtr = (istrt != 0) ? nodes[istrt - 1] : nullptr;
        DbuX leftLimit{segPtr->getMinX()};
        if (prevPtr != nullptr) {
          int leftPadding = 0;
          int rightPadding = 0;
          arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
          leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
        }

        std::unordered_map<Node*, DbuX> origLeft;
        origLeft.reserve(static_cast<size_t>(istop - istrt + 1));
        for (int window_idx = istrt; window_idx <= istop; ++window_idx) {
          Node* node = nodes[window_idx];
          origLeft[node] = node->getLeft();
        }

        const double before_hpwl = exactWindowHpwl(nodes, istrt, istop);
        reorder(nodes, istrt, istop, leftLimit, rightLimit, segId, rowId);
        const double after_hpwl = exactWindowHpwl(nodes, istrt, istop);
        ++scored_windows;
        if (after_hpwl + 1e-3 < before_hpwl) {
          ++accepted_windows;
          accepted_hpwl_gain += before_hpwl - after_hpwl;
        } else {
          for (int window_idx = istrt; window_idx <= istop; ++window_idx) {
            Node* node = nodes[window_idx];
            mgrPtr_->eraseFromGrid(node);
            node->setLeft(origLeft[node]);
            mgrPtr_->paintInGrid(node);
          }
          mgrPtr_->sortCellsInSeg(segId);
        }
      }
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
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedReorderer::exactWindowHpwl(const std::vector<Node*>& nodes,
                                          const int istrt,
                                          const int istop)
{
  std::set<const Edge*> edges;
  for (int i = istrt; i <= istop; ++i) {
    const Node* ndi = nodes[i];
    if (ndi == nullptr) {
      continue;
    }
    for (const Pin* pin : ndi->getPins()) {
      const Edge* edge = pin->getEdge();
      if (edge == nullptr) {
        continue;
      }
      const int npins = edge->getNumPins();
      if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
        continue;
      }
      edges.insert(edge);
    }
  }

  double hpwl = 0.0;
  for (const Edge* edge : edges) {
    hpwl += static_cast<double>(edge->hpwl());
  }
  return hpwl;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
int DetailedReorderer::findNodeSegment(const Node* node) const
{
  if (node == nullptr) {
    return -1;
  }
  const auto& segs = mgrPtr_->getReverseCellToSegs(node->getId());
  if (segs.empty() || segs.front() == nullptr) {
    return -1;
  }
  return segs.front()->getSegId();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::applyTransactionMoves(const std::vector<TxnMove>& moves,
                                              const int seg_id,
                                              const int /*row_id*/)
{
  if (moves.empty()) {
    return false;
  }

  DetailedSeg* seg_ptr = mgrPtr_->getSegment(seg_id);
  if (seg_ptr == nullptr) {
    return false;
  }

  std::vector<DbuX> original_lefts;
  original_lefts.reserve(moves.size());
  for (const auto& move : moves) {
    if (move.node == nullptr) {
      return false;
    }
    original_lefts.push_back(move.node->getLeft());
    const DbuX dx = abs(move.new_left - move.node->getOrigLeft());
    if (dx > mgrPtr_->getMaxDisplacementX()) {
      return false;
    }
  }

  for (const auto& move : moves) {
    mgrPtr_->eraseFromGrid(move.node);
    move.node->setLeft(move.new_left);
    mgrPtr_->paintInGrid(move.node);
  }
  mgrPtr_->sortCellsInSeg(seg_id);

  bool failed = false;
  DbuX last_right = seg_ptr->getMinX();
  for (const Node* node : mgrPtr_->getCellsInSeg(seg_id)) {
    if (node == nullptr) {
      failed = true;
      break;
    }
    if (node->getLeft() < last_right || mgrPtr_->hasPlacementViolation(node)) {
      failed = true;
      break;
    }
    last_right = node->getRight();
  }

  if (failed) {
    for (size_t idx = 0; idx < moves.size(); ++idx) {
      const auto& move = moves[idx];
      mgrPtr_->eraseFromGrid(move.node);
      move.node->setLeft(original_lefts[idx]);
      mgrPtr_->paintInGrid(move.node);
    }
    mgrPtr_->sortCellsInSeg(seg_id);
    return false;
  }

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedReorderer::evaluateTransaction(const std::vector<TxnMove>& moves,
                                            const int seg_id,
                                            const int row_id,
                                            SelectedSegmentTxnStats& stats)
{
  std::set<const Edge*> edges;
  for (const auto& move : moves) {
    if (move.node == nullptr) {
      return false;
    }
    for (const Pin* pin : move.node->getPins()) {
      const Edge* edge = pin->getEdge();
      if (edge == nullptr) {
        continue;
      }
      const int npins = edge->getNumPins();
      if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
        continue;
      }
      edges.insert(edge);
    }
  }

  double before_hpwl = 0.0;
  for (const Edge* edge : edges) {
    before_hpwl += static_cast<double>(edge->hpwl());
  }

  std::vector<DbuX> original_lefts;
  original_lefts.reserve(moves.size());
  for (const auto& move : moves) {
    original_lefts.push_back(move.node->getLeft());
  }

  if (!applyTransactionMoves(moves, seg_id, row_id)) {
    return false;
  }

  ++stats.scored_transactions;
  double after_hpwl = 0.0;
  for (const Edge* edge : edges) {
    after_hpwl += static_cast<double>(edge->hpwl());
  }
  if (after_hpwl + 1e-3 < before_hpwl) {
    ++stats.accepted_transactions;
    stats.accepted_hpwl_gain += before_hpwl - after_hpwl;
    return true;
  }

  for (size_t idx = 0; idx < moves.size(); ++idx) {
    const auto& move = moves[idx];
    mgrPtr_->eraseFromGrid(move.node);
    move.node->setLeft(original_lefts[idx]);
    mgrPtr_->paintInGrid(move.node);
  }
  mgrPtr_->sortCellsInSeg(seg_id);
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::buildPairTransactionCandidates(
    const std::vector<Node*>& nodes,
    const std::vector<int>& anchor_indices,
    const int pair_cap,
    std::vector<std::vector<TxnMove>>& moves,
    int& pair_candidates)
{
  const int n = static_cast<int>(nodes.size());
  for (const int anchor_idx : anchor_indices) {
    if (anchor_idx < 0 || anchor_idx >= n || pair_candidates >= pair_cap) {
      continue;
    }
    Node* anchor = nodes[anchor_idx];
    if (anchor == nullptr) {
      continue;
    }
    const int seg_id = findNodeSegment(anchor);
    if (seg_id < 0) {
      continue;
    }
    const int left = std::max(0, anchor_idx - 3);
    const int right = std::min(n - 1, anchor_idx + 3);
    for (int other_idx = left; other_idx <= right && pair_candidates < pair_cap;
         ++other_idx) {
      if (other_idx == anchor_idx) {
        continue;
      }
      Node* other = nodes[other_idx];
      if (other == nullptr || other->getWidth() != anchor->getWidth()) {
        continue;
      }
      std::vector<TxnMove> txn;
      txn.push_back(
          TxnMove{anchor, anchor->getLeft(), other->getLeft()});
      txn.push_back(
          TxnMove{other, other->getLeft(), anchor->getLeft()});
      moves.push_back(std::move(txn));
      ++pair_candidates;
    }
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::buildCycleTransactionCandidates(
    const std::vector<Node*>& nodes,
    const std::vector<int>& anchor_indices,
    const int cycle_cap,
    std::vector<std::vector<TxnMove>>& moves,
    int& cycle_candidates)
{
  const int n = static_cast<int>(nodes.size());
  for (const int anchor_idx : anchor_indices) {
    if (anchor_idx < 0 || anchor_idx >= n || cycle_candidates >= cycle_cap) {
      continue;
    }
    Node* first = nodes[anchor_idx];
    if (first == nullptr) {
      continue;
    }
    const int seg_id = findNodeSegment(first);
    if (seg_id < 0) {
      continue;
    }
    for (int offset = 1; offset <= 2 && cycle_candidates < cycle_cap; ++offset) {
      const int idx_b = anchor_idx + offset;
      const int idx_c = anchor_idx + offset + 1;
      if (idx_c >= n) {
        break;
      }
      Node* second = nodes[idx_b];
      Node* third = nodes[idx_c];
      if (second == nullptr || third == nullptr) {
        continue;
      }
      if (first->getWidth() != second->getWidth()
          || second->getWidth() != third->getWidth()) {
        continue;
      }
      std::vector<TxnMove> txn;
      txn.push_back(TxnMove{first, first->getLeft(), second->getLeft()});
      txn.push_back(TxnMove{second, second->getLeft(), third->getLeft()});
      txn.push_back(TxnMove{third, third->getLeft(), first->getLeft()});
      moves.push_back(std::move(txn));
      ++cycle_candidates;
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
