// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
namespace dpl_evolve {
class Node;
class Architecture;
class DetailedMgr;
class Network;
class DetailedReorderer
{
 public:
  struct ExactClosureStats
  {
    int frontier_cells = 0;
    int frontier_segments = 0;
    int base_frontier_nodes = 0;
    int critical_frontier_nodes = 0;
    int merged_frontier_nodes = 0;
    int micro_start_added_nodes = 0;
    int base_frontier_segments = 0;
    int critical_frontier_segments = 0;
    int merged_frontier_segments = 0;
    int micro_start_added_segments = 0;
    int windows_generated = 0;
    int windows_selected = 0;
    int windows_capped = 0;
    int windows_evaluated = 0;
    int windows_on_micro_start_segments = 0;
    int windows_on_base_segments = 0;
    int exact_scored = 0;
    int accepts = 0;
    int rollbacks = 0;
    int accepts_on_micro_start_segments = 0;
    int accepts_on_base_segments = 0;
    double accepted_gain = 0.0;
    double gain_on_micro_start_segments = 0.0;
    double gain_on_base_segments = 0.0;
    bool early_stopped = false;
  };

  struct ChainAssignmentStats
  {
    int frontier_cells = 0;
    int frontier_segments = 0;
    int seeds_selected = 0;
    int chain_windows = 0;
    int probes = 0;
    int exact_scored = 0;
    int accepts = 0;
    int rollbacks = 0;
    int depth2_rejects = 0;
    int depth3_rejects = 0;
    int depth3_probes = 0;
    double accepted_gain = 0.0;
    bool early_stopped = false;
  };

  struct MultiRowTransactionStats
  {
    int frontier_cells = 0;
    int frontier_segments = 0;
    int seeds_selected = 0;
    int transaction_windows = 0;
    int probes = 0;
    int exact_scored = 0;
    int accepts = 0;
    int rollbacks = 0;
    int first_step_rejects = 0;
    int second_step_rejects = 0;
    double accepted_gain = 0.0;
    bool early_stopped = false;
  };

  struct ResidualSwapStats
  {
    int frontier_cells = 0;
    int frontier_segments = 0;
    int seeds_selected = 0;
    int swap_windows = 0;
    int probes = 0;
    int exact_scored = 0;
    int accepts = 0;
    int rollbacks = 0;
    int failed_swaps = 0;
    double accepted_gain = 0.0;
    bool early_stopped = false;
  };

  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);
  void exactLocalClosure(DetailedMgr* mgrPtr,
                         const std::vector<Node*>& frontier_nodes,
                         ExactClosureStats& stats,
                         const Opendp::FrontierAttributionStats* frontier_attribution
                         = nullptr);
  void criticalNetChainAssignment(DetailedMgr* mgrPtr,
                                  const std::vector<Node*>& frontier_nodes,
                                  ChainAssignmentStats& stats);
  void multiRowResidualTransactions(DetailedMgr* mgrPtr,
                                    const std::vector<Node*>& frontier_nodes,
                                    MultiRowTransactionStats& stats);
  void segmentLocalResidualSwaps(DetailedMgr* mgrPtr,
                                 const std::vector<Node*>& frontier_nodes,
                                 ResidualSwapStats& stats);

 private:
  struct WindowApplyResult
  {
    int exact_scored = 0;
    bool accepted = false;
    bool rolled_back = false;
    double gain = 0.0;
  };

  void reorder();
  WindowApplyResult reorder(const std::vector<Node*>& nodes,
                            int jstrt,
                            int jstop,
                            DbuX leftLimit,
                            DbuX rightLimit,
                            int segId,
                            int rowId);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);
  double exactCost(const std::vector<const Edge*>& edges) const;

  // Standard stuff.
  Architecture* arch_;
  Network* network_;

  // For segments.
  DetailedMgr* mgrPtr_ = nullptr;

  // Other.
  int skipNetsLargerThanThis_ = 100;
  std::vector<int> edgeMask_;
  std::vector<int> segmentOrder_;
  std::vector<char> focusedSegments_;
  int traversal_ = 0;
  int windowSize_ = 3;
  bool exactWindowMode_ = false;
  int focusedSegmentCount_ = 0;
  int focusedWindowCount_ = 0;
  int focusedAcceptCount_ = 0;
};

}  // namespace dpl_evolve
