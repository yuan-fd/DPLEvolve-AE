// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
namespace dpl_evolve {
class Node;
class Architecture;
class DetailedMgr;
class Network;
class DetailedHPWL;
class DetailedReorderer
{
 public:
  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);

 private:
  struct ConsumerStats
  {
    uint64_t probes = 0;
    uint64_t accepts = 0;
    double gain = 0.0;
  };

  void reorder();
  void reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);
  void resetConsumerStats();
  void reorderSelectedSegments();
  void criticalRowMicroStart();
  void criticalNetChainAssignment();
  void exactLocalClosure();
  void multiRowResidualTransactions();
  void segmentLocalResidualSwaps();
  bool tryFrontierMove(Node* node,
                       DetailedHPWL& hpwl_obj,
                       ConsumerStats& stats,
                       const std::vector<int>& rows,
                       const std::vector<DbuX>& x_targets);
  void logStats(const char* label, const ConsumerStats& stats) const;

  // Standard stuff.
  Architecture* arch_;
  Network* network_;

  // For segments.
  DetailedMgr* mgrPtr_ = nullptr;

  // Other.
  int skipNetsLargerThanThis_ = 100;
  std::vector<int> edgeMask_;
  int traversal_ = 0;
  int windowSize_ = 3;
  bool selected_only_ = false;
  ConsumerStats selected_reorder_stats_;
  ConsumerStats critical_micro_stats_;
  ConsumerStats critical_net_chain_stats_;
  ConsumerStats exact_closure_stats_;
  ConsumerStats multi_row_residual_stats_;
  ConsumerStats segment_residual_swap_stats_;
};

}  // namespace dpl_evolve
