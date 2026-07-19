// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_reorder.h"

#include <algorithm>
#include <array>
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
#include "odb/geom.h"
#include "util/utility.h"
#include "utl/Logger.h"

using utl::DPL;

namespace dpl_evolve {
namespace {
std::vector<Node*> mergeFrontierNodes(const std::vector<Node*>& primary,
                                      const std::vector<Node*>& secondary)
{
  if (primary.empty()) {
    return secondary;
  }
  if (secondary.empty()) {
    return primary;
  }

  std::vector<Node*> merged;
  merged.reserve(primary.size() + secondary.size());
  std::unordered_set<int> seen_ids;
  for (Node* node : primary) {
    if (node == nullptr || !seen_ids.insert(node->getId()).second) {
      continue;
    }
    merged.push_back(node);
  }
  for (Node* node : secondary) {
    if (node == nullptr || !seen_ids.insert(node->getId()).second) {
      continue;
    }
    merged.push_back(node);
  }
  return merged;
}

bool calculateEdgeBB(Edge* edge, const Node* seed, odb::Rect& bbox)
{
  DbuX curX;
  DbuY curY;

  bbox.mergeInit();

  int count = 0;
  for (Pin* pin : edge->getPins()) {
    const Node* other = pin->getNode();
    if (other == seed) {
      continue;
    }
    curX = other->getCenterX() + pin->getOffsetX().v;
    curY = other->getCenterY() + pin->getOffsetY().v;

    bbox.set_xlo(std::min(curX.v, bbox.xMin()));
    bbox.set_xhi(std::max(curX.v, bbox.xMax()));
    bbox.set_ylo(std::min(curY.v, bbox.yMin()));
    bbox.set_yhi(std::max(curY.v, bbox.yMax()));

    ++count;
  }

  return count != 0;
}

bool getMedianRange(Architecture* arch,
                    const Node* node,
                    const int skip_nets_larger_than_this,
                    odb::Rect& node_bbox)
{
  DbuX xmin = arch->getMinX();
  DbuX xmax = arch->getMaxX();
  DbuY ymin = arch->getMinY();
  DbuY ymax = arch->getMaxY();

  std::vector<int> xpts;
  std::vector<int> ypts;
  xpts.reserve(node->getNumPins() * 2);
  ypts.reserve(node->getNumPins() * 2);

  unsigned t = 0;
  for (int n = 0; n < node->getNumPins(); n++) {
    Pin* pin = node->getPins()[n];
    Edge* edge = pin->getEdge();

    node_bbox.mergeInit();

    const int numPins = edge->getNumPins();
    if (numPins <= 1 || numPins > skip_nets_larger_than_this) {
      continue;
    }
    if (!calculateEdgeBB(edge, node, node_bbox)) {
      continue;
    }

    node_bbox.set_xlo(std::min(
        std::max(xmin.v, node_bbox.xMin() - pin->getOffsetX().v), xmax.v));
    node_bbox.set_xhi(std::max(
        std::min(xmax.v, node_bbox.xMax() - pin->getOffsetX().v), xmin.v));
    node_bbox.set_ylo(std::min(
        std::max(ymin.v, node_bbox.yMin() - pin->getOffsetY().v), ymax.v));
    node_bbox.set_yhi(std::max(
        std::min(ymax.v, node_bbox.yMax() - pin->getOffsetY().v), ymin.v));

    xpts.push_back(node_bbox.xMin());
    xpts.push_back(node_bbox.xMax());
    ypts.push_back(node_bbox.yMin());
    ypts.push_back(node_bbox.yMax());

    ++t;
    ++t;
  }

  if (t <= 1) {
    return false;
  }

  const unsigned mid = t >> 1;
  std::ranges::sort(xpts);
  std::ranges::sort(ypts);
  node_bbox.set_xlo(xpts[mid - 1]);
  node_bbox.set_xhi(xpts[mid]);
  node_bbox.set_ylo(ypts[mid - 1]);
  node_bbox.set_yhi(ypts[mid]);
  return true;
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
  exactWindowMode_ = false;

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

  segmentOrder_.clear();
  focusedSegments_.assign(mgrPtr_->getNumSegments(), 0);
  focusedSegmentCount_ = 0;
  focusedWindowCount_ = 0;
  focusedAcceptCount_ = 0;
  const auto& hotSegments = mgrPtr_->getHotSegments();
  if (!hotSegments.empty()) {
    focusedSegmentCount_ = hotSegments.size();
    for (const int segId : hotSegments) {
      if (segId < 0 || segId >= mgrPtr_->getNumSegments()) {
        continue;
      }
      if (focusedSegments_[segId] != 0) {
        continue;
      }
      focusedSegments_[segId] = 1;
      segmentOrder_.push_back(segId);
    }
    for (int segId = 0; segId < mgrPtr_->getNumSegments(); segId++) {
      if (focusedSegments_[segId] == 0) {
        segmentOrder_.push_back(segId);
      }
    }
    mgrPtr_->getLogger()->info(DPL,
                               326,
                               "Selected-segment reorder frontier: {} segments",
                               focusedSegmentCount_);
  } else {
    segmentOrder_.reserve(mgrPtr_->getNumSegments());
    for (int segId = 0; segId < mgrPtr_->getNumSegments(); segId++) {
      segmentOrder_.push_back(segId);
    }
  }

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
  if (focusedSegmentCount_ > 0) {
    mgrPtr_->getLogger()->info(
        DPL,
        327,
        "Selected-segment reorder results: windows={}, accepts={}",
        focusedWindowCount_,
        focusedAcceptCount_);
  }
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
void DetailedReorderer::exactLocalClosure(
    DetailedMgr* mgrPtr,
    const std::vector<Node*>& frontier_nodes,
    ExactClosureStats& stats,
    const Opendp::FrontierAttributionStats* frontier_attribution)
{
  mgrPtr_ = mgrPtr;
  windowSize_ = 4;
  exactWindowMode_ = true;
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  struct RankedFrontierNode
  {
    Node* node = nullptr;
    int64_t displacement = 0;
  };
  struct WindowCandidate
  {
    int seg_id = -1;
    int row_id = -1;
    int start = 0;
    int stop = 0;
    double priority = 0.0;
  };

  const auto& hot_segments = mgrPtr_->getHotSegments();
  const auto& critical_frontier_nodes = mgrPtr_->getCriticalNetFrontierNodes();
  std::vector<unsigned char> micro_start_segment_mask(mgrPtr_->getNumSegments(), 0);
  if (frontier_attribution != nullptr) {
    stats.base_frontier_nodes = frontier_attribution->base_frontier_nodes;
    stats.critical_frontier_nodes = frontier_attribution->critical_frontier_nodes;
    stats.merged_frontier_nodes = frontier_attribution->merged_frontier_nodes;
    stats.micro_start_added_nodes = frontier_attribution->micro_start_added_nodes;
    stats.base_frontier_segments = frontier_attribution->base_frontier_segments;
    stats.critical_frontier_segments
        = frontier_attribution->critical_frontier_segments;
    stats.merged_frontier_segments = frontier_attribution->merged_frontier_segments;
    stats.micro_start_added_segments
        = frontier_attribution->micro_start_added_segments;

    std::unordered_set<int> critical_node_ids;
    critical_node_ids.reserve(critical_frontier_nodes.size());
    for (Node* node : critical_frontier_nodes) {
      if (node != nullptr) {
        critical_node_ids.insert(node->getId());
      }
    }
    for (Node* node : frontier_nodes) {
      if (node == nullptr
          || critical_node_ids.find(node->getId()) != critical_node_ids.end()
          || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
        continue;
      }
      const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
      if (seg_id >= 0 && seg_id < mgrPtr_->getNumSegments()) {
        micro_start_segment_mask[seg_id] = 1;
      }
    }
  }
  std::vector<unsigned char> hot_segment_mask(mgrPtr_->getNumSegments(), 0);
  for (const int seg_id : hot_segments) {
    if (seg_id >= 0 && seg_id < mgrPtr_->getNumSegments()) {
      hot_segment_mask[seg_id] = 1;
    }
  }

  const std::vector<Node*> active_frontier_nodes
      = mergeFrontierNodes(critical_frontier_nodes, frontier_nodes);
  std::vector<RankedFrontierNode> ranked_frontier;
  ranked_frontier.reserve(active_frontier_nodes.size());
  std::vector<unsigned char> seen_segments(mgrPtr_->getNumSegments(), 0);
  for (Node* node : active_frontier_nodes) {
    if (node == nullptr || arch_->isMultiHeightCell(node)
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
    const DbuY dy = abs(node->getBottom() - node->getOrigBottom());
    const int64_t displacement = dx.v + dy.v;
    if (displacement == 0) {
      continue;
    }
    const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    if (!hot_segments.empty() && !hot_segment_mask[seg_id]) {
      continue;
    }
    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
    ranked_frontier.push_back({node, displacement});
  }
  stats.frontier_cells = ranked_frontier.size();
  if (ranked_frontier.empty() && hot_segments.empty()) {
    return;
  }

  std::unordered_map<uint64_t, WindowCandidate> windows;
  windows.reserve(std::max<size_t>(ranked_frontier.size() * 2,
                                   hot_segments.size() * 4));
  auto make_key = [](const int seg_id, const int start, const int stop) {
    return (static_cast<uint64_t>(seg_id) << 40)
           | (static_cast<uint64_t>(start) << 20) | static_cast<uint64_t>(stop);
  };

  auto add_window_candidates = [&](const int seg_id,
                                   const int row_id,
                                   const int idx,
                                   const int node_count,
                                   const double priority_base) {
    const int window_size = std::min(windowSize_, node_count);
    const int start_lo = std::max(0, idx - window_size + 1);
    const int start_hi = std::min(idx, node_count - window_size);
    for (int start = start_lo; start <= start_hi; ++start) {
      const int stop = start + window_size - 1;
      const uint64_t key = make_key(seg_id, start, stop);
      auto [map_it, inserted] = windows.try_emplace(
          key, WindowCandidate{seg_id, row_id, start, stop, 0.0});
      const bool touches_edge = start == 0 || stop == node_count - 1;
      const double priority
          = priority_base * (touches_edge ? 1.75 : 1.0);
      if (inserted || priority > map_it->second.priority) {
        map_it->second.priority = priority;
      }
    }
  };

  std::ranges::sort(
      ranked_frontier,
      [](const RankedFrontierNode& lhs, const RankedFrontierNode& rhs) {
        if (lhs.displacement != rhs.displacement) {
          return lhs.displacement > rhs.displacement;
        }
        return lhs.node->getId() < rhs.node->getId();
      });

  for (const auto& frontier : ranked_frontier) {
    Node* node = frontier.node;
    const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    const int row_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getRowId();
    mgrPtr_->sortCellsInSeg(seg_id);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
    if (nodes.size() < 2) {
      continue;
    }
    const auto it = std::find(nodes.begin(), nodes.end(), node);
    if (it == nodes.end()) {
      continue;
    }
    const int idx = static_cast<int>(it - nodes.begin());
    add_window_candidates(
        seg_id, row_id, idx, static_cast<int>(nodes.size()), frontier.displacement);
  }

  if (critical_frontier_nodes.empty()) {
    for (const int seg_id : hot_segments) {
      if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
        continue;
      }
      mgrPtr_->sortCellsInSeg(seg_id);
      const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
      if (nodes.size() < 2) {
        continue;
      }
      const int row_id = mgrPtr_->getSegment(seg_id)->getRowId();
      if (!seen_segments[seg_id]) {
        seen_segments[seg_id] = 1;
        ++stats.frontier_segments;
      }
      const int window_size = std::min(windowSize_, static_cast<int>(nodes.size()));
      const int seg_windows
          = std::max(1, static_cast<int>(nodes.size()) - window_size + 1);
      for (int start = 0; start < seg_windows; ++start) {
        const int stop = start + window_size - 1;
        const uint64_t key = make_key(seg_id, start, stop);
        auto [map_it, inserted] = windows.try_emplace(
            key, WindowCandidate{seg_id, row_id, start, stop, 1.0});
        if (inserted || map_it->second.priority < 1.0) {
          map_it->second.priority = 1.0;
        }
      }
    }
  }

  std::vector<unsigned char> critical_segment_mask(mgrPtr_->getNumSegments(), 0);
  for (const auto& window : windows) {
    if (window.second.seg_id >= 0
        && window.second.seg_id < mgrPtr_->getNumSegments()) {
      critical_segment_mask[window.second.seg_id] = 1;
    }
  }

  for (const int seg_id : hot_segments) {
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()) {
      continue;
    }
    if (!critical_frontier_nodes.empty() && critical_segment_mask[seg_id] == 0) {
      continue;
    }
    mgrPtr_->sortCellsInSeg(seg_id);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(seg_id);
    if (nodes.size() < 2) {
      continue;
    }
    const int row_id = mgrPtr_->getSegment(seg_id)->getRowId();
    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
    const int window_size = std::min(windowSize_, static_cast<int>(nodes.size()));
    const int edge_window_count = std::min(2, std::max(1, static_cast<int>(nodes.size()) - window_size + 1));
    for (int edge_slot = 0; edge_slot < edge_window_count; ++edge_slot) {
      const int start = (edge_slot == 0) ? 0
                                         : std::max(0, static_cast<int>(nodes.size()) - window_size);
      const int stop = start + window_size - 1;
      const uint64_t key = make_key(seg_id, start, stop);
      auto [map_it, inserted] = windows.try_emplace(
          key, WindowCandidate{seg_id, row_id, start, stop, 0.5});
      if (inserted || map_it->second.priority < 0.5) {
        map_it->second.priority = 0.5;
      }
    }
  }

  std::vector<WindowCandidate> ordered_windows;
  ordered_windows.reserve(windows.size());
  for (const auto& [_, window] : windows) {
    ordered_windows.push_back(window);
  }
  stats.windows_generated = ordered_windows.size();
  if (ordered_windows.empty()) {
    return;
  }

  std::ranges::sort(
      ordered_windows,
      [](const WindowCandidate& lhs, const WindowCandidate& rhs) {
        if (lhs.priority != rhs.priority) {
          return lhs.priority > rhs.priority;
        }
        if (lhs.seg_id != rhs.seg_id) {
          return lhs.seg_id < rhs.seg_id;
        }
        return lhs.start < rhs.start;
      });

  constexpr int kMaxWindows = 512;
  if (static_cast<int>(ordered_windows.size()) > kMaxWindows) {
    stats.windows_capped = static_cast<int>(ordered_windows.size()) - kMaxWindows;
    ordered_windows.resize(kMaxWindows);
  }
  stats.windows_selected = ordered_windows.size();
  for (const WindowCandidate& window : ordered_windows) {
    const bool micro_start_segment
        = window.seg_id >= 0 && window.seg_id < mgrPtr_->getNumSegments()
          && micro_start_segment_mask[window.seg_id] != 0;
    if (micro_start_segment) {
      ++stats.windows_on_micro_start_segments;
    } else {
      ++stats.windows_on_base_segments;
    }
  }

  int consecutive_no_accept = 0;
  for (const WindowCandidate& window : ordered_windows) {
    if ((stats.windows_evaluated >= 192 && stats.accepts == 0)
        || (consecutive_no_accept >= 96 && stats.accepts > 0)) {
      stats.early_stopped = true;
      break;
    }

    mgrPtr_->sortCellsInSeg(window.seg_id);
    const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(window.seg_id);
    if (window.stop >= static_cast<int>(nodes.size())) {
      continue;
    }

    DetailedSeg* segPtr = mgrPtr_->getSegment(window.seg_id);
    const Node* nextPtr
        = (window.stop + 1 < static_cast<int>(nodes.size())) ? nodes[window.stop + 1]
                                                              : nullptr;
    DbuX rightLimit{segPtr->getMaxX()};
    if (nextPtr != nullptr) {
      int leftPadding, rightPadding;
      arch_->getCellPadding(nextPtr, leftPadding, rightPadding);
      rightLimit = std::min(nextPtr->getLeft() - leftPadding, rightLimit);
    }
    const Node* prevPtr = (window.start > 0) ? nodes[window.start - 1] : nullptr;
    DbuX leftLimit{segPtr->getMinX()};
    if (prevPtr != nullptr) {
      int leftPadding, rightPadding;
      arch_->getCellPadding(prevPtr, leftPadding, rightPadding);
      leftLimit = std::max(prevPtr->getRight() + rightPadding, leftLimit);
    }

    ++stats.windows_evaluated;
    const WindowApplyResult result = reorder(nodes,
                                             window.start,
                                             window.stop,
                                             leftLimit,
                                             rightLimit,
                                             window.seg_id,
                                             window.row_id);
    stats.exact_scored += result.exact_scored;
    if (result.rolled_back) {
      ++stats.rollbacks;
    }
    if (result.accepted) {
      ++stats.accepts;
      stats.accepted_gain += result.gain;
      const bool micro_start_segment
          = window.seg_id >= 0 && window.seg_id < mgrPtr_->getNumSegments()
            && micro_start_segment_mask[window.seg_id] != 0;
      if (micro_start_segment) {
        ++stats.accepts_on_micro_start_segments;
        stats.gain_on_micro_start_segments += result.gain;
      } else {
        ++stats.accepts_on_base_segments;
        stats.gain_on_base_segments += result.gain;
      }
      consecutive_no_accept = 0;
    } else {
      ++consecutive_no_accept;
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::criticalNetChainAssignment(
    DetailedMgr* mgrPtr,
    const std::vector<Node*>& frontier_nodes,
    ChainAssignmentStats& stats)
{
  mgrPtr_ = mgrPtr;

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgrPtr_, nullptr);
  hpwlObj.curr();

  const auto& hot_segments = mgrPtr_->getHotSegments();
  const auto& critical_frontier_nodes = mgrPtr_->getCriticalNetFrontierNodes();
  const std::vector<Node*> active_frontier_nodes
      = mergeFrontierNodes(critical_frontier_nodes, frontier_nodes);

  if (active_frontier_nodes.empty() || hot_segments.empty()) {
    return;
  }

  std::vector<unsigned char> hot_segment_mask(mgrPtr_->getNumSegments(), 0);
  for (const int seg_id : hot_segments) {
    if (seg_id >= 0 && seg_id < mgrPtr_->getNumSegments()) {
      hot_segment_mask[seg_id] = 1;
    }
  }

  struct RankedFrontierNode
  {
    Node* node = nullptr;
    int seg_id = -1;
    int row_id = -1;
    int64_t displacement = 0;
  };
  std::vector<RankedFrontierNode> ranked_frontier;
  ranked_frontier.reserve(active_frontier_nodes.size());
  std::vector<unsigned char> seen_segments(mgrPtr_->getNumSegments(), 0);
  for (Node* node : active_frontier_nodes) {
    if (node == nullptr || arch_->isMultiHeightCell(node)
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const auto& segs = mgrPtr_->getReverseCellToSegs(node->getId());
    const int seg_id = segs[0]->getSegId();
    const int row_id = segs[0]->getRowId();
    if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()
        || hot_segment_mask[seg_id] == 0) {
      continue;
    }
    const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
    const DbuY dy = abs(node->getBottom() - node->getOrigBottom());
    const int64_t displacement = dx.v + dy.v;
    if (displacement == 0) {
      continue;
    }
    ranked_frontier.push_back({node, seg_id, row_id, displacement});
    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
  }
  stats.frontier_cells = ranked_frontier.size();
  if (ranked_frontier.empty()) {
    return;
  }

  std::ranges::sort(
      ranked_frontier,
      [](const RankedFrontierNode& lhs, const RankedFrontierNode& rhs) {
        if (lhs.displacement != rhs.displacement) {
          return lhs.displacement > rhs.displacement;
        }
        return lhs.node->getId() < rhs.node->getId();
      });

  struct MoveCandidate
  {
    DbuX target_left{0};
    DbuY target_bottom{0};
    int target_seg_id = -1;
    int target_row_id = -1;
    int improvement = 0;
    int move_distance = 0;
  };
  struct ChainNodeTarget
  {
    Node* node = nullptr;
    DbuX orig_left{0};
    DbuY orig_bottom{0};
    int orig_seg_id = -1;
    DbuX target_left{0};
    DbuY target_bottom{0};
    int target_seg_id = -1;
  };
  struct ChainPlan
  {
    std::vector<ChainNodeTarget> steps;
    double gain = 0.0;
  };

  constexpr int kMaxSeeds = 80;
  constexpr int kMaxMoveCandidates = 4;
  constexpr int kMaxPartners = 4;
  constexpr int kMaxFollowers = 2;
  constexpr int kMaxWindows = 320;
  constexpr int kMaxAcceptedMoves = 4;
  const std::array<int, 3> row_offsets = {0, -1, 1};
  const std::array<int, 5> partner_offsets = {0, -1, 1, -2, 2};

  auto sort_and_unique_targets = [](std::vector<ChainNodeTarget>& steps) {
    std::sort(steps.begin(),
              steps.end(),
              [](const ChainNodeTarget& lhs, const ChainNodeTarget& rhs) {
                return lhs.node->getId() < rhs.node->getId();
              });
    steps.erase(std::unique(steps.begin(),
                            steps.end(),
                            [](const ChainNodeTarget& lhs,
                               const ChainNodeTarget& rhs) {
                              return lhs.node->getId() == rhs.node->getId();
                            }),
                steps.end());
  };

  auto build_move_candidates = [&](Node* seed,
                                   const int source_seg_id,
                                   const int source_row_id) {
    std::vector<MoveCandidate> move_candidates;
    odb::Rect bbox;
    if (!getMedianRange(arch_, seed, skipNetsLargerThanThis_, bbox)) {
      return move_candidates;
    }

    int dispX = 0;
    int dispY = 0;
    mgrPtr_->getMaxDisplacement(dispX, dispY);
    const int width = seed->getWidth().v;
    const int height = seed->getHeight().v;
    const int current_center_x = seed->getCenterX().v;
    const int current_center_y = seed->getCenterY().v;
    const int old_dist
        = std::max(0, bbox.xMin() - current_center_x)
          + std::max(0, current_center_x - bbox.xMax())
          + std::max(0, bbox.yMin() - current_center_y)
          + std::max(0, current_center_y - bbox.yMax());
    const int projected_left = static_cast<int>(std::floor(
        0.5 * (bbox.xMin() + bbox.xMax()) - 0.5 * width));
    const int left_anchor = bbox.xMin() - (width / 2);
    const int right_anchor = bbox.xMax() - (width / 2);
    const int projected_bottom = static_cast<int>(std::floor(
        0.5 * (bbox.yMin() + bbox.yMax()) - 0.5 * height));
    const int target_row = arch_->find_closest_row(DbuY{projected_bottom});
    const std::array<int, 3> raw_anchors
        = {left_anchor, projected_left, right_anchor};

    for (const int row_offset : row_offsets) {
      const int row_id = target_row + row_offset;
      if (row_id < 0 || row_id >= arch_->getNumRows()) {
        continue;
      }
      const DbuY row_bottom = arch_->getRow(row_id)->getBottom();
      if (std::abs((row_bottom - seed->getBottom()).v) > dispY) {
        continue;
      }
      for (int s = 0; s < mgrPtr_->getNumSegsInRow(row_id); ++s) {
        DetailedSeg* seg_ptr = mgrPtr_->getSegsInRow(row_id)[s];
        const int seg_id = seg_ptr->getSegId();
        if (seg_id < 0 || seg_id >= mgrPtr_->getNumSegments()
            || seed->getGroupId() != mgrPtr_->getSegment(seg_id)->getRegId()
            || hot_segment_mask[seg_id] == 0) {
          continue;
        }
        for (const int raw_anchor : raw_anchors) {
          DbuX aligned_left{raw_anchor};
          if (!mgrPtr_->alignPos(
                  seed, aligned_left, seg_ptr->getMinX(), seg_ptr->getMaxX())) {
            continue;
          }
          const int dx = std::abs((aligned_left - seed->getLeft()).v);
          const int dy = std::abs((row_bottom - seed->getBottom()).v);
          if (dx > dispX || dy > dispY) {
            continue;
          }

          const int cand_center_x = aligned_left.v + (width / 2);
          const int cand_center_y = row_bottom.v + (height / 2);
          const int new_dist
              = std::max(0, bbox.xMin() - cand_center_x)
                + std::max(0, cand_center_x - bbox.xMax())
                + std::max(0, bbox.yMin() - cand_center_y)
                + std::max(0, cand_center_y - bbox.yMax());
          const int improvement = old_dist - new_dist;
          if (improvement <= 0 && seg_id == source_seg_id && row_id == source_row_id) {
            continue;
          }
          move_candidates.push_back(MoveCandidate{aligned_left,
                                                  row_bottom,
                                                  seg_id,
                                                  row_id,
                                                  improvement,
                                                  dx + dy});
        }
      }
    }

    std::sort(move_candidates.begin(),
              move_candidates.end(),
              [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
                if (lhs.target_seg_id != rhs.target_seg_id) {
                  return lhs.target_seg_id < rhs.target_seg_id;
                }
                if (lhs.target_bottom != rhs.target_bottom) {
                  return lhs.target_bottom < rhs.target_bottom;
                }
                return lhs.target_left < rhs.target_left;
              });
    move_candidates.erase(
        std::unique(move_candidates.begin(),
                    move_candidates.end(),
                    [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
                      return lhs.target_seg_id == rhs.target_seg_id
                             && lhs.target_bottom == rhs.target_bottom
                             && lhs.target_left == rhs.target_left;
                    }),
        move_candidates.end());
    std::ranges::sort(
        move_candidates,
        [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
          if (lhs.improvement != rhs.improvement) {
            return lhs.improvement > rhs.improvement;
          }
          return lhs.move_distance < rhs.move_distance;
        });
    if (static_cast<int>(move_candidates.size()) > kMaxMoveCandidates) {
      move_candidates.resize(kMaxMoveCandidates);
    }
    return move_candidates;
  };

  auto collect_local_neighbors = [&](Node* anchor,
                                     const int seg_id,
                                     const int row_id,
                                     const int max_cells) {
    std::vector<Node*> neighbors;
    std::unordered_set<int> seen_ids;
    for (const int row_offset : row_offsets) {
      const int cand_row = row_id + row_offset;
      if (cand_row < 0 || cand_row >= arch_->getNumRows()) {
        continue;
      }
      for (int s = 0; s < mgrPtr_->getNumSegsInRow(cand_row); ++s) {
        DetailedSeg* seg_ptr = mgrPtr_->getSegsInRow(cand_row)[s];
        const int cand_seg = seg_ptr->getSegId();
        if (cand_seg < 0 || cand_seg >= mgrPtr_->getNumSegments()
            || hot_segment_mask[cand_seg] == 0) {
          continue;
        }
        mgrPtr_->sortCellsInSeg(cand_seg);
        const std::vector<Node*>& nodes = mgrPtr_->getCellsInSeg(cand_seg);
        if (nodes.empty()) {
          continue;
        }
        auto it = std::lower_bound(
            nodes.begin(),
            nodes.end(),
            anchor->getCenterX(),
            [](const Node* node, const DbuX center_x) {
              return node->getCenterX() < center_x;
            });
        int base_idx = (it == nodes.end()) ? static_cast<int>(nodes.size()) - 1
                                           : static_cast<int>(it - nodes.begin());
        for (const int partner_offset : partner_offsets) {
          const int idx = base_idx + partner_offset;
          if (idx < 0 || idx >= static_cast<int>(nodes.size())) {
            continue;
          }
          Node* neighbor = nodes[idx];
          if (neighbor == nullptr || neighbor == anchor
              || arch_->isMultiHeightCell(neighbor)
              || mgrPtr_->getNumReverseCellToSegs(neighbor->getId()) != 1
              || !seen_ids.insert(neighbor->getId()).second) {
            continue;
          }
          neighbors.push_back(neighbor);
          if (static_cast<int>(neighbors.size()) >= max_cells) {
            return neighbors;
          }
        }
      }
    }
    return neighbors;
  };

  auto add_step = [&](std::vector<ChainNodeTarget>& steps,
                      Node* node,
                      const DbuX target_left,
                      const DbuY target_bottom,
                      const int target_seg_id) {
    if (node == nullptr || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      return false;
    }
    const auto& segs = mgrPtr_->getReverseCellToSegs(node->getId());
    const int orig_seg_id = segs[0]->getSegId();
    steps.push_back(ChainNodeTarget{node,
                                    node->getLeft(),
                                    node->getBottom(),
                                    orig_seg_id,
                                    target_left,
                                    target_bottom,
                                    target_seg_id});
    return true;
  };

  auto apply_chain_plan = [&](const ChainPlan& plan) {
    if (plan.steps.empty()) {
      return false;
    }
    for (size_t step_idx = 0; step_idx < plan.steps.size(); ++step_idx) {
      const ChainNodeTarget& step = plan.steps[step_idx];
      if (!mgrPtr_->tryMove(step.node,
                            step.node->getLeft(),
                            step.node->getBottom(),
                            mgrPtr_->getReverseCellToSegs(step.node->getId())[0]
                                ->getSegId(),
                            step.target_left,
                            step.target_bottom,
                            step.target_seg_id,
                            step_idx == 0)) {
        return false;
      }
    }
    return true;
  };

  int consecutive_no_accept = 0;
  int chain_windows_seen = 0;
  for (int seed_idx = 0;
       seed_idx < std::min(kMaxSeeds, static_cast<int>(ranked_frontier.size()));
       ++seed_idx) {
    if (chain_windows_seen >= kMaxWindows
        || (stats.exact_scored >= 160 && stats.accepts == 0)
        || (stats.exact_scored >= 320 && consecutive_no_accept >= 48)) {
      stats.early_stopped = true;
      break;
    }

    Node* seed = ranked_frontier[seed_idx].node;
    const int source_seg_id = ranked_frontier[seed_idx].seg_id;
    const int source_row_id = ranked_frontier[seed_idx].row_id;
    const auto move_candidates
        = build_move_candidates(seed, source_seg_id, source_row_id);
    if (move_candidates.empty()) {
      ++consecutive_no_accept;
      continue;
    }
    ++stats.seeds_selected;

    ChainPlan best_plan;
    for (const MoveCandidate& move_candidate : move_candidates) {
      if (chain_windows_seen >= kMaxWindows) {
        break;
      }
      ++chain_windows_seen;
      ++stats.chain_windows;

      const auto partner_candidates = collect_local_neighbors(
          seed, move_candidate.target_seg_id, move_candidate.target_row_id, kMaxPartners);
      if (partner_candidates.empty()) {
        ++stats.depth2_rejects;
        continue;
      }

      for (Node* partner : partner_candidates) {
        if (partner == nullptr || partner == seed) {
          continue;
        }

        ChainPlan depth2_plan;
        if (!add_step(depth2_plan.steps,
                      seed,
                      move_candidate.target_left,
                      move_candidate.target_bottom,
                      move_candidate.target_seg_id)
            || !add_step(depth2_plan.steps,
                         partner,
                         seed->getLeft(),
                         seed->getBottom(),
                         source_seg_id)) {
          ++stats.depth2_rejects;
          continue;
        }
        sort_and_unique_targets(depth2_plan.steps);
        if (static_cast<int>(depth2_plan.steps.size()) != 2
            || !apply_chain_plan(depth2_plan)) {
          ++stats.depth2_rejects;
          mgrPtr_->rejectMove();
          continue;
        }

        double depth2_gain = hpwlObj.delta(mgrPtr_->getJournal());
        ++stats.probes;
        ++stats.exact_scored;

        ChainPlan best_local_plan = depth2_plan;
        best_local_plan.gain = depth2_gain;

        const auto follower_candidates = collect_local_neighbors(
            partner, source_seg_id, source_row_id, kMaxFollowers);
        for (Node* follower : follower_candidates) {
          if (follower == nullptr || follower == seed || follower == partner) {
            continue;
          }
          ChainPlan depth3_plan = depth2_plan;
          if (!add_step(depth3_plan.steps,
                        follower,
                        partner->getLeft(),
                        partner->getBottom(),
                        mgrPtr_->getReverseCellToSegs(partner->getId())[0]
                            ->getSegId())) {
            ++stats.depth3_rejects;
            continue;
          }
          sort_and_unique_targets(depth3_plan.steps);
          if (static_cast<int>(depth3_plan.steps.size()) != 3) {
            ++stats.depth3_rejects;
            continue;
          }

          mgrPtr_->rejectMove();
          if (!apply_chain_plan(depth3_plan)) {
            ++stats.depth3_rejects;
            mgrPtr_->rejectMove();
            continue;
          }
          const double depth3_gain = hpwlObj.delta(mgrPtr_->getJournal());
          ++stats.probes;
          ++stats.exact_scored;
          ++stats.depth3_probes;
          if (depth3_gain > best_local_plan.gain) {
            best_local_plan = depth3_plan;
            best_local_plan.gain = depth3_gain;
          }
        }

        mgrPtr_->rejectMove();
        ++stats.rollbacks;
        if (best_local_plan.gain > best_plan.gain
            && static_cast<int>(best_local_plan.steps.size()) <= kMaxAcceptedMoves) {
          best_plan = best_local_plan;
        }
      }
    }

    if (best_plan.gain <= 0.0 || best_plan.steps.empty()) {
      ++consecutive_no_accept;
      continue;
    }

    if (!apply_chain_plan(best_plan)) {
      ++stats.rollbacks;
      ++consecutive_no_accept;
      mgrPtr_->rejectMove();
      continue;
    }

    const double accepted_delta = hpwlObj.delta(mgrPtr_->getJournal());
    if (accepted_delta <= 0.0) {
      mgrPtr_->rejectMove();
      ++stats.rollbacks;
      ++consecutive_no_accept;
      continue;
    }

    hpwlObj.accept();
    mgrPtr_->acceptMove();
    ++stats.accepts;
    stats.accepted_gain += accepted_delta;
    consecutive_no_accept = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::multiRowResidualTransactions(
    DetailedMgr* mgrPtr,
    const std::vector<Node*>& frontier_nodes,
    MultiRowTransactionStats& stats)
{
  mgrPtr_ = mgrPtr;

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgrPtr_, nullptr);
  hpwlObj.curr();

  const auto& hot_segments = mgrPtr_->getHotSegments();
  const auto& critical_frontier_nodes = mgrPtr_->getCriticalNetFrontierNodes();
  std::vector<unsigned char> hot_segment_mask(mgrPtr_->getNumSegments(), 0);
  for (const int seg_id : hot_segments) {
    if (seg_id >= 0 && seg_id < mgrPtr_->getNumSegments()) {
      hot_segment_mask[seg_id] = 1;
    }
  }

  struct RankedFrontierNode
  {
    Node* node = nullptr;
    int64_t displacement = 0;
    int seg_id = -1;
  };

  const std::vector<Node*> active_frontier_nodes
      = mergeFrontierNodes(critical_frontier_nodes, frontier_nodes);
  std::vector<RankedFrontierNode> ranked_frontier;
  ranked_frontier.reserve(active_frontier_nodes.size());
  std::vector<unsigned char> seen_segments(mgrPtr_->getNumSegments(), 0);
  for (Node* node : active_frontier_nodes) {
    if (node == nullptr || arch_->isMultiHeightCell(node)
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    if (!hot_segments.empty() && !hot_segment_mask[seg_id]) {
      continue;
    }

    const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
    const DbuY dy = abs(node->getBottom() - node->getOrigBottom());
    const int64_t displacement = dx.v + dy.v;
    if (displacement == 0) {
      continue;
    }

    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
    ranked_frontier.push_back({node, displacement, seg_id});
  }

  stats.frontier_cells = ranked_frontier.size();
  if (ranked_frontier.empty()) {
    return;
  }

  std::ranges::sort(
      ranked_frontier,
      [](const RankedFrontierNode& lhs, const RankedFrontierNode& rhs) {
        if (lhs.displacement != rhs.displacement) {
          return lhs.displacement > rhs.displacement;
        }
        return lhs.node->getId() < rhs.node->getId();
      });

  struct MoveCandidate
  {
    DbuX target_left{0};
    DbuY target_bottom{0};
    int target_seg_id = -1;
    int distance_improvement = 0;
    int center_distance = 0;
    int move_distance = 0;
  };
  struct TransactionPlan
  {
    Node* seed = nullptr;
    Node* partner = nullptr;
    DbuX seed_orig_left{0};
    DbuY seed_orig_bottom{0};
    int source_seg_id = -1;
    DbuX seed_target_left{0};
    DbuY seed_target_bottom{0};
    int target_seg_id = -1;
    double gain = 0.0;
  };

  constexpr int kMaxSeeds = 96;
  constexpr int kMaxMoveCandidates = 4;
  constexpr int kMaxPartnerCandidates = 6;
  const std::array<int, 5> row_offsets = {0, -1, 1, -2, 2};
  const std::array<int, 7> partner_offsets = {0, -1, 1, -2, 2, -3, 3};

  int consecutive_no_accept = 0;
  for (int frontier_idx = 0;
       frontier_idx < std::min(kMaxSeeds, static_cast<int>(ranked_frontier.size()));
       ++frontier_idx) {
    if ((stats.exact_scored >= 320 && stats.accepts == 0)
        || (stats.exact_scored >= 768 && consecutive_no_accept >= 32)) {
      stats.early_stopped = true;
      break;
    }

    Node* seed = ranked_frontier[frontier_idx].node;
    if (seed == nullptr || mgrPtr_->getNumReverseCellToSegs(seed->getId()) != 1) {
      continue;
    }
    ++stats.seeds_selected;

    odb::Rect bbox;
    if (!getMedianRange(arch_, seed, skipNetsLargerThanThis_, bbox)) {
      ++consecutive_no_accept;
      continue;
    }

    const int source_seg_id
        = mgrPtr_->getReverseCellToSegs(seed->getId())[0]->getSegId();
    const int width = seed->getWidth().v;
    const int height = seed->getHeight().v;
    const int current_center_x = seed->getCenterX().v;
    const int current_center_y = seed->getCenterY().v;
    const int bbox_center_x = (bbox.xMin() + bbox.xMax()) / 2;
    const int bbox_center_y = (bbox.yMin() + bbox.yMax()) / 2;
    const int old_dist
        = std::max(0, bbox.xMin() - current_center_x)
          + std::max(0, current_center_x - bbox.xMax())
          + std::max(0, bbox.yMin() - current_center_y)
          + std::max(0, current_center_y - bbox.yMax());

    const int projected_left = static_cast<int>(std::floor(
        0.5 * (bbox.xMin() + bbox.xMax()) - 0.5 * width));
    const int left_anchor = bbox.xMin() - (width / 2);
    const int right_anchor = bbox.xMax() - (width / 2);
    const int projected_bottom = static_cast<int>(std::floor(
        0.5 * (bbox.yMin() + bbox.yMax()) - 0.5 * height));
    const int target_row = arch_->find_closest_row(DbuY{projected_bottom});
    int dispX = 0;
    int dispY = 0;
    mgrPtr_->getMaxDisplacement(dispX, dispY);

    std::vector<MoveCandidate> move_candidates;
    move_candidates.reserve(12);
    const std::array<int, 3> raw_anchors
        = {left_anchor, projected_left, right_anchor};
    for (const int offset : row_offsets) {
      const int row_id = target_row + offset;
      if (row_id < 0 || row_id >= arch_->getNumRows()) {
        continue;
      }

      const DbuY row_bottom = arch_->getRow(row_id)->getBottom();
      if (std::abs((row_bottom - seed->getBottom()).v) > dispY) {
        continue;
      }

      for (int s = 0; s < mgrPtr_->getNumSegsInRow(row_id); s++) {
        DetailedSeg* seg_ptr = mgrPtr_->getSegsInRow(row_id)[s];
        const int seg_id = seg_ptr->getSegId();
        if (seg_id < 0
            || seed->getGroupId() != mgrPtr_->getSegment(seg_id)->getRegId()) {
          continue;
        }
        if (!hot_segments.empty() && hot_segment_mask[seg_id] == 0) {
          continue;
        }

        for (const int raw_anchor : raw_anchors) {
          DbuX aligned_left{raw_anchor};
          if (!mgrPtr_->alignPos(
                  seed, aligned_left, seg_ptr->getMinX(), seg_ptr->getMaxX())) {
            continue;
          }
          const int dx = std::abs((aligned_left - seed->getLeft()).v);
          const int dy = std::abs((row_bottom - seed->getBottom()).v);
          if (dx > dispX || dy > dispY) {
            continue;
          }

          const int cand_center_x = aligned_left.v + (width / 2);
          const int cand_center_y = row_bottom.v + (height / 2);
          const int new_dist
              = std::max(0, bbox.xMin() - cand_center_x)
                + std::max(0, cand_center_x - bbox.xMax())
                + std::max(0, bbox.yMin() - cand_center_y)
                + std::max(0, cand_center_y - bbox.yMax());

          move_candidates.push_back(MoveCandidate{aligned_left,
                                                  row_bottom,
                                                  seg_id,
                                                  old_dist - new_dist,
                                                  std::abs(cand_center_x
                                                           - bbox_center_x)
                                                      + std::abs(cand_center_y
                                                                 - bbox_center_y),
                                                  dx + dy});
        }
      }
    }

    if (move_candidates.empty()) {
      ++consecutive_no_accept;
      continue;
    }

    std::sort(move_candidates.begin(),
              move_candidates.end(),
              [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
                if (lhs.target_seg_id != rhs.target_seg_id) {
                  return lhs.target_seg_id < rhs.target_seg_id;
                }
                if (lhs.target_bottom != rhs.target_bottom) {
                  return lhs.target_bottom < rhs.target_bottom;
                }
                return lhs.target_left < rhs.target_left;
              });
    move_candidates.erase(
        std::unique(move_candidates.begin(),
                    move_candidates.end(),
                    [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
                      return lhs.target_seg_id == rhs.target_seg_id
                             && lhs.target_bottom == rhs.target_bottom
                             && lhs.target_left == rhs.target_left;
                    }),
        move_candidates.end());

    std::ranges::sort(
        move_candidates,
        [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
          if (lhs.distance_improvement != rhs.distance_improvement) {
            return lhs.distance_improvement > rhs.distance_improvement;
          }
          if (lhs.center_distance != rhs.center_distance) {
            return lhs.center_distance < rhs.center_distance;
          }
          return lhs.move_distance < rhs.move_distance;
        });
    if (static_cast<int>(move_candidates.size()) > kMaxMoveCandidates) {
      move_candidates.resize(kMaxMoveCandidates);
    }
    stats.transaction_windows += move_candidates.size();

    TransactionPlan best_plan;
    for (const MoveCandidate& move_candidate : move_candidates) {
      mgrPtr_->sortCellsInSeg(move_candidate.target_seg_id);
      const std::vector<Node*>& target_nodes
          = mgrPtr_->getCellsInSeg(move_candidate.target_seg_id);
      if (target_nodes.empty()) {
        continue;
      }

      auto anchor_it = std::lower_bound(
          target_nodes.begin(),
          target_nodes.end(),
          move_candidate.target_left,
          [](const Node* node, const DbuX target_left) {
            return node->getCenterX() < target_left;
          });
      int anchor_idx = 0;
      if (anchor_it == target_nodes.end()) {
        anchor_idx = static_cast<int>(target_nodes.size()) - 1;
      } else {
        anchor_idx = static_cast<int>(anchor_it - target_nodes.begin());
      }

      std::vector<Node*> partner_candidates;
      partner_candidates.reserve(kMaxPartnerCandidates);
      std::unordered_set<int> seen_partner_ids;
      for (const int partner_offset : partner_offsets) {
        const int partner_idx = anchor_idx + partner_offset;
        if (partner_idx < 0 || partner_idx >= static_cast<int>(target_nodes.size())) {
          continue;
        }
        Node* partner = target_nodes[partner_idx];
        if (partner == nullptr || partner == seed || arch_->isMultiHeightCell(partner)
            || !seen_partner_ids.insert(partner->getId()).second) {
          continue;
        }
        partner_candidates.push_back(partner);
        if (static_cast<int>(partner_candidates.size()) >= kMaxPartnerCandidates) {
          break;
        }
      }
      if (partner_candidates.empty()) {
        continue;
      }

      for (Node* partner : partner_candidates) {
        if (mgrPtr_->getNumReverseCellToSegs(partner->getId()) != 1) {
          continue;
        }
        const int partner_seg_id
            = mgrPtr_->getReverseCellToSegs(partner->getId())[0]->getSegId();
        if (partner_seg_id != move_candidate.target_seg_id) {
          continue;
        }

        if (!mgrPtr_->tryMove(seed,
                              seed->getLeft(),
                              seed->getBottom(),
                              source_seg_id,
                              move_candidate.target_left,
                              move_candidate.target_bottom,
                              move_candidate.target_seg_id)) {
          ++stats.first_step_rejects;
          continue;
        }
        if (!mgrPtr_->tryMove(partner,
                              partner->getLeft(),
                              partner->getBottom(),
                              partner_seg_id,
                              seed->getLeft(),
                              seed->getBottom(),
                              source_seg_id,
                              false)) {
          ++stats.second_step_rejects;
          continue;
        }

        const double delta = hpwlObj.delta(mgrPtr_->getJournal());
        ++stats.probes;
        ++stats.exact_scored;
        mgrPtr_->rejectMove();
        ++stats.rollbacks;
        if (delta > best_plan.gain) {
          best_plan.seed = seed;
          best_plan.partner = partner;
          best_plan.seed_orig_left = seed->getLeft();
          best_plan.seed_orig_bottom = seed->getBottom();
          best_plan.source_seg_id = source_seg_id;
          best_plan.seed_target_left = move_candidate.target_left;
          best_plan.seed_target_bottom = move_candidate.target_bottom;
          best_plan.target_seg_id = move_candidate.target_seg_id;
          best_plan.gain = delta;
        }
      }
    }

    if (best_plan.gain <= 0.0 || best_plan.seed == nullptr
        || best_plan.partner == nullptr) {
      ++consecutive_no_accept;
      continue;
    }

    if (!mgrPtr_->tryMove(best_plan.seed,
                          best_plan.seed->getLeft(),
                          best_plan.seed->getBottom(),
                          best_plan.source_seg_id,
                          best_plan.seed_target_left,
                          best_plan.seed_target_bottom,
                          best_plan.target_seg_id)) {
      ++stats.first_step_rejects;
      ++consecutive_no_accept;
      continue;
    }
    if (mgrPtr_->getNumReverseCellToSegs(best_plan.partner->getId()) != 1
        || !mgrPtr_->tryMove(best_plan.partner,
                             best_plan.partner->getLeft(),
                             best_plan.partner->getBottom(),
                             mgrPtr_->getReverseCellToSegs(best_plan.partner->getId())[0]
                                 ->getSegId(),
                             best_plan.seed_orig_left,
                             best_plan.seed_orig_bottom,
                             best_plan.source_seg_id,
                             false)) {
      ++stats.second_step_rejects;
      ++consecutive_no_accept;
      continue;
    }

    const double accepted_delta = hpwlObj.delta(mgrPtr_->getJournal());
    if (accepted_delta <= 0.0) {
      mgrPtr_->rejectMove();
      ++stats.rollbacks;
      ++consecutive_no_accept;
      continue;
    }

    hpwlObj.accept();
    mgrPtr_->acceptMove();
    ++stats.accepts;
    stats.accepted_gain += accepted_delta;
    consecutive_no_accept = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::segmentLocalResidualSwaps(
    DetailedMgr* mgrPtr,
    const std::vector<Node*>& frontier_nodes,
    ResidualSwapStats& stats)
{
  mgrPtr_ = mgrPtr;

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgrPtr_, nullptr);
  hpwlObj.curr();

  const auto& hot_segments = mgrPtr_->getHotSegments();
  const auto& critical_frontier_nodes = mgrPtr_->getCriticalNetFrontierNodes();
  std::vector<unsigned char> hot_segment_mask(mgrPtr_->getNumSegments(), 0);
  for (const int seg_id : hot_segments) {
    if (seg_id >= 0 && seg_id < mgrPtr_->getNumSegments()) {
      hot_segment_mask[seg_id] = 1;
    }
  }

  struct RankedFrontierNode
  {
    Node* node = nullptr;
    int64_t displacement = 0;
    int seg_id = -1;
  };

  const std::vector<Node*> active_frontier_nodes
      = mergeFrontierNodes(critical_frontier_nodes, frontier_nodes);
  std::vector<RankedFrontierNode> ranked_frontier;
  ranked_frontier.reserve(active_frontier_nodes.size());
  std::vector<unsigned char> seen_segments(mgrPtr_->getNumSegments(), 0);
  for (Node* node : active_frontier_nodes) {
    if (node == nullptr || arch_->isMultiHeightCell(node)
        || mgrPtr_->getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const int seg_id = mgrPtr_->getReverseCellToSegs(node->getId())[0]->getSegId();
    if (!hot_segments.empty() && hot_segment_mask[seg_id] == 0) {
      continue;
    }

    const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
    const DbuY dy = abs(node->getBottom() - node->getOrigBottom());
    const int64_t displacement = dx.v + dy.v;
    if (displacement == 0) {
      continue;
    }

    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
    ranked_frontier.push_back({node, displacement, seg_id});
  }

  stats.frontier_cells = ranked_frontier.size();
  if (ranked_frontier.empty()) {
    return;
  }

  std::ranges::sort(
      ranked_frontier,
      [](const RankedFrontierNode& lhs, const RankedFrontierNode& rhs) {
        if (lhs.displacement != rhs.displacement) {
          return lhs.displacement > rhs.displacement;
        }
        return lhs.node->getId() < rhs.node->getId();
      });

  constexpr int kMaxSeeds = 64;
  const std::array<int, 5> partner_offsets = {0, -1, 1, -2, 2};

  int consecutive_no_accept = 0;
  for (int frontier_idx = 0;
       frontier_idx < std::min(kMaxSeeds, static_cast<int>(ranked_frontier.size()));
       ++frontier_idx) {
    if ((stats.exact_scored >= 192 && stats.accepts == 0)
        || (stats.exact_scored >= 384 && consecutive_no_accept >= 24)) {
      stats.early_stopped = true;
      break;
    }

    Node* seed = ranked_frontier[frontier_idx].node;
    if (seed == nullptr || mgrPtr_->getNumReverseCellToSegs(seed->getId()) != 1) {
      continue;
    }
    ++stats.seeds_selected;

    const int seed_seg_id = ranked_frontier[frontier_idx].seg_id;
    mgrPtr_->sortCellsInSeg(seed_seg_id);
    const std::vector<Node*>& seg_nodes = mgrPtr_->getCellsInSeg(seed_seg_id);
    if (seg_nodes.size() < 2) {
      ++consecutive_no_accept;
      continue;
    }

    auto seed_it = std::find(seg_nodes.begin(), seg_nodes.end(), seed);
    if (seed_it == seg_nodes.end()) {
      ++consecutive_no_accept;
      continue;
    }
    const int seed_idx = static_cast<int>(seed_it - seg_nodes.begin());

    Node* best_partner = nullptr;
    double best_gain = 0.0;
    for (const int partner_offset : partner_offsets) {
      const int partner_idx = seed_idx + partner_offset;
      if (partner_idx < 0 || partner_idx >= static_cast<int>(seg_nodes.size())) {
        continue;
      }
      Node* partner = seg_nodes[partner_idx];
      if (partner == nullptr || partner == seed || arch_->isMultiHeightCell(partner)
          || mgrPtr_->getNumReverseCellToSegs(partner->getId()) != 1) {
        continue;
      }

      ++stats.swap_windows;
      if (!mgrPtr_->trySwap(seed,
                            seed->getLeft(),
                            seed->getBottom(),
                            seed_seg_id,
                            partner->getLeft(),
                            partner->getBottom(),
                            seed_seg_id)) {
        ++stats.failed_swaps;
        continue;
      }

      const double delta = hpwlObj.delta(mgrPtr_->getJournal());
      ++stats.probes;
      ++stats.exact_scored;
      mgrPtr_->rejectMove();
      ++stats.rollbacks;
      if (delta > best_gain) {
        best_gain = delta;
        best_partner = partner;
      }
      if (static_cast<int>(stats.swap_windows) >= 256) {
        break;
      }
    }

    if (best_partner == nullptr || best_gain <= 0.0) {
      ++consecutive_no_accept;
      continue;
    }

    if (!mgrPtr_->trySwap(seed,
                          seed->getLeft(),
                          seed->getBottom(),
                          seed_seg_id,
                          best_partner->getLeft(),
                          best_partner->getBottom(),
                          seed_seg_id)) {
      ++stats.failed_swaps;
      ++consecutive_no_accept;
      continue;
    }

    const double accepted_delta = hpwlObj.delta(mgrPtr_->getJournal());
    if (accepted_delta <= 0.0) {
      mgrPtr_->rejectMove();
      ++stats.rollbacks;
      ++consecutive_no_accept;
      continue;
    }

    hpwlObj.accept();
    mgrPtr_->acceptMove();
    ++stats.accepts;
    stats.accepted_gain += accepted_delta;
    consecutive_no_accept = 0;
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
void DetailedReorderer::reorder()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, traversal_);

  // Loop over each segment; find single height cells and reorder.
  for (const int orderedSegId : segmentOrder_) {
    DetailedSeg* segPtr = mgrPtr_->getSegment(orderedSegId);
    const int segId = segPtr->getSegId();
    const int rowId = segPtr->getRowId();
    const bool focused = segId >= 0
                         && segId < static_cast<int>(focusedSegments_.size())
                         && focusedSegments_[segId] != 0;

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

        if (focused) {
          focusedWindowCount_++;
        }
        const WindowApplyResult result
            = reorder(nodes, istrt, istop, leftLimit, rightLimit, segId, rowId);
        if (result.accepted && focused) {
          focusedAcceptCount_++;
        }
      }
    }
  }
}

///////////////////////////////////////////////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////
DetailedReorderer::WindowApplyResult DetailedReorderer::reorder(
    const std::vector<Node*>& nodes,
    const int jstrt,
    const int jstop,
    const DbuX leftLimit,
    const DbuX rightLimit,
    const int segId,
    const int rowId)
{
  WindowApplyResult result;
  std::vector<const Edge*> exact_edges;
  const int size = jstop - jstrt + 1;
  if (size <= 0) {
    return result;
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
    return result;
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
    return result;
  }

  // Generate the different permutations.  Evaluate each one and keep
  // the best one.
  //
  // NOTE: The first permutation, which is the original placement,
  // might not generate the original placement since the spacing
  // might be different.  So, just consider the first permutation
  // like all the others.

  if (exactWindowMode_) {
    std::unordered_set<int> edge_ids;
    edge_ids.reserve(size * 8);
    for (int i = jstrt; i <= jstop; ++i) {
      const Node* ndi = nodes[i];
      for (const Pin* pin : ndi->getPins()) {
        const Edge* edge = pin->getEdge();
        if (edge == nullptr) {
          continue;
        }
        const int npins = edge->getNumPins();
        if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
          continue;
        }
        if (edge_ids.insert(edge->getId()).second) {
          exact_edges.push_back(edge);
        }
      }
    }
  }

  const auto score_window = [&]() -> double {
    return exactWindowMode_ ? exactCost(exact_edges) : cost(nodes, jstrt, jstop);
  };

  double bestCost = score_window();
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
      ++result.exact_scored;
      const double currCost = score_window();
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
    return result;
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
        const double lastCost = score_window();
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
      result.rolled_back = true;
      // Restore original placement.
      for (int i = 0; i < size; i++) {
        Node* ndi = nodes[jstrt + i];
        mgrPtr_->eraseFromGrid(ndi);
        ndi->setLeft(origLeft[ndi]);
        mgrPtr_->paintInGrid(ndi);
      }
      mgrPtr_->sortCellsInSeg(segId, jstrt, jstop + 1);
      return result;
    }
  }

  result.accepted = true;
  result.gain = origCost - score_window();
  return result;
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

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedReorderer::exactCost(const std::vector<const Edge*>& edges) const
{
  double cost = 0.0;
  for (const Edge* edge : edges) {
    cost += static_cast<double>(Utility::hpwl(edge));
  }
  return cost;
}

}  // namespace dpl_evolve
