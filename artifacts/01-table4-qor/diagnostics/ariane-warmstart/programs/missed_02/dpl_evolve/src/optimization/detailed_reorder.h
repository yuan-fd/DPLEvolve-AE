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
  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);
  void runExactLocalClosure(DetailedMgr* mgrPtr);

 private:
  void reorder();
  void reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);
  bool buildWindowFromAcceptedNodes(int segId,
                                    std::vector<Node*>& window_nodes,
                                    int& jstrt,
                                    int& jstop,
                                    DbuX& leftLimit,
                                    DbuX& rightLimit,
                                    int& rowId);

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
  int selected_reorder_frontier_ = 0;
  int selected_reorder_windows_ = 0;
  int selected_reorder_accepts_ = 0;
  double selected_reorder_gain_ = 0.0;
  int exact_closure_windows_ = 0;
  int exact_closure_accepts_ = 0;
  double exact_closure_gain_ = 0.0;
};

}  // namespace dpl_evolve
