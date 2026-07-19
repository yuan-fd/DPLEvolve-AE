// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
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
  struct ResidualWindowCandidate
  {
    int segId = -1;
    int rowId = -1;
    int start = -1;
    int stop = -1;
    int anchorOffset = -1;
    DbuX leftLimit{0};
    DbuX rightLimit{0};
  };

  struct ResidualWindowSnapshot
  {
    int segId = -1;
    std::vector<Node*> nodes;
    std::vector<DbuX> origLeft;
  };

  enum class TelemetryMode
  {
    None,
    Residual,
    Sweep
  };

  void reorder();
  void reorderAllSegments();
  std::vector<int> buildStagedSweepSegments() const;
  bool reorderResidualIntervals();
  bool reorderResidualInterval(const DetailedResidualSpec& residual,
                               std::unordered_set<uint64_t>& seen_windows);
  bool reorderResidualAdjacentChains(const DetailedResidualSpec& residual,
                                     const std::vector<int>& segment_ids);
  void buildResidualWindowCandidates(const std::vector<Node*>& nodes,
                                     int segId,
                                     int rowId,
                                     int anchorIndex,
                                     DbuX targetLeft,
                                     int maxWindow,
                                     int extraRadius,
                                     int maxCandidates,
                                     std::vector<ResidualWindowCandidate>&
                                         candidates);
  void reorderSegmentWindows(const std::vector<Node*>& nodes,
                             int segId,
                             int rowId,
                             int anchorIndex,
                             DbuX targetLeft,
                             bool residualGuided,
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
               bool countResidualTelemetry = true,
               TelemetryMode telemetryMode = TelemetryMode::None);
  ResidualWindowSnapshot captureResidualWindowSnapshot(
      const std::vector<Node*>& nodes,
      int segId,
      int start,
      int stop);
  void restoreResidualWindowSnapshot(const ResidualWindowSnapshot& snapshot);
  bool isResidualWindowChanged(const ResidualWindowSnapshot& snapshot) const;
  std::vector<const Edge*> collectAffectedEdgesForSnapshotSet(
      const std::vector<ResidualWindowSnapshot>& snapshots);
  std::vector<const Edge*> collectAffectedEdgesForSnapshots(
      const ResidualWindowSnapshot& first,
      const ResidualWindowSnapshot* second);
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
  int residual_exact_probes_ = 0;
  int residual_permutation_candidates_ = 0;
  int residual_chain_candidates_ = 0;
  int residual_accepted_moves_ = 0;
  double residual_hpwl_gain_ = 0.0;
  int residual_adjacent_clusters_ = 0;
  int residual_adjacent_exact_probes_ = 0;
  int residual_adjacent_accepts_ = 0;
  int residual_adjacent_rejects_ = 0;
  int residual_adjacent_rollbacks_ = 0;
  double residual_adjacent_hpwl_gain_ = 0.0;
  int staged_sweep_segments_selected_ = 0;
  int staged_sweep_segments_attempted_ = 0;
  int staged_sweep_segments_accepted_ = 0;
  int staged_sweep_segments_rejected_ = 0;
  int staged_sweep_segments_rolled_back_ = 0;
  int staged_sweep_global_rollbacks_ = 0;
  int staged_sweep_windows_built_ = 0;
  int staged_sweep_exact_probes_ = 0;
  int staged_sweep_permutation_candidates_ = 0;
  double staged_sweep_hpwl_gain_ = 0.0;
  bool staged_sweep_early_stop_ = false;
};

}  // namespace dpl_evolve
