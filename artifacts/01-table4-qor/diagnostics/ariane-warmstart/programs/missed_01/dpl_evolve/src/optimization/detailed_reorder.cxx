// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <set>
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
  selected_only_ = !mgrPtr_->getHotSegments().empty();
  resetConsumerStats();

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
  logStats("selected_reorder", selected_reorder_stats_);
  if (selected_only_) {
    criticalRowMicroStart();
    criticalNetChainAssignment();
    exactLocalClosure();
    multiRowResidualTransactions();
    segmentLocalResidualSwaps();
    logStats("critical_micro_start", critical_micro_stats_);
    logStats("critical_net_chain", critical_net_chain_stats_);
    logStats("exact_closure", exact_closure_stats_);
    logStats("multi_row_residual", multi_row_residual_stats_);
    logStats("segment_residual_swap", segment_residual_swap_stats_);
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  std::set<int> selected_segments;
  if (selected_only_) {
    const auto& hot_segments = mgrPtr_->getHotSegments();
    selected_segments.insert(hot_segments.begin(), hot_segments.end());
  }

  // Loop over each segment; find single height cells and reorder.
  for (int s = 0; s < mgrPtr_->getNumSegments(); s++) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(s);
    const int segId = segPtr->getSegId();
    if (selected_only_ && selected_segments.find(segId) == selected_segments.end()) {
      continue;
    }
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
  const double gain = origCost - bestCost;
  for (int i = 0; i < size; i++) {
    Node* ndi = nodes[jstrt + i];
    mgrPtr_->eraseFromGrid(ndi);
    ndi->setLeft(bestPosn[i]);
    mgrPtr_->paintInGrid(ndi);
  }

  // Need to resort.
  mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
  if (gain > 0.0) {
    selected_reorder_stats_.accepts++;
    selected_reorder_stats_.gain += gain;
  }
  selected_reorder_stats_.probes++;

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

void DetailedReorderer::resetConsumerStats()
{
  selected_reorder_stats_ = ConsumerStats{};
  critical_micro_stats_ = ConsumerStats{};
  critical_net_chain_stats_ = ConsumerStats{};
  exact_closure_stats_ = ConsumerStats{};
  multi_row_residual_stats_ = ConsumerStats{};
  segment_residual_swap_stats_ = ConsumerStats{};
}

bool DetailedReorderer::tryFrontierMove(Node* node,
                                        DetailedHPWL& hpwl_obj,
                                        ConsumerStats& stats,
                                        const std::vector<int>& rows,
                                        const std::vector<DbuX>& x_targets)
{
  if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
    return false;
  }

  const int src_seg = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
  const DbuX src_x = node->getLeft();
  const DbuY src_y = node->getBottom();
  bool accepted = false;
  for (const int row_id : rows) {
    if (row_id < 0 || row_id >= arch_->getNumRows()) {
      continue;
    }
    const DbuY row_y{arch_->getRow(row_id)->getBottom()};
    for (int s = 0; s < mgrPtr_->getNumSegsInRow(row_id); s++) {
      const DetailedSeg* seg_ptr = mgrPtr_->getSegsInRow(row_id)[s];
      if (seg_ptr->getRegId() != node->getGroupId()) {
        continue;
      }
      const int seg_id = seg_ptr->getSegId();
      const DbuX min_left = seg_ptr->getMinX();
      const DbuX max_left = seg_ptr->getMaxX() - node->getWidth();
      if (max_left < min_left) {
        continue;
      }
      for (const DbuX x_target : x_targets) {
        const DbuX clipped_x = std::max(min_left, std::min(x_target, max_left));
        if (seg_id == src_seg && abs(clipped_x - src_x) == 0) {
          continue;
        }
        stats.probes++;
        bool moved
            = mgrPtr_->tryMove(node, src_x, src_y, src_seg, clipped_x, row_y, seg_id);
        if (!moved) {
          moved = mgrPtr_->trySwap(
              node, src_x, src_y, src_seg, clipped_x, row_y, seg_id);
        }
        if (!moved) {
          continue;
        }
        const double delta = hpwl_obj.delta(mgrPtr_->getJournal());
        if (delta > 0.0) {
          hpwl_obj.accept();
          mgrPtr_->acceptMove();
          stats.accepts++;
          stats.gain += delta;
          accepted = true;
          break;
        }
        mgrPtr_->rejectMove();
      }
      if (accepted) {
        break;
      }
    }
    if (accepted) {
      break;
    }
  }
  return accepted;
}

void DetailedReorderer::criticalRowMicroStart()
{
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);
  for (Node* node : mgrPtr_->getCriticalFrontier()) {
    if (node == nullptr) {
      continue;
    }
    const int row = arch_->find_closest_row(node->getBottom());
    const std::vector<int> rows{row - 1, row, row + 1};
    const std::vector<DbuX> x_targets{
        node->getLeft() - node->siteWidth() * 2,
        node->getLeft() + node->siteWidth() * 2};
    tryFrontierMove(node, hpwl_obj, critical_micro_stats_, rows, x_targets);
  }
}

void DetailedReorderer::criticalNetChainAssignment()
{
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);
  for (Node* node : mgrPtr_->getCriticalFrontier()) {
    if (node == nullptr) {
      continue;
    }
    const std::vector<int> rows{
        arch_->find_closest_row(node->getBottom()),
        arch_->find_closest_row(node->getBottom()) + 1};
    std::vector<DbuX> x_targets;
    for (const Pin* pin : node->getPins()) {
      if (pin == nullptr || pin->getEdge() == nullptr) {
        continue;
      }
      x_targets.push_back(node->getCenterX() + pin->getOffsetX() - node->getWidth() / 2);
    }
    if (x_targets.empty()) {
      x_targets.push_back(node->getLeft());
    }
    tryFrontierMove(node, hpwl_obj, critical_net_chain_stats_, rows, x_targets);
  }
}

void DetailedReorderer::exactLocalClosure()
{
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);
  for (Node* node : mgrPtr_->getAcceptedNodes()) {
    if (node == nullptr) {
      continue;
    }
    const int row = arch_->find_closest_row(node->getBottom());
    const std::vector<int> rows{row};
    const std::vector<DbuX> x_targets{
        node->getLeft() - node->siteWidth(),
        node->getLeft() + node->siteWidth()};
    tryFrontierMove(node, hpwl_obj, exact_closure_stats_, rows, x_targets);
  }
}

void DetailedReorderer::multiRowResidualTransactions()
{
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);
  for (Node* node : mgrPtr_->getCriticalFrontier()) {
    if (node == nullptr) {
      continue;
    }
    const int row = arch_->find_closest_row(node->getBottom());
    const std::vector<int> rows{row - 2, row - 1, row + 1, row + 2};
    const std::vector<DbuX> x_targets{node->getLeft()};
    tryFrontierMove(
        node, hpwl_obj, multi_row_residual_stats_, rows, x_targets);
  }
}

void DetailedReorderer::segmentLocalResidualSwaps()
{
  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);
  for (Node* node : mgrPtr_->getAcceptedNodes()) {
    if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    const int row_id = mgrPtr_->getSegment(seg_id)->getRowId();
    const std::vector<int> rows{row_id};
    const std::vector<DbuX> x_targets{
        node->getLeft() - node->getWidth(),
        node->getLeft() + node->getWidth()};
    tryFrontierMove(
        node, hpwl_obj, segment_residual_swap_stats_, rows, x_targets);
  }
}

void DetailedReorderer::logStats(const char* label,
                                 const ConsumerStats& stats) const
{
  mgrPtr_->getLogger()->info(DPL,
                             342,
                             "{} probes={} accepts={} gain={:.2f}",
                             label,
                             stats.probes,
                             stats.accepts,
                             stats.gain);
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
