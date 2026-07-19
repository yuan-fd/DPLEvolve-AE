// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

///////////////////////////////////////////////////////////////////////////////
//
// Description:
// Essentially a zero temperature annealer that can use a variety of
// move generators, different objectives and a cost function in order
// to improve a placement.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <functional>
#include <limits>
#include <map>
#include <set>
#include <stack>
#include <string>
#include <unordered_set>
#include <vector>

#include "boost/tokenizer.hpp"
#include "optimization/detailed_generator.h"
#include "util/utility.h"
#include "utl/Logger.h"
// For detailed improvement.
#include "detailed_manager.h"
#include "detailed_orient.h"
#include "detailed_random.h"
#include "infrastructure/detailed_segment.h"
// Detailed placement objectives.
#include "detailed_global.h"
#include "detailed_vertical.h"
#include "objective/detailed_abu.h"
#include "objective/detailed_displacement.h"
#include "objective/detailed_hpwl.h"
#include "objective/detailed_objective.h"

using utl::DPL;

namespace dpl_evolve {
namespace {

struct FocusRefreshStats
{
  int accepted_moves = 0;
  int anchor_nodes = 0;
  int frontier_edges = 0;
  int frontier_nodes = 0;
  int payload_windows = 0;
  int payload_rows = 0;
  int payload_nodes = 0;
};

struct ChangedNetNodeCandidate
{
  Node* node = nullptr;
  const Pin* pin = nullptr;
  int seg = -1;
  int row = -1;
  int target_left = 0;
  int target_row = -1;
  double pressure = 0.0;
};

struct VacancySlot
{
  DbuX left{0};
  DbuY bottom{0};
  int seg = -1;
  int width = 0;
};

struct ChangedNetAssignmentSlot
{
  DbuX left{0};
  DbuY bottom{0};
  int seg = -1;
  int row = -1;
  Node* owner = nullptr;
  bool current_slot = false;
};

struct ChangedNetAssignmentPlan
{
  std::vector<DetailedMgr::AssignmentMove> moves;
  double proxy_gain = 0.0;
};

struct PayloadWindowNodeCandidate
{
  Node* node = nullptr;
  int seg = -1;
  int row = -1;
  int target_left = 0;
  int target_row = -1;
  double pressure = 0.0;
  bool payload_seed = false;
  bool focus_seed = false;
  int dist_to_window = std::numeric_limits<int>::max();
};

FocusRefreshStats noteAcceptedExactPolishFrontier(DetailedMgr* mgr,
                                                  const Journal& journal,
                                                  const double acceptedGain)
{
  FocusRefreshStats stats;
  if (mgr == nullptr || acceptedGain <= 0.0) {
    return stats;
  }

  const auto& movedNodes = journal.getAffectedNodes();
  if (!movedNodes.empty()) {
    stats.accepted_moves = 1;
    stats.anchor_nodes = static_cast<int>(movedNodes.size());
    const double anchorShare
        = acceptedGain / static_cast<double>(movedNodes.size());
    for (Node* node : movedNodes) {
      mgr->noteExactPolishAnchor(node, anchorShare);
    }
  }

  const auto& affectedEdges = journal.getAffectedEdges();
  if (affectedEdges.empty()) {
    const auto payload_stats
        = mgr->noteExactPolishPayloadFromJournal(journal, acceptedGain);
    if (payload_stats.payload_nodes > 0) {
      stats.payload_windows += 1;
      stats.payload_rows += payload_stats.row_slices;
      stats.payload_nodes += payload_stats.payload_nodes;
    }
    return stats;
  }

  const double edgeShare
      = acceptedGain / static_cast<double>(affectedEdges.size());
  for (Edge* edge : affectedEdges) {
    if (edge == nullptr) {
      continue;
    }
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins > 16) {
      continue;
    }

    int movablePins = 0;
    for (const Pin* pin : edge->getPins()) {
      const Node* node = pin->getNode();
      if (node != nullptr && node->isStdCell() && !node->isFixed()) {
        movablePins++;
      }
    }
    if (movablePins == 0) {
      continue;
    }

    stats.frontier_edges += 1;
    stats.frontier_nodes += movablePins;
    const double frontierShare = edgeShare / static_cast<double>(movablePins);
    mgr->noteExactPolishEdge(edge, edgeShare);
    for (const Pin* pin : edge->getPins()) {
      mgr->noteExactPolishFrontier(pin->getNode(), frontierShare);
    }
  }

  const auto payload_stats
      = mgr->noteExactPolishPayloadFromJournal(journal, acceptedGain);
  if (payload_stats.payload_nodes > 0) {
    stats.payload_windows += 1;
    stats.payload_rows += payload_stats.row_slices;
    stats.payload_nodes += payload_stats.payload_nodes;
  }

  return stats;
}

std::vector<Edge*> collectFallbackChangedNets(DetailedMgr* mgr,
                                              const std::vector<Node*>& nodes,
                                              const int limit)
{
  std::vector<Edge*> edges;
  if (mgr == nullptr) {
    return edges;
  }

  for (Node* node : nodes) {
    if (node == nullptr || node->isFixed() || !node->isStdCell()) {
      continue;
    }
    for (const Pin* pin : node->getPins()) {
      Edge* edge = pin->getEdge();
      if (edge == nullptr || edge->getNumPins() <= 1 || edge->getNumPins() > 16) {
        continue;
      }
      if (std::find(edges.begin(), edges.end(), edge) == edges.end()) {
        edges.push_back(edge);
        if (limit > 0 && static_cast<int>(edges.size()) >= limit) {
          return edges;
        }
      }
    }
  }
  return edges;
}

std::vector<ChangedNetNodeCandidate> collectChangedNetNodeCandidates(
    DetailedMgr* mgr,
    Architecture* arch,
    Edge* edge)
{
  std::vector<ChangedNetNodeCandidate> bestCandidates;
  if (mgr == nullptr || arch == nullptr || edge == nullptr) {
    return bestCandidates;
  }

  std::map<int, ChangedNetNodeCandidate> bestByNode;
  const int row_height = mgr->getSingleRowHeight().v;
  for (const Pin* pin : edge->getPins()) {
    Node* node = pin->getNode();
    if (node == nullptr || node->isFixed() || !node->isStdCell()
        || !arch->isSingleHeightCell(node)) {
      continue;
    }

    const auto& segs = mgr->getReverseCellToSegs(node->getId());
    if (segs.size() != 1) {
      continue;
    }

    int xmin = std::numeric_limits<int>::max();
    int xmax = std::numeric_limits<int>::min();
    int ymin = std::numeric_limits<int>::max();
    int ymax = std::numeric_limits<int>::min();
    for (const Pin* other_pin : edge->getPins()) {
      const Node* other = other_pin->getNode();
      if (other == nullptr || other == node) {
        continue;
      }
      const int pin_x = (other->getCenterX() + other_pin->getOffsetX()).v;
      const int pin_y = (other->getCenterY() + other_pin->getOffsetY()).v;
      xmin = std::min(xmin, pin_x);
      xmax = std::max(xmax, pin_x);
      ymin = std::min(ymin, pin_y);
      ymax = std::max(ymax, pin_y);
    }
    if (xmin > xmax || ymin > ymax) {
      continue;
    }

    const int curr_pin_x = (node->getCenterX() + pin->getOffsetX()).v;
    const int curr_pin_y = (node->getCenterY() + pin->getOffsetY()).v;
    const int desired_pin_x = std::clamp(curr_pin_x, xmin, xmax);
    const int desired_pin_y = std::clamp(curr_pin_y, ymin, ymax);
    const double pressure = std::abs(desired_pin_x - curr_pin_x)
                            + (0.25 * std::abs(desired_pin_y - curr_pin_y));
    if (pressure <= 0.0) {
      continue;
    }

    const double target_center_x
        = desired_pin_x - static_cast<double>(pin->getOffsetX().v);
    const double target_center_y
        = desired_pin_y - static_cast<double>(pin->getOffsetY().v);
    ChangedNetNodeCandidate candidate;
    candidate.node = node;
    candidate.pin = pin;
    candidate.seg = segs[0]->getSegId();
    candidate.row = segs[0]->getRowId();
    candidate.target_left = static_cast<int>(
        std::llround(target_center_x - (0.5 * node->getWidth().v)));
    candidate.target_row = std::clamp(
        static_cast<int>((target_center_y - arch->getMinY().v) / row_height),
        0,
        mgr->getNumSingleHeightRows() - 1);
    candidate.pressure = pressure;

    auto [it, inserted] = bestByNode.emplace(node->getId(), candidate);
    if (!inserted && candidate.pressure > it->second.pressure) {
      it->second = candidate;
    }
  }

  bestCandidates.reserve(bestByNode.size());
  for (const auto& [ignored_node_id, candidate] : bestByNode) {
    bestCandidates.push_back(candidate);
  }
  std::sort(bestCandidates.begin(),
            bestCandidates.end(),
            [](const ChangedNetNodeCandidate& lhs,
               const ChangedNetNodeCandidate& rhs) {
              if (lhs.pressure == rhs.pressure) {
                return lhs.node->getId() < rhs.node->getId();
              }
              return lhs.pressure > rhs.pressure;
            });
  return bestCandidates;
}

int countMovedNodesOnEdge(const Journal& journal, Edge* edge)
{
  if (edge == nullptr) {
    return 0;
  }
  int moved = 0;
  for (const Pin* pin : edge->getPins()) {
    if (journal.getAffectedNodes().count(pin->getNode()) > 0) {
      moved++;
    }
  }
  return moved;
}

bool slotEquals(const ChangedNetAssignmentSlot& lhs,
                const ChangedNetAssignmentSlot& rhs)
{
  return lhs.left == rhs.left && lhs.bottom == rhs.bottom && lhs.seg == rhs.seg;
}

void addChangedNetAssignmentSlot(std::vector<ChangedNetAssignmentSlot>& slots,
                                 const ChangedNetAssignmentSlot& slot)
{
  if (slot.seg < 0) {
    return;
  }
  for (const auto& existing : slots) {
    if (slotEquals(existing, slot)) {
      return;
    }
  }
  slots.push_back(slot);
}

bool isSlotCurrentlyAvailable(DetailedMgr* mgr,
                              Node* node,
                              DbuX left,
                              int seg)
{
  if (mgr == nullptr || node == nullptr || seg < 0) {
    return false;
  }
  DetailedSeg* seg_ptr = mgr->getSegment(seg);
  if (left < seg_ptr->getMinX() || left + node->getWidth() > seg_ptr->getMaxX()) {
    return false;
  }
  for (Node* other : mgr->getCellsInSeg(seg)) {
    if (other == nullptr || other == node) {
      continue;
    }
    if (other->getLeft() < left) {
      const DbuX required_left
          = other->getLeft() + other->getWidth() + mgr->getCellSpacing(other, node);
      if (required_left > left) {
        return false;
      }
    } else {
      const DbuX required_other_left
          = left + node->getWidth() + mgr->getCellSpacing(node, other);
      if (required_other_left > other->getLeft()) {
        return false;
      }
    }
  }
  return true;
}

std::vector<ChangedNetAssignmentSlot> collectChangedNetAssignmentSlots(
    DetailedMgr* mgr,
    Architecture* arch,
    const std::vector<ChangedNetNodeCandidate>& net_nodes,
    const int site_width)
{
  std::vector<ChangedNetAssignmentSlot> slots;
  if (mgr == nullptr || arch == nullptr || net_nodes.size() < 2) {
    return slots;
  }

  const int node_limit = std::min(4, static_cast<int>(net_nodes.size()));
  for (int node_idx = 0; node_idx < node_limit; node_idx++) {
    const auto& candidate = net_nodes[node_idx];
    addChangedNetAssignmentSlot(
        slots,
        {candidate.node->getLeft(),
         candidate.node->getBottom(),
         candidate.seg,
         candidate.row,
         candidate.node,
         true});

    std::vector<std::pair<int, ChangedNetAssignmentSlot>> node_slots;
    std::vector<int> row_candidates;
    auto add_row = [&](const int row_id) {
      if (row_id < 0 || row_id >= mgr->getNumSingleHeightRows()) {
        return;
      }
      if (std::find(row_candidates.begin(), row_candidates.end(), row_id)
          == row_candidates.end()) {
        row_candidates.push_back(row_id);
      }
    };
    add_row(candidate.target_row);
    add_row(candidate.target_row - 1);
    add_row(candidate.target_row + 1);
    add_row(candidate.row);
    add_row(candidate.row - 1);
    add_row(candidate.row + 1);

    for (const int row_id : row_candidates) {
      for (DetailedSeg* seg_ptr : mgr->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != candidate.node->getGroupId()) {
          continue;
        }
        const int seg_min = seg_ptr->getMinX().v;
        const int seg_max = seg_ptr->getMaxX().v - candidate.node->getWidth().v;
        if (seg_max < seg_min) {
          continue;
        }
        const int clamped_left
            = std::max(seg_min, std::min(candidate.target_left, seg_max));
        for (const int site_delta : {0, -1, 1, -2, 2}) {
          DbuX candidate_left{clamped_left + (site_delta * site_width)};
          if (!mgr->alignPos(candidate.node,
                             candidate_left,
                             seg_ptr->getMinX(),
                             seg_ptr->getMaxX())) {
            continue;
          }
          const DbuY candidate_bottom = arch->getRow(row_id)->getBottom();
          if (candidate_left == candidate.node->getLeft()
              && candidate_bottom == candidate.node->getBottom()) {
            continue;
          }
          if (!isSlotCurrentlyAvailable(
                  mgr, candidate.node, candidate_left, seg_ptr->getSegId())) {
            continue;
          }
          const int target_cost = std::abs(candidate_left.v - candidate.target_left)
                                  + (site_width
                                     * std::abs(row_id - candidate.target_row));
          node_slots.push_back(
              {target_cost,
               {candidate_left,
                candidate_bottom,
                seg_ptr->getSegId(),
                row_id,
                nullptr,
                false}});
        }
      }
    }

    std::sort(node_slots.begin(),
              node_slots.end(),
              [](const auto& lhs, const auto& rhs) {
                if (lhs.first == rhs.first) {
                  return lhs.second.seg < rhs.second.seg;
                }
                return lhs.first < rhs.first;
              });
    int added_target_slots = 0;
    for (const auto& [ignored_cost, slot] : node_slots) {
      const size_t slot_count_before = slots.size();
      addChangedNetAssignmentSlot(slots, slot);
      if (slots.size() != slot_count_before) {
        added_target_slots++;
        if (added_target_slots >= 2) {
          break;
        }
      }
    }
  }
  return slots;
}

double computeChangedNetAssignmentProxy(
    const std::vector<DetailedMgr::AssignmentMove>& moves)
{
  if (moves.empty()) {
    return 0.0;
  }

  std::vector<DetailedHPWL::EdgePositionOverride> overrides;
  overrides.reserve(moves.size());
  for (const auto& move : moves) {
    overrides.push_back({move.node, move.newLeft, move.newBottom});
  }

  std::vector<Edge*> affected_edges;
  for (const auto& move : moves) {
    if (move.node == nullptr) {
      continue;
    }
    for (const Pin* pin : move.node->getPins()) {
      Edge* edge = pin->getEdge();
      if (edge == nullptr || edge->getNumPins() <= 1 || edge->getNumPins() > 16) {
        continue;
      }
      if (std::find(affected_edges.begin(), affected_edges.end(), edge)
          == affected_edges.end()) {
        affected_edges.push_back(edge);
      }
    }
  }
  if (affected_edges.empty()) {
    return 0.0;
  }

  double total_gain = 0.0;
  for (Edge* edge : affected_edges) {
    const uint64_t old_hpwl = edge->hpwl();
    const uint64_t new_hpwl = DetailedHPWL::edgeHpwlWithOverrides(edge, overrides);
    total_gain += static_cast<double>(old_hpwl) - static_cast<double>(new_hpwl);
  }
  return total_gain;
}

bool deriveNodeTargetPosition(DetailedMgr* mgr,
                              Architecture* arch,
                              Node* node,
                              int& target_left,
                              int& target_row,
                              double& pressure)
{
  pressure = 0.0;
  if (mgr == nullptr || arch == nullptr || node == nullptr || node->isFixed()
      || !node->isStdCell() || !arch->isSingleHeightCell(node)) {
    return false;
  }

  const auto& segs = mgr->getReverseCellToSegs(node->getId());
  if (segs.size() != 1) {
    return false;
  }

  const int row_height = std::max(1, mgr->getSingleRowHeight().v);
  const int src_row = segs[0]->getRowId();
  const int curr_center_x = node->getCenterX().v;
  const int curr_center_y = node->getCenterY().v;
  double target_center_x = curr_center_x;
  double target_center_y = curr_center_y;
  int x_votes = 0;
  int y_votes = 0;
  double fallback_span = -1.0;
  double fallback_x = curr_center_x;
  double fallback_y = curr_center_y;

  for (const Pin* pin : node->getPins()) {
    Edge* edge = pin->getEdge();
    if (edge == nullptr) {
      continue;
    }
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins > 16) {
      continue;
    }

    int xmin = std::numeric_limits<int>::max();
    int xmax = std::numeric_limits<int>::min();
    int ymin = std::numeric_limits<int>::max();
    int ymax = std::numeric_limits<int>::min();
    for (const Pin* edge_pin : edge->getPins()) {
      const Node* other = edge_pin->getNode();
      if (other == nullptr || other == node) {
        continue;
      }
      const int pin_x = (other->getCenterX() + edge_pin->getOffsetX()).v;
      const int pin_y = (other->getCenterY() + edge_pin->getOffsetY()).v;
      xmin = std::min(xmin, pin_x);
      xmax = std::max(xmax, pin_x);
      ymin = std::min(ymin, pin_y);
      ymax = std::max(ymax, pin_y);
    }
    if (xmin > xmax || ymin > ymax) {
      continue;
    }

    const int curr_pin_x = (node->getCenterX() + pin->getOffsetX()).v;
    const int curr_pin_y = (node->getCenterY() + pin->getOffsetY()).v;
    const int desired_pin_x = std::clamp(curr_pin_x, xmin, xmax);
    const int desired_pin_y = std::clamp(curr_pin_y, ymin, ymax);
    if (desired_pin_x != curr_pin_x) {
      target_center_x += desired_pin_x - pin->getOffsetX().v;
      x_votes++;
    }
    if (desired_pin_y != curr_pin_y) {
      target_center_y += desired_pin_y - pin->getOffsetY().v;
      y_votes++;
    }

    const double span = static_cast<double>((xmax - xmin) + (ymax - ymin));
    if (span > fallback_span) {
      fallback_span = span;
      fallback_x = (0.5 * static_cast<double>(xmin + xmax))
                   - static_cast<double>(pin->getOffsetX().v);
      fallback_y = (0.5 * static_cast<double>(ymin + ymax))
                   - static_cast<double>(pin->getOffsetY().v);
    }
  }

  if (fallback_span < 0.0 && x_votes == 0 && y_votes == 0) {
    return false;
  }
  if (x_votes > 0) {
    target_center_x /= (x_votes + 1);
  } else {
    target_center_x = fallback_x;
  }
  if (y_votes > 0) {
    target_center_y /= (y_votes + 1);
  } else {
    target_center_y = fallback_y;
  }

  target_left = static_cast<int>(
      std::llround(target_center_x - (0.5 * node->getWidth().v)));
  target_row = std::clamp(
      static_cast<int>((target_center_y - arch->getMinY().v) / row_height),
      0,
      mgr->getNumSingleHeightRows() - 1);
  pressure = std::abs(target_left - node->getLeft().v)
             + (0.25 * row_height * std::abs(target_row - src_row));
  return pressure > 0.0;
}

std::vector<PayloadWindowNodeCandidate> collectPayloadWindowNodeCandidates(
    DetailedMgr* mgr,
    Architecture* arch,
    const DetailedMgr::ExactPolishPayloadWindow& window,
    const std::unordered_set<int>& payload_ids,
    const std::unordered_set<int>& focus_ids)
{
  std::vector<PayloadWindowNodeCandidate> best_candidates;
  if (mgr == nullptr || arch == nullptr || window.row_id < 0) {
    return best_candidates;
  }

  const int site_halo = arch->getRow(0)->getSiteSpacing().v * 8;
  const int x_min = window.x_min.v - site_halo;
  const int x_max = window.x_max.v + site_halo;
  const int x_mid = x_min + ((x_max - x_min) / 2);
  std::map<int, PayloadWindowNodeCandidate> best_by_node;
  for (int row_id = std::max(0, window.row_id - 1);
       row_id <= std::min(mgr->getNumSingleHeightRows() - 1, window.row_id + 1);
       row_id++) {
    for (DetailedSeg* seg_ptr : mgr->getSegsInRow(row_id)) {
      if (seg_ptr == nullptr || seg_ptr->getMaxX().v < x_min
          || seg_ptr->getMinX().v > x_max) {
        continue;
      }
      for (Node* node : mgr->getCellsInSeg(seg_ptr->getSegId())) {
        if (node == nullptr || node->isFixed() || !node->isStdCell()
            || !arch->isSingleHeightCell(node) || node->getId() < 0) {
          continue;
        }
        if (node->getRight().v < x_min || node->getLeft().v > x_max) {
          continue;
        }

        int target_left = 0;
        int target_row = -1;
        double pressure = 0.0;
        if (!deriveNodeTargetPosition(
                mgr, arch, node, target_left, target_row, pressure)) {
          continue;
        }

        PayloadWindowNodeCandidate candidate;
        candidate.node = node;
        candidate.seg = seg_ptr->getSegId();
        candidate.row = row_id;
        candidate.target_left = target_left;
        candidate.target_row = target_row;
        candidate.pressure = pressure;
        candidate.payload_seed = payload_ids.count(node->getId()) > 0;
        candidate.focus_seed = focus_ids.count(node->getId()) > 0;
        candidate.dist_to_window
            = std::abs(node->getCenterX().v - x_mid) + std::abs(row_id - window.row_id);

        auto [it, inserted] = best_by_node.emplace(node->getId(), candidate);
        if (!inserted) {
          const auto& current = it->second;
          if ((candidate.payload_seed && !current.payload_seed)
              || (candidate.payload_seed == current.payload_seed
                  && candidate.focus_seed && !current.focus_seed)
              || (candidate.payload_seed == current.payload_seed
                  && candidate.focus_seed == current.focus_seed
                  && (candidate.pressure > current.pressure
                      || (candidate.pressure == current.pressure
                          && candidate.dist_to_window < current.dist_to_window)))) {
            it->second = candidate;
          }
        }
      }
    }
  }

  best_candidates.reserve(best_by_node.size());
  for (const auto& [ignored_id, candidate] : best_by_node) {
    best_candidates.push_back(candidate);
  }
  std::sort(best_candidates.begin(),
            best_candidates.end(),
            [](const PayloadWindowNodeCandidate& lhs,
               const PayloadWindowNodeCandidate& rhs) {
              if (lhs.payload_seed == rhs.payload_seed) {
                if (lhs.focus_seed == rhs.focus_seed) {
                  if (lhs.pressure == rhs.pressure) {
                    if (lhs.dist_to_window == rhs.dist_to_window) {
                      return lhs.node->getId() < rhs.node->getId();
                    }
                    return lhs.dist_to_window < rhs.dist_to_window;
                  }
                  return lhs.pressure > rhs.pressure;
                }
                return lhs.focus_seed && !rhs.focus_seed;
              }
              return lhs.payload_seed && !rhs.payload_seed;
            });

  std::vector<PayloadWindowNodeCandidate> seeded_candidates;
  std::vector<PayloadWindowNodeCandidate> unseeded_candidates;
  seeded_candidates.reserve(best_candidates.size());
  unseeded_candidates.reserve(best_candidates.size());
  for (const auto& candidate : best_candidates) {
    if (candidate.payload_seed || candidate.focus_seed) {
      seeded_candidates.push_back(candidate);
    } else {
      unseeded_candidates.push_back(candidate);
    }
  }

  if (seeded_candidates.size() >= 2) {
    std::vector<PayloadWindowNodeCandidate> compact_candidates;
    compact_candidates.reserve(4);
    for (const auto& candidate : seeded_candidates) {
      compact_candidates.push_back(candidate);
      if (compact_candidates.size() >= 3) {
        break;
      }
    }
    for (const auto& candidate : unseeded_candidates) {
      compact_candidates.push_back(candidate);
      if (compact_candidates.size() >= 4) {
        break;
      }
    }
    return compact_candidates;
  }

  if (best_candidates.size() > 4) {
    best_candidates.resize(4);
  }
  return best_candidates;
}

std::vector<ChangedNetAssignmentSlot> collectPayloadWindowAssignmentSlots(
    DetailedMgr* mgr,
    Architecture* arch,
    const DetailedMgr::ExactPolishPayloadWindow& window,
    const std::vector<PayloadWindowNodeCandidate>& window_nodes,
    const int site_width)
{
  std::vector<ChangedNetAssignmentSlot> slots;
  if (mgr == nullptr || arch == nullptr || window_nodes.size() < 2) {
    return slots;
  }

  const int site_halo = site_width * 8;
  const int x_min = window.x_min.v - site_halo;
  const int x_max = window.x_max.v + site_halo;
  const int node_limit = std::min(4, static_cast<int>(window_nodes.size()));
  for (int node_idx = 0; node_idx < node_limit; node_idx++) {
    const auto& candidate = window_nodes[node_idx];
    addChangedNetAssignmentSlot(
        slots,
        {candidate.node->getLeft(),
         candidate.node->getBottom(),
         candidate.seg,
         candidate.row,
         candidate.node,
         true});

    std::vector<std::pair<int, ChangedNetAssignmentSlot>> node_slots;
    std::vector<int> row_candidates;
    auto add_row = [&](const int row_id) {
      if (row_id < 0 || row_id >= mgr->getNumSingleHeightRows()) {
        return;
      }
      if (std::abs(row_id - window.row_id) > 1
          && std::abs(row_id - candidate.target_row) > 1) {
        return;
      }
      if (std::find(row_candidates.begin(), row_candidates.end(), row_id)
          == row_candidates.end()) {
        row_candidates.push_back(row_id);
      }
    };
    add_row(window.row_id);
    add_row(window.row_id - 1);
    add_row(window.row_id + 1);
    add_row(candidate.target_row);
    add_row(candidate.target_row - 1);
    add_row(candidate.target_row + 1);
    add_row(candidate.row);

    for (const int row_id : row_candidates) {
      for (DetailedSeg* seg_ptr : mgr->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != candidate.node->getGroupId()) {
          continue;
        }
        const int seg_min = std::max(seg_ptr->getMinX().v, x_min);
        const int seg_max
            = std::min(seg_ptr->getMaxX().v - candidate.node->getWidth().v,
                       x_max - candidate.node->getWidth().v);
        if (seg_max < seg_min) {
          continue;
        }
        const int clamped_left
            = std::max(seg_min, std::min(candidate.target_left, seg_max));
        for (const int site_delta : {0, -1, 1, -2, 2}) {
          DbuX candidate_left{clamped_left + (site_delta * site_width)};
          if (!mgr->alignPos(candidate.node,
                             candidate_left,
                             DbuX{seg_min},
                             DbuX{seg_max + candidate.node->getWidth().v})) {
            continue;
          }
          const DbuY candidate_bottom = arch->getRow(row_id)->getBottom();
          if (candidate_left == candidate.node->getLeft()
              && candidate_bottom == candidate.node->getBottom()) {
            continue;
          }
          if (candidate_left.v < x_min
              || (candidate_left + candidate.node->getWidth()).v > x_max) {
            continue;
          }
          if (!isSlotCurrentlyAvailable(
                  mgr, candidate.node, candidate_left, seg_ptr->getSegId())) {
            continue;
          }
          const int target_cost
              = std::abs(candidate_left.v - candidate.target_left)
                + (site_width * std::abs(row_id - candidate.target_row));
          node_slots.push_back(
              {target_cost,
               {candidate_left,
                candidate_bottom,
                seg_ptr->getSegId(),
                row_id,
                nullptr,
                false}});
        }
      }
    }

    std::sort(node_slots.begin(),
              node_slots.end(),
              [](const auto& lhs, const auto& rhs) {
                if (lhs.first == rhs.first) {
                  return lhs.second.seg < rhs.second.seg;
                }
                return lhs.first < rhs.first;
              });
    int added_target_slots = 0;
    for (const auto& [ignored_cost, slot] : node_slots) {
      const size_t slot_count_before = slots.size();
      addChangedNetAssignmentSlot(slots, slot);
      if (slots.size() != slot_count_before) {
        added_target_slots++;
        if (added_target_slots >= 2) {
          break;
        }
      }
    }
  }
  return slots;
}

template <typename CandidateVec>
ChangedNetAssignmentPlan findBestAssignmentPlan(
    DetailedMgr* mgr,
    const CandidateVec& node_candidates,
    const std::vector<ChangedNetAssignmentSlot>& slot_pool,
    const int site_width,
    const int max_node_candidates,
    const int max_slots_per_node,
    int& assignment_proposals,
    int& proxy_positive_assignments)
{
  ChangedNetAssignmentPlan best_plan;
  if (mgr == nullptr || node_candidates.size() < 2 || slot_pool.size() < 2) {
    return best_plan;
  }

  const int node_limit = std::min(max_node_candidates,
                                  static_cast<int>(node_candidates.size()));
  auto evaluate_assignment = [&](const std::vector<int>& node_indices,
                                 const std::vector<int>& slot_indices) {
    std::vector<DetailedMgr::AssignmentMove> moves;
    moves.reserve(node_indices.size());
    for (size_t idx = 0; idx < node_indices.size(); idx++) {
      const auto& node_candidate = node_candidates[node_indices[idx]];
      const auto& slot = slot_pool[slot_indices[idx]];
      moves.push_back({node_candidate.node,
                       node_candidate.node->getLeft(),
                       node_candidate.node->getBottom(),
                       node_candidate.seg,
                       slot.left,
                       slot.bottom,
                       slot.seg});
    }
    assignment_proposals++;
    if (!mgr->isMoveAssignmentFeasible(moves)) {
      return;
    }
    const double proxy_gain = computeChangedNetAssignmentProxy(moves);
    if (proxy_gain <= 0.0) {
      return;
    }
    proxy_positive_assignments++;
    if (proxy_gain > best_plan.proxy_gain) {
      best_plan.moves = std::move(moves);
      best_plan.proxy_gain = proxy_gain;
    }
  };

  for (int move_count = std::min(3, node_limit); move_count >= 2; move_count--) {
    std::vector<int> node_indices(move_count, 0);
    std::function<void(int, int)> choose_nodes = [&](int depth, int start) {
      if (depth == move_count) {
        std::set<Node*> selected_nodes;
        for (const int node_idx : node_indices) {
          selected_nodes.insert(node_candidates[node_idx].node);
        }

        std::vector<std::vector<int>> eligible_slots(move_count);
        for (int local_idx = 0; local_idx < move_count; local_idx++) {
          const auto& node_candidate = node_candidates[node_indices[local_idx]];
          std::vector<std::pair<int, int>> ranked_slots;
          for (int slot_idx = 0; slot_idx < static_cast<int>(slot_pool.size());
               slot_idx++) {
            const auto& slot = slot_pool[slot_idx];
            if (mgr->getSegment(slot.seg)->getRegId()
                != node_candidate.node->getGroupId()) {
              continue;
            }
            if (slot.current_slot && selected_nodes.count(slot.owner) == 0) {
              continue;
            }
            if (slot.left == node_candidate.node->getLeft()
                && slot.bottom == node_candidate.node->getBottom()) {
              continue;
            }
            const int rank_cost
                = std::abs(slot.left.v - node_candidate.target_left)
                  + (site_width
                     * std::abs(slot.row - node_candidate.target_row));
            ranked_slots.push_back({rank_cost, slot_idx});
          }
          std::sort(ranked_slots.begin(),
                    ranked_slots.end(),
                    [](const auto& lhs, const auto& rhs) {
                      if (lhs.first == rhs.first) {
                        return lhs.second < rhs.second;
                      }
                      return lhs.first < rhs.first;
                    });
          for (const auto& [ignored_cost, slot_idx] : ranked_slots) {
            eligible_slots[local_idx].push_back(slot_idx);
            if (eligible_slots[local_idx].size()
                >= static_cast<size_t>(max_slots_per_node)) {
              break;
            }
          }
          if (eligible_slots[local_idx].empty()) {
            return;
          }
        }

        std::vector<int> chosen_slots(move_count, -1);
        std::set<int> used_slots;
        std::function<void(int)> choose_slots = [&](int depth_slots) {
          if (depth_slots == move_count) {
            evaluate_assignment(node_indices, chosen_slots);
            return;
          }
          for (const int slot_idx : eligible_slots[depth_slots]) {
            if (!used_slots.insert(slot_idx).second) {
              continue;
            }
            chosen_slots[depth_slots] = slot_idx;
            choose_slots(depth_slots + 1);
            used_slots.erase(slot_idx);
          }
        };
        choose_slots(0);
        return;
      }

      for (int idx = start; idx <= node_limit - (move_count - depth); idx++) {
        node_indices[depth] = idx;
        choose_nodes(depth + 1, idx + 1);
      }
    };
    choose_nodes(0, 0);
  }

  return best_plan;
}

}  // namespace

bool DetailedRandom::isOperator(char ch) const
{
  return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^';
}

bool DetailedRandom::isObjective(char ch) const
{
  return ch >= 'a' && ch <= 'z';
}

bool DetailedRandom::isNumber(char ch) const
{
  return ch >= '0' && ch <= '9';
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedRandom::DetailedRandom(Architecture* arch, Network* network)
    : arch_(arch), network_(network)
{
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::run(DetailedMgr* mgrPtr, const std::string& command)
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

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::run(DetailedMgr* mgrPtr, std::vector<std::string>& args)
{
  // This is, more or less, a greedy or low temperature anneal.  It is capable
  // of handling very complex objectives, etc.  There should be a lot of
  // arguments provided actually.  But, right now, I am just getting started.

  mgrPtr_ = mgrPtr;

  std::string generatorStr;
  std::string objectiveStr;
  std::string costStr;
  movesPerCandidate_ = 3.0;
  int passes = 1;
  double tol = 0.01;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-f" && i + 1 < args.size()) {
      movesPerCandidate_ = std::atof(args[++i].c_str());
    } else if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    } else if (args[i] == "-gen" && i + 1 < args.size()) {
      generatorStr = args[++i];
    } else if (args[i] == "-obj" && i + 1 < args.size()) {
      objectiveStr = args[++i];
    } else if (args[i] == "-cost" && i + 1 < args.size()) {
      costStr = args[++i];
    } else if (args[i] == "-focus" && i + 1 < args.size()) {
      const std::string focusStr = args[++i];
      if (focusStr == "polish") {
        focusMode_ = FocusMode_ExactPolish;
      } else {
        focusMode_ = FocusMode_All;
      }
    } else if (args[i] == "-focus_limit" && i + 1 < args.size()) {
      focusCandidateLimit_ = std::atoi(args[++i].c_str());
    }
  }
  tol = std::max(tol, 0.01);
  passes = std::max(passes, 1);
  focusCandidateLimit_ = std::max(16, focusCandidateLimit_);

  // Generators.
  for (auto generator : generators_) {
    delete generator;
  }
  generators_.clear();

  // Additional generators per the command. XXX: Need to write the code for
  // these objects; just a concept now.
  if (!generatorStr.empty()) {
    boost::char_separator<char> separators(" \r\t\n:");
    boost::tokenizer<boost::char_separator<char>> tokens(generatorStr,
                                                         separators);
    std::vector<std::string> gens;
    for (const auto& token : tokens) {
      gens.push_back(token);
    }

    for (const auto& gen : gens) {
      // if( gens[i] == "ro" )       std::cout << "reorder generator requested."
      // << std::endl; else if( gens[i] == "mis" ) std::cout << "set matching
      // generator requested." << std::endl;
      if (gen == "gs") {
        generators_.push_back(new DetailedGlobalSwap());
      } else if (gen == "vs") {
        generators_.push_back(new DetailedVerticalSwap());
      } else if (gen == "rng") {
        generators_.push_back(new RandomGenerator());
      } else if (gen == "disp") {
        generators_.push_back(new DisplacementGenerator());
      } else if (gen == "tpair") {
        generators_.push_back(new TouchedPairGenerator());
      } else if (gen == "tend") {
        generators_.push_back(new TouchedEndpointGenerator());
      } else if (gen == "ttxn") {
        generators_.push_back(new ChangedNetTransactionGenerator());
      }
    }
  }
  if (generators_.empty()) {
    // Default generator.
    generators_.push_back(new RandomGenerator());
  }
  for (auto generator : generators_) {
    generator->init(mgrPtr_);

    mgrPtr_->getLogger()->info(DPL,
                               324,
                               "Random improver is using {:s} generator.",
                               generator->getName().c_str());
  }

  // Objectives.
  for (auto objective : objectives_) {
    delete objective;
  }
  objectives_.clear();

  // Additional objectives per the command. XXX: Need to write the code for
  // these objects; just a concept now.
  if (!objectiveStr.empty()) {
    boost::char_separator<char> separators(" \r\t\n:");
    boost::tokenizer<boost::char_separator<char>> tokens(objectiveStr,
                                                         separators);
    std::vector<std::string> objs;
    for (boost::tokenizer<boost::char_separator<char>>::iterator it
         = tokens.begin();
         it != tokens.end();
         it++) {
      objs.push_back(*it);
    }

    for (const auto& obj : objs) {
      if (obj == "abu") {
        auto objABU = new DetailedABU(arch_, network_);
        objABU->init(mgrPtr_, nullptr);
        objectives_.push_back(objABU);
      } else if (obj == "disp") {
        auto objDisp = new DetailedDisplacement(arch_);
        objDisp->init(mgrPtr_, nullptr);
        objectives_.push_back(objDisp);
      } else if (obj == "hpwl") {
        auto objHpwl = new DetailedHPWL(network_);
        objHpwl->init(mgrPtr_, nullptr);
        objectives_.push_back(objHpwl);
      }
    }
  }
  if (objectives_.empty()) {
    // Default objective.
    auto objHpwl = new DetailedHPWL(network_);
    objHpwl->init(mgrPtr_, nullptr);
    objectives_.push_back(objHpwl);
  }

  for (auto objective : objectives_) {
    mgrPtr_->getLogger()->info(DPL,
                               325,
                               "Random improver is using {:s} objective.",
                               objective->getName().c_str());
  }

  // Should I just be figuring out the objectives needed from the cost string?
  if (!costStr.empty()) {
    // Replace substrings of objectives with a number.
    for (size_t i = objectives_.size(); i > 0;) {
      --i;
      for (;;) {
        size_t pos = costStr.find(objectives_[i]->getName());
        if (pos == std::string::npos) {
          break;
        }
        std::string val;
        val.append(1, (char) ('a' + i));
        costStr.replace(pos, objectives_[i]->getName().length(), val);
      }
    }

    mgrPtr_->getLogger()->info(
        DPL, 326, "Random improver cost string is {:s}.", costStr.c_str());

    expr_.clear();
    for (std::string::iterator it = costStr.begin(); it != costStr.end();
         ++it) {
      if (*it == '(' || *it == ')') {
      } else if (isOperator(*it) || isObjective(*it)) {
        expr_.emplace_back(1, *it);
      } else {
        std::string val;
        while (!isOperator(*it) && !isObjective(*it) && it != costStr.end()
               && *it != '(' && *it != ')') {
          val.append(1, *it);
          ++it;
        }
        expr_.push_back(val);
        --it;
      }
    }
  } else {
    expr_.clear();
    expr_.emplace_back(1, 'a');
    for (size_t i = 1; i < objectives_.size(); i++) {
      expr_.emplace_back(1, (char) ('a' + i));
      expr_.emplace_back(1, '+');
    }
  }

  currCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    currCost_[i] = objectives_[i]->curr();
  }
  double iCost = eval(currCost_, expr_);

  for (int p = 1; p <= passes; p++) {
    mgrPtr_->resortSegments();  // Needed?
    double change = go();
    mgrPtr_->getLogger()->info(
        DPL,
        327,
        "Pass {:3d} of random improver; improvement in cost is {:.2f} percent.",
        p,
        (change * 100));
    if (change < tol) {
      break;
    }
  }
  mgrPtr_->resortSegments();  // Needed?

  currCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    currCost_[i] = objectives_[i]->curr();
  }
  double fCost = eval(currCost_, expr_);

  double imp = (((iCost - fCost) / iCost) * 100.);
  mgrPtr_->getLogger()->info(
      DPL, 328, "End of random improver; improvement is {:.6f} percent.", imp);

  // Cleanup.
  for (auto generator : generators_) {
    delete generator;
  }
  generators_.clear();
  for (auto objective : objectives_) {
    delete objective;
  }
  objectives_.clear();
}

double DetailedRandom::doOperation(double a, double b, char op) const
{
  switch (op) {
    case '+':
      return b + a;
      break;
    case '-':
      return b - a;
      break;
    case '*':
      return b * a;
      break;
    case '/':
      return b / a;
      break;
    case '^':
      return std::pow(b, a);
      break;
    default:
      break;
  }
  return 0.0;
}

double DetailedRandom::eval(const std::vector<double>& costs,
                            const std::vector<std::string>& expr) const
{
  std::stack<double> stk;
  for (const std::string& val : expr) {
    if (isOperator(val[0])) {
      double a = stk.top();
      stk.pop();
      double b = stk.top();
      stk.pop();
      stk.push(doOperation(a, b, val[0]));
    } else if (isObjective(val[0])) {
      stk.push(costs[(int) (val[0] - 'a')]);
    } else {
      // Assume number.
      stk.push(std::stod(val));
    }
  }
  if (stk.size() != 1) {
    // Cost function should never be negative.  If we have a problem,
    // then return some negative value and we can catch this error.
    return -1.0;
  }
  return stk.top();
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
double DetailedRandom::go()
{
  if (generators_.empty()) {
    mgrPtr_->getLogger()->info(
        DPL, 329, "Random improver requires at least one generator.");
    return 0.0;
  }

  // Collect candidate cells.
  collectCandidates();
  if (candidates_.empty()) {
    mgrPtr_->getLogger()->info(DPL, 203, "No movable cells found");
    return 0.0;
  }

  // Try to improve.
  int maxAttempts
      = (int) std::ceil(movesPerCandidate_ * (double) candidates_.size());
  mgrPtr_->shuffle(candidates_);

  if (focusMode_ == FocusMode_ExactPolish) {
    const auto pair_nodes = mgrPtr_->getExactPolishPairNodes(focusCandidateLimit_);
    const auto endpoint_nodes
        = mgrPtr_->getExactPolishEndpointNodes(focusCandidateLimit_);
    const auto payload_nodes
        = mgrPtr_->getExactPolishPayloadNodes(focusCandidateLimit_);
    mgrPtr_->getLogger()->info(
        DPL,
        342,
        "Focused exact polish candidates: pairs={} endpoints={} payload={} "
        "union={} "
        "limit={}.",
        pair_nodes.size(),
        endpoint_nodes.size(),
        payload_nodes.size(),
        candidates_.size(),
        focusCandidateLimit_);
  }

  deltaCost_.resize(objectives_.size());
  initCost_.resize(objectives_.size());
  currCost_.resize(objectives_.size());
  nextCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    deltaCost_[i] = 0.;
    initCost_[i] = objectives_[i]->curr();
    currCost_[i] = initCost_[i];
    nextCost_[i] = initCost_[i];

    if (objectives_[i]->getName() == "abu") {
      auto ptr = dynamic_cast<DetailedABU*>(objectives_[i]);
      if (ptr != nullptr) {
        ptr->measureABU(true);
      }
    }
  }

  // Test.
  if (eval(currCost_, expr_) < 0.0) {
    mgrPtr_->getLogger()->info(DPL,
                               330,
                               "Test objective function failed, possibly due "
                               "to a badly formed cost function.");
    return 0.0;
  }

  double currTotalCost;
  double initTotalCost;
  initTotalCost = eval(currCost_, expr_);
  currTotalCost = initTotalCost;

  std::vector<int> gen_count(generators_.size());
  std::vector<int> gen_generated(generators_.size());
  std::vector<int> gen_accepted(generators_.size());
  std::vector<int> gen_rejected(generators_.size());
  std::vector<int> gen_failed(generators_.size());
  std::vector<double> gen_accepted_delta(generators_.size());
  FocusRefreshStats focus_refresh_stats;
  std::ranges::fill(gen_count, 0);
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    // Pick a generator at random.
    int g = mgrPtr_->getRandom(generators_.size());
    ++gen_count[g];
    // Generate a move list.
    if (!generators_[g]->generate(mgrPtr_, candidates_)) {
      // Failed to generate anything so just move on to the next attempt.
      ++gen_failed[g];
      continue;
    }
    ++gen_generated[g];

    // The generator has provided a successful move which is stored in the
    // manager.  We need to evaluate that move to see if we should accept
    // or reject it.  Scan over the objective functions and use the move
    // information to compute the weighted deltas; an overall weighted delta
    // better than zero implies improvement.
    for (size_t i = 0; i < objectives_.size(); i++) {
      // XXX: NEED TO WEIGHT EACH OBJECTIVE!
      double change = objectives_[i]->delta(mgrPtr_->getJournal());

      deltaCost_[i] = change;
      nextCost_[i] = currCost_[i] - deltaCost_[i];  // -delta is +ve is less.
    }
    const double nextTotalCost = eval(nextCost_, expr_);
    if (nextTotalCost <= currTotalCost) {
      gen_accepted[g] += 1;
      const double acceptedGain = currTotalCost - nextTotalCost;
      gen_accepted_delta[g] += acceptedGain;
      if (focusMode_ == FocusMode_ExactPolish) {
        const auto refresh_stats = noteAcceptedExactPolishFrontier(
            mgrPtr_, mgrPtr_->getJournal(), acceptedGain);
        focus_refresh_stats.accepted_moves += refresh_stats.accepted_moves;
        focus_refresh_stats.anchor_nodes += refresh_stats.anchor_nodes;
        focus_refresh_stats.frontier_edges += refresh_stats.frontier_edges;
        focus_refresh_stats.frontier_nodes += refresh_stats.frontier_nodes;
        focus_refresh_stats.payload_windows += refresh_stats.payload_windows;
        focus_refresh_stats.payload_rows += refresh_stats.payload_rows;
        focus_refresh_stats.payload_nodes += refresh_stats.payload_nodes;
      }
      generators_[g]->noteAccepted(acceptedGain, mgrPtr_->getJournal());
      mgrPtr_->acceptMove();
      for (auto objective : objectives_) {
        objective->accept();
      }

      // A great, but time-consuming, check here is to recompute the costs from
      // scratch and make sure they are the same as the incrementally computed
      // costs.  Very useful for debugging!  Could do this check ever so often
      // or just at the end...
      ;
      for (size_t i = 0; i < objectives_.size(); i++) {
        currCost_[i] = nextCost_[i];
      }
      currTotalCost = nextTotalCost;
    } else {
      gen_rejected[g] += 1;
      generators_[g]->noteRejected();
      mgrPtr_->rejectMove();
      for (auto objective : objectives_) {
        objective->reject();
      }
    }
  }
  for (size_t i = 0; i < gen_count.size(); i++) {
    mgrPtr_->getLogger()->info(DPL,
                               332,
                               "End of pass, Generator {:s} called {:d} times.",
                               generators_[i]->getName().c_str(),
                               gen_count[i]);
  }
  for (auto generator : generators_) {
    generator->stats();
  }
  for (size_t i = 0; i < generators_.size(); i++) {
    mgrPtr_->getLogger()->info(
        DPL,
        343,
        "Focused generator {:s}: probes={} generated={} accepted={} "
        "rejected={} replay_failures={} accepted_delta={:.2f}.",
        generators_[i]->getName().c_str(),
        gen_count[i],
        gen_generated[i],
        gen_accepted[i],
        gen_rejected[i],
        gen_failed[i],
        gen_accepted_delta[i]);
  }
  if (focusMode_ == FocusMode_ExactPolish) {
    mgrPtr_->getLogger()->info(
        DPL,
        346,
        "Focused exact polish refresh: accepted_moves={} anchor_nodes={} "
        "frontier_edges={} frontier_nodes={} payload_windows={} "
        "payload_rows={} payload_nodes={}.",
        focus_refresh_stats.accepted_moves,
        focus_refresh_stats.anchor_nodes,
        focus_refresh_stats.frontier_edges,
        focus_refresh_stats.frontier_nodes,
        focus_refresh_stats.payload_windows,
        focus_refresh_stats.payload_rows,
        focus_refresh_stats.payload_nodes);
  }

  for (size_t i = 0; i < objectives_.size(); i++) {
    double scratch = objectives_[i]->curr();
    nextCost_[i] = scratch;  // Temporary.
    bool error = (std::fabs(scratch - currCost_[i]) > 1.0e-3);
    mgrPtr_->getLogger()->info(
        DPL,
        333,
        "End of pass, Objective {:s}, Initial cost {:.6e}, Scratch cost "
        "{:.6e}, Incremental cost {:.6e}, Mismatch? {:c}",
        objectives_[i]->getName().c_str(),
        initCost_[i],
        scratch,
        currCost_[i],
        ((error) ? 'Y' : 'N'));

    if (objectives_[i]->getName() == "abu") {
      auto ptr = dynamic_cast<DetailedABU*>(objectives_[i]);
      if (ptr != nullptr) {
        ptr->measureABU(true);
      }
    }
  }
  const double nextTotalCost = eval(nextCost_, expr_);
  mgrPtr_->getLogger()->info(
      DPL, 338, "End of pass, Total cost is {:.6e}.", nextTotalCost);

  return ((initTotalCost - currTotalCost) / initTotalCost);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::collectCandidates()
{
  candidates_.clear();
  if (focusMode_ == FocusMode_ExactPolish) {
    candidates_ = mgrPtr_->getExactPolishNodes(focusCandidateLimit_);
    return;
  }
  candidates_.insert(candidates_.end(),
                     mgrPtr_->getSingleHeightCells().begin(),
                     mgrPtr_->getSingleHeightCells().end());
  for (size_t i = 2; i < mgrPtr_->getNumMultiHeights(); i++) {
    candidates_.insert(candidates_.end(),
                       mgrPtr_->getMultiHeightCells(i).begin(),
                       mgrPtr_->getMultiHeightCells(i).end());
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
RandomGenerator::RandomGenerator() : DetailedGenerator("random")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool RandomGenerator::generate(DetailedMgr* mgr, std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();

  const int ydim = mgr_->getNumSingleHeightRows();
  double xwid = arch_->getRow(0)->getSiteSpacing().v;
  const int xdim
      = std::max(0, (int) ((arch_->getMaxX() - arch_->getMinX()).v / xwid));

  xwid = (arch_->getMaxX() - arch_->getMinX()).v / (double) xdim;
  double ywid = (arch_->getMaxY() - arch_->getMinY()).v / (double) ydim;

  Node* ndi = candidates[mgr_->getRandom(candidates.size())];
  const int spanned_i = arch_->getCellHeightInRows(ndi);
  if (spanned_i != 1) {
    return false;
  }
  // Segments for the source.
  const std::vector<DetailedSeg*>& segs_i
      = mgr_->getReverseCellToSegs(ndi->getId());
  if (segs_i.size() != 1) {
    mgr_->getLogger()->error(
        DPL, 385, "Only working with single height cells currently.");
  }

  // For the window size.  This should be parameterized.
  const int rly = 10;
  const int rlx = 10;

  const int tries = 5;
  for (int t = 1; t <= tries; t++) {
    // Position of the source.
    const double yi = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
    const double xi = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

    // Segment for the source.
    const int si = segs_i[0]->getSegId();

    // Random position within a box centered about (xi,yi).
    const int grid_xi = std::min(
        xdim - 1, std::max(0, (int) ((xi - arch_->getMinX().v) / xwid)));
    const int grid_yi = std::min(
        ydim - 1, std::max(0, (int) ((yi - arch_->getMinY().v) / ywid)));

    const int rel_x = mgr_->getRandom(2 * rlx + 1);
    const int rel_y = mgr_->getRandom(2 * rly + 1);

    const int grid_xj
        = std::min(xdim - 1, std::max(0, (grid_xi - rlx + rel_x)));
    const int grid_yj
        = std::min(ydim - 1, std::max(0, (grid_yi - rly + rel_y)));

    // Position of the destination.
    const double xj = arch_->getMinX().v + grid_xj * xwid;
    double yj = arch_->getMinY().v + grid_yj * ywid;

    // Row and segment for the destination.
    int rj = (int) ((yj - arch_->getMinY().v) / mgr_->getSingleRowHeight().v);
    rj = std::min(mgr_->getNumSingleHeightRows() - 1, std::max(0, rj));
    yj = arch_->getRow(rj)->getBottom().v;
    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      const DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }

    // Need to determine validity of things.
    if (sj == -1 || ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
      // The target segment cannot support the candidate cell.
      continue;
    }

    if (mgr_->tryMove(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++moves_;
      return true;
    }
    if (mgr_->trySwap(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++swaps_;
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void RandomGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      335,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DisplacementGenerator::DisplacementGenerator()
    : DetailedGenerator("displacement")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DisplacementGenerator::generate(DetailedMgr* mgr,
                                     std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();

  const int ydim = mgr_->getNumSingleHeightRows();
  double xwid = arch_->getRow(0)->getSiteSpacing().v;
  const int xdim
      = std::max(0, (int) ((arch_->getMaxX() - arch_->getMinX()).v / xwid));

  xwid = (arch_->getMaxX() - arch_->getMinX()).v / (double) xdim;
  double ywid = (arch_->getMaxY() - arch_->getMinY()).v / (double) ydim;

  Node* ndi = candidates[mgr_->getRandom(candidates.size())];

  // Segments for the source.
  const std::vector<DetailedSeg*>& segs_i
      = mgr_->getReverseCellToSegs(ndi->getId());

  // For the window size.  This should be parameterized.
  const int rly = 5;
  const int rlx = 5;

  const int tries = 5;
  for (int t = 1; t <= tries; t++) {
    // Position of the source.
    // yi = ndi->getBottom()+0.5*ndi->getHeight();
    // xi = ndi->getLeft()+0.5*ndi->getWidth();

    // Segment for the source.
    const int si = segs_i[0]->getSegId();

    // Choices: (i) random position within a box centered at the original
    // position; (ii) random position within a box between the current
    // and original position; (iii) the original position itself.  Should
    // this also be a randomized choice??????????????????????????????????
    double xj, yj;
    constexpr bool option_1 = true;
    constexpr bool option_2 = false;
    if (option_1) {
      // Centered at the original position within a box.
      const double orig_yc = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
      const double orig_xc = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;

      const int grid_xi = std::min(
          xdim - 1, std::max(0, (int) ((orig_xc - arch_->getMinX().v) / xwid)));
      const int grid_yi = std::min(
          ydim - 1, std::max(0, (int) ((orig_yc - arch_->getMinY().v) / ywid)));

      const int rel_x = mgr_->getRandom(2 * rlx + 1);
      const int rel_y = mgr_->getRandom(2 * rly + 1);

      const int grid_xj
          = std::min(xdim - 1, std::max(0, (grid_xi - rlx + rel_x)));
      const int grid_yj
          = std::min(ydim - 1, std::max(0, (grid_yi - rly + rel_y)));

      xj = arch_->getMinX().v + grid_xj * xwid;
      yj = arch_->getMinY().v + grid_yj * ywid;
    } else if (option_2) {
      // The original position.
      xj = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;
      yj = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
    } else {
      // Somewhere between current position and original position.
      double orig_yc = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
      double orig_xc = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;

      double curr_yc = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
      double curr_xc = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

      int grid_xi = std::min(
          xdim - 1, std::max(0, (int) ((curr_xc - arch_->getMinX().v) / xwid)));
      int grid_yi = std::min(
          ydim - 1, std::max(0, (int) ((curr_yc - arch_->getMinY().v) / ywid)));

      int grid_xj = std::min(
          xdim - 1, std::max(0, (int) ((orig_xc - arch_->getMinX().v) / xwid)));
      int grid_yj = std::min(
          ydim - 1, std::max(0, (int) ((orig_yc - arch_->getMinY().v) / ywid)));

      if (grid_xi > grid_xj) {
        std::swap(grid_xi, grid_xj);
      }
      if (grid_yi > grid_yj) {
        std::swap(grid_yi, grid_yj);
      }

      const int w = grid_xj - grid_xi;
      const int h = grid_yj - grid_yi;

      const int rel_x = mgr_->getRandom(w + 1);
      const int rel_y = mgr_->getRandom(h + 1);

      grid_xj = std::min(xdim - 1, std::max(0, (grid_xi + rel_x)));
      grid_yj = std::min(ydim - 1, std::max(0, (grid_yi + rel_y)));

      xj = arch_->getMinX().v + grid_xj * xwid;
      yj = arch_->getMinY().v + grid_yj * ywid;
    }

    // Row and segment for the destination.
    int rj = (int) ((yj - arch_->getMinY().v) / mgr_->getSingleRowHeight().v);
    rj = std::min(mgr_->getNumSingleHeightRows() - 1, std::max(0, rj));
    yj = arch_->getRow(rj)->getBottom().v;
    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }

    // Need to determine validity of things.
    if (sj == -1 || ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
      // The target segment cannot support the candidate cell.
      continue;
    }

    if (mgr_->tryMove(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++moves_;
      return true;
    }
    if (mgr_->trySwap(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++swaps_;
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DisplacementGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      337,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
TouchedPairGenerator::TouchedPairGenerator()
    : DetailedGenerator("touched_pair")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedPairGenerator::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  anchors_ = mgr_->getExactPolishPairNodes(192);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool TouchedPairGenerator::generate(DetailedMgr* mgr,
                                    std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  if (attempts_ == 1 || (attempts_ % 16) == 0) {
    anchors_ = mgr_->getExactPolishPairNodes(192);
  }
  if (anchors_.empty()) {
    anchors_ = candidates.empty() ? mgr_->getExactPolishPairNodes(192)
                                  : candidates;
  }
  if (anchors_.empty()) {
    return false;
  }

  const std::array<int, 6> neighbor_offsets{{-1, 1, -2, 2, -3, 3}};
  const int anchor_trials = std::min(6, static_cast<int>(anchors_.size()));
  for (int trial = 0; trial < anchor_trials; trial++) {
    Node* ndi = anchors_[mgr_->getRandom(anchors_.size())];
    if (ndi == nullptr || ndi->isFixed() || !ndi->isStdCell()
        || !arch_->isSingleHeightCell(ndi)) {
      continue;
    }
    const auto& segs_i = mgr_->getReverseCellToSegs(ndi->getId());
    if (segs_i.size() != 1) {
      continue;
    }
    const int si = segs_i[0]->getSegId();
    const int src_row = segs_i[0]->getRowId();

    mgr_->sortCellsInSeg(si);
    const std::vector<Node*>& same_seg_nodes = mgr_->getCellsInSeg(si);
    auto anchor_it = std::find(same_seg_nodes.begin(), same_seg_nodes.end(), ndi);
    if (anchor_it != same_seg_nodes.end()) {
      const int anchor_idx
          = static_cast<int>(std::distance(same_seg_nodes.begin(), anchor_it));
      for (const int offset : neighbor_offsets) {
        const int neighbor_idx = anchor_idx + offset;
        if (neighbor_idx < 0 || neighbor_idx >= same_seg_nodes.size()) {
          continue;
        }
        Node* ndj = same_seg_nodes[neighbor_idx];
        if (ndj == nullptr || ndj == ndi || ndj->isFixed()
            || !ndj->isStdCell() || !arch_->isSingleHeightCell(ndj)) {
          continue;
        }
        if (mgr_->trySwap(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          ndj->getLeft(),
                          ndj->getBottom(),
                          si)) {
          ++swaps_;
          return true;
        }
        if (mgr_->tryMove(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          ndj->getLeft(),
                          ndj->getBottom(),
                          si)) {
          ++moves_;
          return true;
        }
      }
    }

    for (const int row_delta : {-1, 1, -2, 2}) {
      const int row_id = src_row + row_delta;
      if (row_id < 0 || row_id >= mgr_->getNumSingleHeightRows()) {
        continue;
      }
      Node* best_partner = nullptr;
      int best_seg = -1;
      int best_dist = std::numeric_limits<int>::max();
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        const int sj = seg_ptr->getSegId();
        mgr_->sortCellsInSeg(sj);
        const std::vector<Node*>& row_nodes = mgr_->getCellsInSeg(sj);
        for (Node* ndj : row_nodes) {
          if (ndj == nullptr || ndj->isFixed() || !ndj->isStdCell()
              || !arch_->isSingleHeightCell(ndj)) {
            continue;
          }
          const int dist = std::abs((ndj->getCenterX() - ndi->getCenterX()).v);
          if (dist < best_dist) {
            best_dist = dist;
            best_partner = ndj;
            best_seg = sj;
          }
        }
      }
      if (best_partner != nullptr
          && mgr_->trySwap(ndi,
                           ndi->getLeft(),
                           ndi->getBottom(),
                           si,
                           best_partner->getLeft(),
                           best_partner->getBottom(),
                           best_seg)) {
        ++swaps_;
        return true;
      }
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedPairGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      344,
      "Generator {:s}, anchors {:d}, cumulative attempts {:d}, swaps {:d}, "
      "moves {:5d} since last reset.",
      getName().c_str(),
      static_cast<int>(anchors_.size()),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
TouchedEndpointGenerator::TouchedEndpointGenerator()
    : DetailedGenerator("touched_endpoint")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedEndpointGenerator::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  frontier_ = mgr_->getExactPolishEndpointNodes(256);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool TouchedEndpointGenerator::generate(DetailedMgr* mgr,
                                        std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  if (attempts_ == 1 || (attempts_ % 16) == 0) {
    frontier_ = mgr_->getExactPolishEndpointNodes(256);
  }
  if (frontier_.empty()) {
    frontier_ = candidates.empty() ? mgr_->getExactPolishEndpointNodes(256)
                                   : candidates;
  }
  if (frontier_.empty()) {
    return false;
  }

  const int site_width = arch_->getRow(0)->getSiteWidth().v;
  const int row_height = mgr_->getSingleRowHeight().v;
  const int trials = std::min(8, static_cast<int>(frontier_.size()));
  for (int trial = 0; trial < trials; trial++) {
    Node* ndi = frontier_[mgr_->getRandom(frontier_.size())];
    if (ndi == nullptr || ndi->isFixed() || !ndi->isStdCell()
        || !arch_->isSingleHeightCell(ndi)) {
      continue;
    }
    const auto& segs_i = mgr_->getReverseCellToSegs(ndi->getId());
    if (segs_i.size() != 1) {
      continue;
    }
    const int si = segs_i[0]->getSegId();
    const int src_row = segs_i[0]->getRowId();
    const int curr_center_x = ndi->getCenterX().v;
    const int curr_center_y = ndi->getCenterY().v;

    double target_center_x = curr_center_x;
    double target_center_y = curr_center_y;
    int x_votes = 0;
    int y_votes = 0;
    double fallback_span = -1.0;
    double fallback_x = curr_center_x;
    double fallback_y = curr_center_y;
    for (const Pin* pin : ndi->getPins()) {
      Edge* edge = pin->getEdge();
      if (edge == nullptr) {
        continue;
      }
      const int npins = edge->getNumPins();
      if (npins <= 1 || npins > 16) {
        continue;
      }

      int xmin = std::numeric_limits<int>::max();
      int xmax = std::numeric_limits<int>::min();
      int ymin = std::numeric_limits<int>::max();
      int ymax = std::numeric_limits<int>::min();
      for (const Pin* edge_pin : edge->getPins()) {
        const Node* ndj = edge_pin->getNode();
        if (ndj == nullptr || ndj == ndi) {
          continue;
        }
        const int pin_x = (ndj->getCenterX() + edge_pin->getOffsetX()).v;
        const int pin_y = (ndj->getCenterY() + edge_pin->getOffsetY()).v;
        xmin = std::min(xmin, pin_x);
        xmax = std::max(xmax, pin_x);
        ymin = std::min(ymin, pin_y);
        ymax = std::max(ymax, pin_y);
      }
      if (xmin > xmax || ymin > ymax) {
        continue;
      }

      const int curr_pin_x = (ndi->getCenterX() + pin->getOffsetX()).v;
      const int curr_pin_y = (ndi->getCenterY() + pin->getOffsetY()).v;
      const int desired_pin_x = std::clamp(curr_pin_x, xmin, xmax);
      const int desired_pin_y = std::clamp(curr_pin_y, ymin, ymax);
      if (desired_pin_x != curr_pin_x) {
        target_center_x += desired_pin_x - pin->getOffsetX().v;
        x_votes++;
      }
      if (desired_pin_y != curr_pin_y) {
        target_center_y += desired_pin_y - pin->getOffsetY().v;
        y_votes++;
      }

      const double span = static_cast<double>((xmax - xmin) + (ymax - ymin));
      if (span > fallback_span) {
        fallback_span = span;
        fallback_x = (0.5 * static_cast<double>(xmin + xmax))
                     - static_cast<double>(pin->getOffsetX().v);
        fallback_y = (0.5 * static_cast<double>(ymin + ymax))
                     - static_cast<double>(pin->getOffsetY().v);
      }
    }

    if (x_votes > 0) {
      target_center_x /= (x_votes + 1);
    } else {
      target_center_x = fallback_x;
    }
    if (y_votes > 0) {
      target_center_y /= (y_votes + 1);
    } else {
      target_center_y = fallback_y;
    }

    const int target_left = static_cast<int>(
        std::llround(target_center_x - (0.5 * ndi->getWidth().v)));
    const int target_row = std::clamp(
        static_cast<int>((target_center_y - arch_->getMinY().v) / row_height),
        0,
        mgr_->getNumSingleHeightRows() - 1);

    std::vector<int> row_candidates;
    auto add_row = [&](const int row_id) {
      if (row_id < 0 || row_id >= mgr_->getNumSingleHeightRows()) {
        return;
      }
      if (std::find(row_candidates.begin(), row_candidates.end(), row_id)
          == row_candidates.end()) {
        row_candidates.push_back(row_id);
      }
    };
    add_row(src_row);
    add_row(target_row);
    add_row(target_row - 1);
    add_row(target_row + 1);
    add_row(src_row - 1);
    add_row(src_row + 1);

    for (const int row_id : row_candidates) {
      int best_seg = -1;
      int best_left = 0;
      int best_dist = std::numeric_limits<int>::max();
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        const int seg_min = seg_ptr->getMinX().v;
        const int seg_max = seg_ptr->getMaxX().v - ndi->getWidth().v;
        if (seg_max < seg_min) {
          continue;
        }
        const int clamped_left = std::max(seg_min, std::min(target_left, seg_max));
        const int dist = std::abs(clamped_left - target_left);
        if (dist < best_dist) {
          best_dist = dist;
          best_left = clamped_left;
          best_seg = seg_ptr->getSegId();
        }
      }
      if (best_seg < 0) {
        continue;
      }

      for (const int site_delta : {0, -1, 1, -2, 2}) {
        DbuX candidate_left{best_left + (site_delta * site_width)};
        if (!mgr_->alignPos(
                ndi,
                candidate_left,
                mgr_->getSegment(best_seg)->getMinX(),
                mgr_->getSegment(best_seg)->getMaxX())) {
          continue;
        }
        DbuY candidate_bottom = arch_->getRow(row_id)->getBottom();
        if (candidate_left == ndi->getLeft()
            && candidate_bottom == ndi->getBottom()) {
          continue;
        }
        if (mgr_->tryMove(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          candidate_left,
                          candidate_bottom,
                          best_seg)) {
          ++moves_;
          return true;
        }
      }
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedEndpointGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      345,
      "Generator {:s}, frontier {:d}, cumulative attempts {:d}, swaps {:d}, "
      "moves {:5d} since last reset.",
      getName().c_str(),
      static_cast<int>(frontier_.size()),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
ChangedNetTransactionGenerator::ChangedNetTransactionGenerator()
    : DetailedGenerator("changed_net_txn")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ChangedNetTransactionGenerator::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  payload_ = mgr_->getExactPolishPayloadNodes(128);
  payloadWindows_ = mgr_->getExactPolishPayloadWindows(48);
  changedNets_ = mgr_->getExactPolishEdges(128);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool ChangedNetTransactionGenerator::generate(DetailedMgr* mgr,
                                              std::vector<Node*>& candidates)
{
  ++attempts_;
  lastProposalKind_ = ProposalKind::None;

  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  if (attempts_ == 1 || (attempts_ % 16) == 0) {
    payload_ = mgr_->getExactPolishPayloadNodes(128);
    payloadWindows_ = mgr_->getExactPolishPayloadWindows(48);
  }
  if (attempts_ == 1 || (attempts_ % 32) == 0) {
    changedNets_ = mgr_->getExactPolishEdges(128);
  }

  const int site_width = arch_->getRow(0)->getSiteWidth().v;
  std::unordered_set<int> payload_ids;
  payload_ids.reserve(payload_.size() * 2 + 1);
  for (Node* node : payload_) {
    if (node != nullptr) {
      payload_ids.insert(node->getId());
    }
  }
  std::unordered_set<int> focus_ids;
  focus_ids.reserve(candidates.size() * 2 + 1);
  for (Node* node : candidates) {
    if (node != nullptr) {
      focus_ids.insert(node->getId());
    }
  }

  const int payload_window_top_k
      = std::min(12, static_cast<int>(payloadWindows_.size()));
  const int payload_window_trials = std::min(3, payload_window_top_k);
  const int payload_window_start
      = payload_window_top_k > payload_window_trials
            ? mgr_->getRandom(payload_window_top_k)
            : 0;
  for (int window_trial = 0; window_trial < payload_window_trials; window_trial++) {
    const int window_idx
        = (payload_window_start + window_trial) % payload_window_top_k;
    const auto& window = payloadWindows_[window_idx];
    payload_window_candidates_++;
    const auto window_nodes = collectPayloadWindowNodeCandidates(
        mgr_, arch_, window, payload_ids, focus_ids);
    if (window_nodes.size() < 2) {
      continue;
    }
    payload_node_pools_++;

    const auto slot_pool = collectPayloadWindowAssignmentSlots(
        mgr_, arch_, window, window_nodes, site_width);
    if (slot_pool.size() < 2) {
      continue;
    }

    auto best_plan = findBestAssignmentPlan(mgr_,
                                            window_nodes,
                                            slot_pool,
                                            site_width,
                                            3,
                                            2,
                                            payload_assignment_proposals_,
                                            payload_proxy_positive_assignments_);
    if (best_plan.moves.empty()) {
      continue;
    }

    if (mgr_->tryMoveAssignment(best_plan.moves)) {
      lastProposalKind_ = ProposalKind::PayloadWindow;
      payload_generated_++;
      return true;
    }
    mgr_->rejectMove();
  }

  if (changedNets_.empty()) {
    changedNets_ = collectFallbackChangedNets(mgr_, candidates, 96);
  }
  if (changedNets_.empty()) {
    return false;
  }

  const int edge_trials = std::min(6, static_cast<int>(changedNets_.size()));
  for (int edge_trial = 0; edge_trial < edge_trials; edge_trial++) {
    Edge* edge = changedNets_[mgr_->getRandom(changedNets_.size())];
    if (edge == nullptr || edge->getNumPins() <= 1 || edge->getNumPins() > 16) {
      continue;
    }

    const auto net_nodes = collectChangedNetNodeCandidates(mgr_, arch_, edge);
    if (net_nodes.size() < 2) {
      continue;
    }

    const auto slot_pool = collectChangedNetAssignmentSlots(
        mgr_, arch_, net_nodes, site_width);
    fallback_slot_pool_candidates_ += static_cast<int>(slot_pool.size());
    if (slot_pool.size() < 2) {
      continue;
    }
    auto best_plan = findBestAssignmentPlan(mgr_,
                                            net_nodes,
                                            slot_pool,
                                            site_width,
                                            4,
                                            2,
                                            fallback_assignment_proposals_,
                                            fallback_proxy_positive_assignments_);

    if (!best_plan.moves.empty() && mgr_->tryMoveAssignment(best_plan.moves)) {
      const int moved_on_edge = countMovedNodesOnEdge(mgr_->getJournal(), edge);
      if (moved_on_edge >= 2) {
        lastProposalKind_ = ProposalKind::ChangedNetFallback;
        fallback_generated_++;
        return true;
      }
      mgr_->rejectMove();
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ChangedNetTransactionGenerator::noteAccepted(const double acceptedGain,
                                                  const Journal& journal)
{
  switch (lastProposalKind_) {
    case ProposalKind::PayloadWindow:
      payload_accepted_++;
      payload_accepted_delta_ += acceptedGain;
      payload_changed_cells_ += static_cast<int>(journal.getAffectedNodes().size());
      payload_changed_nets_ += static_cast<int>(journal.getAffectedEdges().size());
      break;
    case ProposalKind::ChangedNetFallback:
      fallback_accepted_++;
      fallback_accepted_delta_ += acceptedGain;
      break;
    case ProposalKind::None:
      break;
  }
  lastProposalKind_ = ProposalKind::None;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ChangedNetTransactionGenerator::noteRejected()
{
  switch (lastProposalKind_) {
    case ProposalKind::PayloadWindow:
      payload_rejected_++;
      break;
    case ProposalKind::ChangedNetFallback:
      fallback_rejected_++;
      break;
    case ProposalKind::None:
      break;
  }
  lastProposalKind_ = ProposalKind::None;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void ChangedNetTransactionGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      347,
      "Generator {:s}, cumulative attempts {:d}, payload_windows {:d}, "
      "payload_node_pools {:d}, payload_assignment_proposals {:d}, "
      "payload_proxy_positive {:d}, payload_generated {:d}, "
      "payload_accepted {:d}, payload_rejected {:d}, "
      "payload_accepted_delta {:.1f}, payload_changed_cells {:d}, "
      "payload_changed_nets {:d}.",
      getName().c_str(),
      attempts_,
      payload_window_candidates_,
      payload_node_pools_,
      payload_assignment_proposals_,
      payload_proxy_positive_assignments_,
      payload_generated_,
      payload_accepted_,
      payload_rejected_,
      payload_accepted_delta_,
      payload_changed_cells_,
      payload_changed_nets_);
  mgr_->getLogger()->info(
      DPL,
      348,
      "Generator {:s}, changed_nets {:d}, fallback_slot_pool {:d}, "
      "fallback_assignment_proposals {:d}, fallback_proxy_positive {:d}, "
      "fallback_generated {:d}, fallback_accepted {:d}, "
      "fallback_rejected {:d}, fallback_accepted_delta {:.1f}.",
      getName().c_str(),
      static_cast<int>(changedNets_.size()),
      fallback_slot_pool_candidates_,
      fallback_assignment_proposals_,
      fallback_proxy_positive_assignments_,
      fallback_generated_,
      fallback_accepted_,
      fallback_rejected_,
      fallback_accepted_delta_);
}

}  // namespace dpl_evolve
