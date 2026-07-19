// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <array>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"

namespace dpl_evolve {

class Architecture;
class DetailedMgr;
struct DetailedResidualSpec;
class Edge;
class Network;
class Node;

class DetailedReorderer
{
 public:
  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);

 private:
  static constexpr int kMaxPackedMemoWindowCells = 8;

  struct ResidualWindowCandidate
  {
    int start = 0;
    int stop = 0;
    int anchorIndex = 0;
    int producerScore = 0;
    int producerSpan = 0;
    int assignmentOrderCap = 0;
    int assignmentProbeCap = 0;
  };

  struct PackedWindowCostMemoKey
  {
    int segId = -1;
    int size = 0;
    std::array<int, kMaxPackedMemoWindowCells> nodeIds{};
    std::array<int, kMaxPackedMemoWindowCells> lefts{};

    bool operator==(const PackedWindowCostMemoKey& other) const;
  };

  struct PackedWindowCostMemoKeyHash
  {
    size_t operator()(const PackedWindowCostMemoKey& key) const;
  };

  void reorder();
  void reorderAllSegments();
  bool reorderResidualIntervals();
  bool reorderResidualInterval(const DetailedResidualSpec& residual,
                               std::unordered_set<uint64_t>& seen_windows);
  std::vector<ResidualWindowCandidate> buildResidualWindowCandidates(
      const std::vector<Node*>& nodes,
      int segId,
      int anchorIndex,
      DbuX targetLeft,
      int maxWindow,
      int extraRadius,
      std::unordered_set<uint64_t>& seen_windows);
  void reorderSegmentWindows(const std::vector<Node*>& nodes,
                             int segId,
                             int rowId,
                             int anchorIndex,
                             DbuX targetLeft,
                             bool residualGuided,
                             int producerScore,
                             int producerSpan,
                             std::unordered_set<uint64_t>& seen_windows,
                             int maxWindow,
                             int extraRadius);
  void reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId,
               DbuX targetLeft,
               int anchorIndex,
               bool residualGuided,
               int assignmentOrderCapOverride = 0,
               int assignmentProbeCapOverride = 0);
  double memoizedCost(const std::vector<Node*>& nodes,
                      int jstrt,
                      int jstop,
                      int segId,
                      bool packedWindow,
                      const std::vector<const Edge*>& edges);
  void invalidatePackedCostMemo();
  std::vector<const Edge*> collectAffectedEdges(const std::vector<Node*>& nodes,
                                                int istrt,
                                                int istop);
  double cost(const std::vector<const Edge*>& edges) const;
  bool isSingleHeightMovable(const Node* node) const;
  int findAnchorIndex(const std::vector<Node*>& nodes,
                      const Node* anchorNode,
                      DbuX targetLeft) const;

  Architecture* arch_;
  Network* network_;
  DetailedMgr* mgrPtr_ = nullptr;

  int skipNetsLargerThanThis_ = 100;
  std::vector<int> edgeMask_;
  int traversal_ = 0;
  int windowSize_ = 3;

  int residual_frontier_size_ = 0;
  int residual_frontier_active_ = 0;
  int residual_prioritized_cells_ = 0;
  int residual_targeted_segments_ = 0;
  int residual_intervals_built_ = 0;
  int residual_window_candidates_ = 0;
  int residual_exact_probes_ = 0;
  int residual_permutation_candidates_ = 0;
  int residual_chain_candidates_ = 0;
  int residual_accepted_moves_ = 0;
  int residual_packed_windows_ = 0;
  int residual_packed_order_candidates_ = 0;
  int residual_packed_probes_ = 0;
  int residual_packed_accepts_ = 0;
  int residual_packed_memo_hits_ = 0;
  int residual_packed_memo_misses_ = 0;
  int residual_packed_memo_invalidations_ = 0;
  int residual_assignment_windows_ = 0;
  int residual_assignment_order_candidates_ = 0;
  int residual_assignment_probes_ = 0;
  int residual_assignment_accepts_ = 0;
  int residual_assignment_fallbacks_ = 0;
  int residual_assignment_high_score_windows_ = 0;
  int residual_assignment_high_score_accepts_ = 0;
  double residual_hpwl_gain_ = 0.0;
  double residual_packed_hpwl_gain_ = 0.0;
  double residual_assignment_hpwl_gain_ = 0.0;
  double residual_assignment_high_score_hpwl_gain_ = 0.0;
  bool residual_fallback_sweep_ = false;

  std::unordered_map<PackedWindowCostMemoKey,
                     double,
                     PackedWindowCostMemoKeyHash>
      packed_cost_memo_;
  int packed_cost_generation_ = 0;
};

}  // namespace dpl_evolve
