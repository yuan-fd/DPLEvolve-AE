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
#include <tuple>
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
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  auto& stats = mgrPtr_->getDetailedChainStats();
  const std::vector<int>& hot_segments = mgrPtr_->getHotSegments();
  const std::vector<Node*>& accepted_nodes = mgrPtr_->getAcceptedNodes();
  const std::vector<Node*>& frontier_nodes = mgrPtr_->getCriticalFrontier();
  const std::vector<Edge*>& touched_edges = mgrPtr_->getTouchedEdges();

  std::vector<int> selected_segments = hot_segments;
  stats.selected_reorder_frontier = selected_segments.size();
  reorderSegments(selected_segments,
                  windowSize_,
                  stats.selected_reorder_windows,
                  stats.selected_reorder_accepts,
                  stats.selected_reorder_gain);

  DetailedHPWL hpwl_obj(network_);
  hpwl_obj.init(mgrPtr_, nullptr);

  std::vector<Node*> critical_nodes = accepted_nodes;
  for (Node* node : frontier_nodes) {
    if (node == nullptr) {
      continue;
    }
    if (std::find(critical_nodes.begin(), critical_nodes.end(), node)
        == critical_nodes.end()) {
      critical_nodes.push_back(node);
    }
  }

  auto add_unique_row = [](std::vector<int>& rows, const int row_id) {
    if (row_id < 0) {
      return;
    }
    if (std::find(rows.begin(), rows.end(), row_id) == rows.end()) {
      rows.push_back(row_id);
    }
  };

  for (Node* node : accepted_nodes) {
    if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) == 0) {
      continue;
    }
    DetailedSeg* seg = mgrPtr_->getReverseCellToSegs(node->getId())[0];
    std::vector<int> rows;
    add_unique_row(rows, seg->getRowId());
    add_unique_row(rows, std::max(0, seg->getRowId() - 1));
    add_unique_row(
        rows, std::min(arch_->getNumRows() - 1, seg->getRowId() + 1));
    std::vector<DbuX> anchors = {
        node->getLeft() - arch_->getRow(0)->getSiteWidth(),
        node->getLeft(),
        node->getLeft() + arch_->getRow(0)->getSiteWidth()};
    attemptBestMove(node,
                    rows,
                    anchors,
                    hpwl_obj,
                    stats.critical_micro_start_probes,
                    stats.critical_micro_start_accepts,
                    stats.critical_micro_start_gain);
  }

  for (Edge* edge : touched_edges) {
    if (edge == nullptr || edge->getNumPins() <= 1
        || edge->getNumPins() >= skipNetsLargerThanThis_) {
      continue;
    }
    for (Pin* pin : edge->getPins()) {
      Node* node = pin->getNode();
      if (node == nullptr || node->isFixed() || !node->isStdCell()
          || mgrPtr_->getNumReverseCellToSegs(node->getId()) == 0) {
        continue;
      }
      odb::Rect bbox;
      if (!computeTargetBox(node, bbox)) {
        continue;
      }
      const int target_row = arch_->find_closest_row(
          DbuY{(bbox.yMin() + bbox.yMax()) / 2});
      std::vector<int> rows;
      add_unique_row(rows, target_row);
      add_unique_row(rows, std::max(0, target_row - 1));
      add_unique_row(rows, std::min(arch_->getNumRows() - 1, target_row + 1));
      std::vector<DbuX> anchors = {
          DbuX{bbox.xMin() - node->getWidth().v / 2},
          DbuX{((bbox.xMin() + bbox.xMax()) / 2) - node->getWidth().v / 2},
          DbuX{bbox.xMax() - node->getWidth().v / 2}};
      attemptBestMove(node,
                      rows,
                      anchors,
                      hpwl_obj,
                      stats.critical_net_chain_probes,
                      stats.critical_net_chain_accepts,
                      stats.critical_net_chain_gain);
    }
  }

  std::vector<int> exact_segments;
  for (Node* node : critical_nodes) {
    if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) == 0) {
      continue;
    }
    add_unique_row(exact_segments,
                   mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId());
  }
  reorderSegments(exact_segments,
                  4,
                  stats.exact_closure_windows,
                  stats.exact_closure_accepts,
                  stats.exact_closure_gain);

  for (Node* node : frontier_nodes) {
    if (node == nullptr || node->isFixed() || !node->isStdCell()
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) == 0) {
      continue;
    }
    DetailedSeg* seg = mgrPtr_->getReverseCellToSegs(node->getId())[0];
    std::vector<int> rows;
    add_unique_row(rows, seg->getRowId());
    add_unique_row(rows, std::max(0, seg->getRowId() - 1));
    add_unique_row(rows, std::min(arch_->getNumRows() - 1, seg->getRowId() + 1));
    std::vector<DbuX> anchors = {
        node->getLeft() - (2 * arch_->getRow(0)->getSiteWidth()),
        node->getLeft() + (2 * arch_->getRow(0)->getSiteWidth())};
    attemptBestMove(node,
                    rows,
                    anchors,
                    hpwl_obj,
                    stats.multi_row_residual_probes,
                    stats.multi_row_residual_accepts,
                    stats.multi_row_residual_gain);
  }

  reorderSegments(selected_segments,
                  2,
                  stats.segment_residual_swap_probes,
                  stats.segment_residual_swap_accepts,
                  stats.segment_residual_swap_gain);
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorderSegments(const std::vector<int>& segment_ids,
                                        const int window_size,
                                        int& windows,
                                        int& accepts,
                                        double& gain)
{
  const int saved_window = windowSize_;
  windowSize_ = std::min(4, std::max(2, window_size));

  for (const int seg_id : segment_ids) {
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
      continue;
    }
    DetailedSeg* segPtr = mgrPtr_->getSegment(seg_id);
    const int rowId = segPtr->getRowId();
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
    if (nodes.size() < 2) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(seg_id);

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

        windows++;
        const double delta
            = reorder(nodes, istrt, istop, leftLimit, rightLimit, seg_id, rowId);
        if (delta > 0.0) {
          accepts++;
          gain += delta;
        }
      }
    }
  }

  windowSize_ = saved_window;
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
double DetailedReorderer::reorder(const std::vector<Node*>& nodes,
                                  const int jstrt,
                                  const int jstop,
                                  const DbuX leftLimit,
                                  const DbuX rightLimit,
                                  const int segId,
                                  const int rowId)
{
  const int size = jstop - jstrt + 1;
  if (size <= 0) {
    return 0.0;
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
    return 0.0;
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
    return 0.0;
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
    for (size_t i = 0; i < size; i++) {
      Node* ndi = nodes[jstrt + i];
      mgrPtr_->eraseFromGrid(ndi);
      ndi->setLeft(origLeft[ndi]);
      mgrPtr_->paintInGrid(ndi);
    }
    return 0.0;
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
      for (int i = 0; i < size; i++) {
        Node* ndi = nodes[jstrt + i];
        mgrPtr_->eraseFromGrid(ndi);
        ndi->setLeft(origLeft[ndi]);
        mgrPtr_->paintInGrid(ndi);
      }
      mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
      return 0.0;
    }
  }

  return origCost - bestCost;
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

bool DetailedReorderer::computeTargetBox(Node* node, odb::Rect& bbox) const
{
  if (node == nullptr) {
    return false;
  }
  bbox.mergeInit();
  int samples = 0;
  for (Pin* pin : node->getPins()) {
    Edge* edge = pin->getEdge();
    if (edge == nullptr) {
      continue;
    }
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
      continue;
    }
    for (Pin* other_pin : edge->getPins()) {
      Node* other = other_pin->getNode();
      if (other == nullptr || other == node) {
        continue;
      }
      const DbuX x = other->getCenterX() + other_pin->getOffsetX();
      const DbuY y = other->getCenterY() + other_pin->getOffsetY();
      bbox.set_xlo(std::min(bbox.xMin(), x.v));
      bbox.set_xhi(std::max(bbox.xMax(), x.v));
      bbox.set_ylo(std::min(bbox.yMin(), y.v));
      bbox.set_yhi(std::max(bbox.yMax(), y.v));
      samples++;
    }
  }
  return samples > 0;
}

bool DetailedReorderer::attemptBestMove(Node* node,
                                        const std::vector<int>& row_ids,
                                        const std::vector<DbuX>& anchors,
                                        DetailedHPWL& hpwl_obj,
                                        int& probes,
                                        int& accepts,
                                        double& gain)
{
  if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
    return false;
  }

  DetailedSeg* current_seg = mgrPtr_->getReverseCellToSegs(node->getId())[0];
  double best_delta = 0.0;
  std::vector<MoveCellAction> best_actions;

  for (const int row_id : row_ids) {
    if (row_id < 0 || row_id >= arch_->getNumRows()) {
      continue;
    }
    for (DetailedSeg* seg : mgrPtr_->getSegsInRow(row_id)) {
      if (seg == nullptr || seg->getRegId() != node->getGroupId()) {
        continue;
      }
      const DbuX max_left = seg->getMaxX() - node->getWidth();
      if (max_left < seg->getMinX()) {
        continue;
      }
      for (const DbuX anchor : anchors) {
        probes++;
        const DbuX x = std::clamp(anchor, seg->getMinX(), max_left);
        const DbuY y = arch_->getRow(row_id)->getBottom();
        bool legal_probe = false;
        if (mgrPtr_->tryMove(node,
                             node->getLeft(),
                             node->getBottom(),
                             current_seg->getSegId(),
                             x,
                             y,
                             seg->getSegId())) {
          legal_probe = true;
        } else if (mgrPtr_->trySwap(node,
                                    node->getLeft(),
                                    node->getBottom(),
                                    current_seg->getSegId(),
                                    x,
                                    y,
                                    seg->getSegId())) {
          legal_probe = true;
        }
        if (!legal_probe) {
          continue;
        }

        std::vector<MoveCellAction> actions;
        for (const auto& action_ptr : mgrPtr_->getJournal()) {
          if (action_ptr == nullptr
              || action_ptr->typeId() != JournalActionTypeEnum::MOVE_CELL) {
            continue;
          }
          const auto* move_action
              = static_cast<const MoveCellAction*>(action_ptr.get());
          actions.emplace_back(move_action->getNode(),
                               move_action->getOrigLeft(),
                               move_action->getOrigBottom(),
                               move_action->getNewLeft(),
                               move_action->getNewBottom(),
                               move_action->wasPlaced(),
                               move_action->getOrigSegs(),
                               move_action->getNewSegs());
        }
        const double delta = hpwl_obj.delta(mgrPtr_->getJournal());
        mgrPtr_->rejectMove();

        if (delta > best_delta && !actions.empty()) {
          best_delta = delta;
          best_actions = std::move(actions);
        }
      }
    }
  }

  if (best_delta <= 0.0 || best_actions.empty()) {
    return false;
  }
  if (!mgrPtr_->replayMoveActions(best_actions)) {
    return false;
  }
  const double replay_delta = hpwl_obj.delta(mgrPtr_->getJournal());
  if (replay_delta <= 0.0) {
    mgrPtr_->rejectMove();
    return false;
  }

  accepts++;
  gain += replay_delta;
  mgrPtr_->noteAcceptedJournal(mgrPtr_->getJournal(), 0.0, false);
  hpwl_obj.accept();
  mgrPtr_->acceptMove();
  return true;
}

}  // namespace dpl_evolve
