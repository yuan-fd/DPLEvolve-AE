// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_hpwl.h"

#include <algorithm>
#include <cstdint>

#include "infrastructure/Objects.h"
#include "objective/detailed_objective.h"
#include "optimization/detailed_orient.h"
#include "util/journal.h"

namespace dpl_evolve {

namespace {

uint64_t hashEdgeIds(const std::vector<int>& edge_ids)
{
  uint64_t hash = 1469598103934665603ULL;
  for (const int edge_id : edge_ids) {
    hash ^= static_cast<uint64_t>(edge_id + 1);
    hash *= 1099511628211ULL;
  }
  hash ^= static_cast<uint64_t>(edge_ids.size() + 1);
  hash *= 1099511628211ULL;
  return hash;
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
  affected_edges_.clear();
  clearDeltaCache();
  last_delta_cache_hit_ = false;
  last_delta_affected_edges_ = 0;
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
    if (!isScorableEdge(edi)) {
      continue;
    }
    auto edi_hpwl = edi->hpwl();
    edge_hpwl_[edi->getId()] = edi_hpwl;
    hpwl += edi_hpwl;
  }
  return hpwl;
}

bool DetailedHPWL::isScorableEdge(const Edge* edge) const
{
  if (edge == nullptr) {
    return false;
  }
  const int npins = edge->getNumPins();
  return npins > 1 && npins < skipNetsLargerThanThis_;
}

uint64_t DetailedHPWL::edgeHpwlCached(const Edge* edge) const
{
  if (edge == nullptr || edge->getId() < 0
      || edge->getId() >= static_cast<int>(edge_hpwl_.size())) {
    return 0;
  }
  return edge_hpwl_[edge->getId()];
}

void DetailedHPWL::clearDeltaCache()
{
  delta_cache_.clear();
  delta_cache_fifo_.clear();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedHPWL::delta(const Journal& journal)
{
  uint64_t old_wl = 0.;
  uint64_t new_wl = 0.;
  affected_edges_.clear();
  last_delta_cache_hit_ = false;
  last_delta_affected_edges_ = 0;

  std::vector<int> edge_ids;
  edge_ids.reserve(journal.getAffectedEdges().size());
  for (const auto& edge : journal.getAffectedEdges()) {
    if (!isScorableEdge(edge)) {
      continue;
    }
    edge_ids.push_back(edge->getId());
  }
  if (edge_ids.empty()) {
    return 0.0;
  }

  std::sort(edge_ids.begin(), edge_ids.end());
  affected_edges_ = edge_ids;
  last_delta_affected_edges_ = static_cast<int>(affected_edges_.size());

  const uint64_t signature = hashEdgeIds(edge_ids);
  auto cache_it = delta_cache_.find(signature);
  if (cache_it != delta_cache_.end() && cache_it->second.edge_ids == edge_ids) {
    old_wl = cache_it->second.old_wl;
    last_delta_cache_hit_ = true;
    ++delta_cache_hits_;
  } else {
    for (const int edge_id : affected_edges_) {
      old_wl += edge_hpwl_[edge_id];
    }
    ++delta_cache_misses_;
    DeltaCacheEntry entry;
    entry.edge_ids = edge_ids;
    entry.old_wl = old_wl;
    if (cache_it == delta_cache_.end()) {
      delta_cache_.emplace(signature, std::move(entry));
      delta_cache_fifo_.push_back(signature);
      while (delta_cache_fifo_.size() > kDeltaCacheLimit) {
        delta_cache_.erase(delta_cache_fifo_.front());
        delta_cache_fifo_.pop_front();
      }
    } else {
      cache_it->second = std::move(entry);
    }
  }

  for (const int edge_id : affected_edges_) {
    new_wl += network_->getEdge(edge_id)->hpwl();
  }
  // +ve means improvement.
  return (double) old_wl - new_wl;
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
  clearDeltaCache();
  last_delta_cache_hit_ = false;
  last_delta_affected_edges_ = 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedHPWL::reject()
{
  affected_edges_.clear();
  last_delta_cache_hit_ = false;
  last_delta_affected_edges_ = 0;
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
}  // namespace dpl_evolve
