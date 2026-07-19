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
  struct SelectedSegmentTxnStats
  {
    int segments = 0;
    int anchor_cells = 0;
    int pair_candidates = 0;
    int cycle_candidates = 0;
    int scored_transactions = 0;
    int accepted_transactions = 0;
    double accepted_hpwl_gain = 0.0;
  };

  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);
  void runSelectedSegments(DetailedMgr* mgrPtr,
                           const std::vector<int>& segment_ids,
                           int window_size,
                           int window_cap,
                           int accept_stop,
                           int& scored_windows,
                           int& accepted_windows,
                           double& accepted_hpwl_gain);
  void runSelectedSegmentTransactions(
      DetailedMgr* mgrPtr,
      const std::vector<int>& segment_ids,
      const std::vector<int>& anchor_cell_ids,
      int segment_cap,
      int transaction_cap,
      int accept_stop,
      SelectedSegmentTxnStats& stats);

 private:
  struct TxnMove
  {
    Node* node = nullptr;
    DbuX orig_left{0};
    DbuX new_left{0};
  };

  void reorder();
  void reorder(const std::vector<int>& segment_ids,
               int window_cap,
               int accept_stop,
               int& scored_windows,
               int& accepted_windows,
               double& accepted_hpwl_gain);
  void reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);
  double exactWindowHpwl(const std::vector<Node*>& nodes, int istrt, int istop);
  int findNodeSegment(const Node* node) const;
  bool applyTransactionMoves(const std::vector<TxnMove>& moves,
                             int seg_id,
                             int row_id);
  bool evaluateTransaction(const std::vector<TxnMove>& moves,
                           int seg_id,
                           int row_id,
                           SelectedSegmentTxnStats& stats);
  void buildPairTransactionCandidates(const std::vector<Node*>& nodes,
                                      const std::vector<int>& anchor_indices,
                                      int pair_cap,
                                      std::vector<std::vector<TxnMove>>& moves,
                                      int& pair_candidates);
  void buildCycleTransactionCandidates(
      const std::vector<Node*>& nodes,
      const std::vector<int>& anchor_indices,
      int cycle_cap,
      std::vector<std::vector<TxnMove>>& moves,
      int& cycle_candidates);

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
};

}  // namespace dpl_evolve
