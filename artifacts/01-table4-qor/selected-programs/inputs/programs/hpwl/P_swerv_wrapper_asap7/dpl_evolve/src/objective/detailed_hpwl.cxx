// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_hpwl.h"

#include <cstdint>
#include <limits>

#include "infrastructure/Objects.h"
#include "objective/detailed_objective.h"
#include "optimization/detailed_orient.h"
#include "util/journal.h"

namespace dpl_evolve {
namespace {
const DetailedHPWL::EdgePositionOverride* findOverride(
    const std::vector<DetailedHPWL::EdgePositionOverride>& overrides,
    const Node* node)
{
  for (const auto& override : overrides) {
    if (override.node == node) {
      return &override;
    }
  }
  return nullptr;
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedHPWL::DetailedHPWL(Network* network)
    : DetailedObjective("hpwl"), network_(network)
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedHPWL::init()
{
  edge_hpwl_.clear();
  edge_hpwl_.resize(network_->getNumEdges(), 0);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedHPWL::init(DetailedMgr* mgrPtr, DetailedOrient* orientPtr)
{
  orientPtr_ = orientPtr;
  mgrPtr_ = mgrPtr;
  init();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedHPWL::curr()
{
  uint64_t hpwl = 0.;
  for (int i = 0; i < network_->getNumEdges(); i++) {
    const Edge* edi = network_->getEdge(i);
    const int npins = edi->getNumPins();
    if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
      continue;
    }
    auto edi_hpwl = edi->hpwl();
    edge_hpwl_[edi->getId()] = edi_hpwl;
    hpwl += edi_hpwl;
  }
  return hpwl;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedHPWL::delta(const Journal& journal)
{
  affected_edges_.clear();

  uint64_t old_wl = 0.;
  uint64_t new_wl = 0.;
  auto affected_edges = journal.getAffectedEdges();
  for (const auto& edge : affected_edges) {
    int npins = edge->getNumPins();
    if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
      continue;
    }
    affected_edges_.emplace_back(edge->getId());
    old_wl += edge_hpwl_[edge->getId()];
    new_wl += edge->hpwl();
  }
  // +ve means improvement.
  return (double) old_wl - new_wl;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
uint64_t DetailedHPWL::edgeHpwlWithOverrides(
    const Edge* edge,
    const std::vector<EdgePositionOverride>& overrides)
{
  if (edge == nullptr || edge->getNumPins() <= 1) {
    return 0;
  }

  int xmin = std::numeric_limits<int>::max();
  int xmax = std::numeric_limits<int>::min();
  int ymin = std::numeric_limits<int>::max();
  int ymax = std::numeric_limits<int>::min();
  int valid_pins = 0;
  for (const Pin* pin : edge->getPins()) {
    const Node* node = pin->getNode();
    if (node == nullptr) {
      continue;
    }

    int pin_x = (node->getCenterX() + pin->getOffsetX()).v;
    int pin_y = (node->getCenterY() + pin->getOffsetY()).v;
    if (const auto* override = findOverride(overrides, node); override != nullptr) {
      pin_x += override->left.v - node->getLeft().v;
      pin_y += override->bottom.v - node->getBottom().v;
    }

    xmin = std::min(xmin, pin_x);
    xmax = std::max(xmax, pin_x);
    ymin = std::min(ymin, pin_y);
    ymax = std::max(ymax, pin_y);
    valid_pins++;
  }

  if (valid_pins <= 1 || xmin > xmax || ymin > ymax) {
    return 0;
  }
  return static_cast<uint64_t>((xmax - xmin) + (ymax - ymin));
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedHPWL::accept()
{
  // Accept the changes.
  for (const auto& edge_id : affected_edges_) {
    const Edge* edge = network_->getEdge(edge_id);
    edge_hpwl_[edge->getId()] = edge->hpwl();
  }
  affected_edges_.clear();
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
}  // namespace dpl_evolve
