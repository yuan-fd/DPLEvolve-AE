// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_hpwl.h"

#include <cstdint>

#include "infrastructure/Objects.h"
#include "objective/detailed_objective.h"
#include "optimization/detailed_orient.h"
#include "util/journal.h"

namespace dpl_evolve {

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
  cache_edge_stamp_.assign(network_->getNumEdges(), 0);
  cache_edge_hpwl_.assign(network_->getNumEdges(), 0);
  cache_stamp_ = 1;
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
void DetailedHPWL::ensureCacheScratch()
{
  const int edge_count = network_->getNumEdges();
  if (static_cast<int>(cache_edge_stamp_.size()) != edge_count) {
    cache_edge_stamp_.assign(edge_count, 0);
    cache_edge_hpwl_.assign(edge_count, 0);
    cache_stamp_ = 1;
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedHPWL::advanceCacheStamp()
{
  if (++cache_stamp_ != 0) {
    return;
  }
  std::fill(cache_edge_stamp_.begin(), cache_edge_stamp_.end(), 0);
  cache_stamp_ = 1;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedHPWL::buildDeltaCache(const Journal& journal, DeltaCache& cache)
{
  ensureCacheScratch();
  advanceCacheStamp();

  const auto& affected_edges = journal.getAffectedEdges();
  uint64_t old_wl = 0.;
  uint64_t new_wl = 0.;
  for (const auto& edge : affected_edges) {
    const int edge_id = edge->getId();
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
      continue;
    }
    cache_edge_stamp_[edge_id] = cache_stamp_;
    const uint64_t curr_hpwl = edge->hpwl();
    cache_edge_hpwl_[edge_id] = curr_hpwl;
    old_wl += edge_hpwl_[edge_id];
    new_wl += curr_hpwl;
  }

  cache.delta = static_cast<double>(old_wl) - new_wl;
  return cache.delta;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedHPWL::deltaFromCache(const DeltaCache& cache,
                                    const Journal& journal,
                                    int* overlap_edges)
{
  ensureCacheScratch();

  double delta = cache.delta;
  int overlaps = 0;
  for (const auto& edge : journal.getAffectedEdges()) {
    const int edge_id = edge->getId();
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins >= skipNetsLargerThanThis_) {
      continue;
    }
    const double final_hpwl = static_cast<double>(edge->hpwl());
    if (cache_edge_stamp_[edge_id] == cache_stamp_) {
      delta += static_cast<double>(cache_edge_hpwl_[edge_id]) - final_hpwl;
      overlaps++;
    } else {
      delta += static_cast<double>(edge_hpwl_[edge_id]) - final_hpwl;
    }
  }

  if (overlap_edges != nullptr) {
    *overlap_edges = overlaps;
  }
  return delta;
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
  advanceCacheStamp();
}
////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
}  // namespace dpl_evolve
