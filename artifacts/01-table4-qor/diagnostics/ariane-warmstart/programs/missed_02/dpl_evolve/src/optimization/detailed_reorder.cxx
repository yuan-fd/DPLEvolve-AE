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

  mgrPtr_->resortSegments();
  selected_reorder_frontier_ = static_cast<int>(mgrPtr_->getHotSegments().size());
  selected_reorder_windows_ = 0;
  selected_reorder_accepts_ = 0;
  selected_reorder_gain_ = 0.0;

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
  selected_reorder_gain_ = static_cast<double>(init_hpwl - curr_hpwl);
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
      306,
      "selected_reorder_frontier={} selected_reorder_windows={} "
      "selected_reorder_accepts={} selected_reorder_gain={:.0f}",
      selected_reorder_frontier_,
      selected_reorder_windows_,
      selected_reorder_accepts_,
      selected_reorder_gain_);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  std::vector<int> segment_order;
  segment_order.reserve(mgrPtr_->getNumSegments());
  for (const int seg : mgrPtr_->getHotSegments()) {
    if (seg >= 0 && seg < mgrPtr_->getNumSegments()) {
      segment_order.push_back(seg);
    }
  }
  for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
    if (std::find(segment_order.begin(), segment_order.end(), s)
        == segment_order.end()) {
      segment_order.push_back(s);
    }
  }

  // Loop over each segment; find single height cells and reorder.
  for (const int s : segment_order) {
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
        selected_reorder_windows_++;
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

  selected_reorder_accepts_++;
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
bool DetailedReorderer::buildWindowFromAcceptedNodes(int segId,
                                                     std::vector<Node*>& nodes,
                                                     int& jstrt,
                                                     int& jstop,
                                                     DbuX& leftLimit,
                                                     DbuX& rightLimit,
                                                     int& rowId)
{
  const auto& seg_nodes = mgrPtr_->getCellsInSeg(segId);
  if (seg_nodes.size() < 2) {
    return false;
  }
  nodes = seg_nodes;
  DetailedSeg* segPtr = mgrPtr_->getSegment(segId);
  rowId = segPtr->getRowId();
  leftLimit = segPtr->getMinX();
  rightLimit = segPtr->getMaxX();

  std::vector<int> accepted_indices;
  const auto& accepted = mgrPtr_->getAcceptedNodes();
  for (int i = 0; i < static_cast<int>(nodes.size()); i++) {
    if (std::find(accepted.begin(), accepted.end(), nodes[i]) != accepted.end()) {
      accepted_indices.push_back(i);
    }
  }
  if (accepted_indices.empty()) {
    return false;
  }
  const int center = accepted_indices.front();
  jstrt = std::max(0, center - 1);
  jstop = std::min(static_cast<int>(nodes.size()) - 1, center + 1);
  return jstop - jstrt + 1 >= 2;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::runExactLocalClosure(DetailedMgr* mgrPtr)
{
  mgrPtr_ = mgrPtr;
  mgrPtr_->resortSegments();
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);
  exact_closure_windows_ = 0;
  exact_closure_accepts_ = 0;
  exact_closure_gain_ = 0.0;

  uint64_t hpwl_x = 0;
  uint64_t hpwl_y = 0;
  const int64_t init_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  for (const int segId : mgrPtr_->getHotSegments()) {
    std::vector<Node*> nodes;
    int jstrt = 0;
    int jstop = 0;
    int rowId = 0;
    DbuX leftLimit{0};
    DbuX rightLimit{0};
    if (!buildWindowFromAcceptedNodes(
            segId, nodes, jstrt, jstop, leftLimit, rightLimit, rowId)) {
      continue;
    }
    exact_closure_windows_++;
    const double before = cost(nodes, jstrt, jstop);
    reorder(nodes, jstrt, jstop, leftLimit, rightLimit, segId, rowId);
    const double after = cost(nodes, jstrt, jstop);
    if (after + 1e-6 < before) {
      exact_closure_accepts_++;
    }
  }
  const int64_t final_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  exact_closure_gain_ = static_cast<double>(init_hpwl - final_hpwl);
  mgrPtr_->getLogger()->info(
      DPL,
      307,
      "exact_closure_windows={} exact_closure_accepts={} exact_closure_gain={:.0f}",
      exact_closure_windows_,
      exact_closure_accepts_,
      exact_closure_gain_);
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
