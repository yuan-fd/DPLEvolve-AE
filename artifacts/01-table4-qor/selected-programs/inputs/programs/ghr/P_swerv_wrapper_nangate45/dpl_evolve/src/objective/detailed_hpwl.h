// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

// Description:
// - An objective function to help with computation of change in wirelength
//   if doing some sort of moves (e.g., single, swap, sets, etc.).

#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

#include "detailed_objective.h"
#include "infrastructure/network.h"

namespace dpl_evolve {

class DetailedOrient;
class DetailedMgr;

class DetailedHPWL : public DetailedObjective
{
  // For WL objective.
 public:
  explicit DetailedHPWL(Network* network);

  void init();
  double curr() override;
  double delta(const Journal& journal) override;
  void accept() override;
  void reject() override;
  // Other.
  void init(DetailedMgr* mgrPtr, DetailedOrient* orientPtr);
  bool isScorableEdge(const Edge* edge) const;
  uint64_t edgeHpwlCached(const Edge* edge) const;
  uint64_t deltaCacheHits() const { return delta_cache_hits_; }
  uint64_t deltaCacheMisses() const { return delta_cache_misses_; }
  bool lastDeltaUsedCache() const { return last_delta_cache_hit_; }
  int lastDeltaAffectedEdges() const { return last_delta_affected_edges_; }
  void clearDeltaCache();

 private:
  struct DeltaCacheEntry
  {
    std::vector<int> edge_ids;
    uint64_t old_wl = 0;
  };

  Network* network_;

  DetailedMgr* mgrPtr_ = nullptr;
  DetailedOrient* orientPtr_ = nullptr;

  // Other.
  int skipNetsLargerThanThis_ = 100;
  std::vector<uint64_t> edge_hpwl_;
  std::vector<int> affected_edges_;
  std::unordered_map<uint64_t, DeltaCacheEntry> delta_cache_;
  std::deque<uint64_t> delta_cache_fifo_;
  uint64_t delta_cache_hits_ = 0;
  uint64_t delta_cache_misses_ = 0;
  bool last_delta_cache_hit_ = false;
  int last_delta_affected_edges_ = 0;
  static constexpr size_t kDeltaCacheLimit = 1024;
};

}  // namespace dpl_evolve
