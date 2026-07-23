// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
namespace odb {
class Rect;
}
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
  void reorder();
  void reorderSegments(const std::vector<int>& segment_ids,
                       int window_size,
                       int& windows,
                       int& accepts,
                       double& gain);
  double reorder(const std::vector<Node*>& nodes,
                 int jstrt,
                 int jstop,
                 DbuX leftLimit,
                 DbuX rightLimit,
                 int segId,
                 int rowId);
  bool computeTargetBox(Node* node, odb::Rect& bbox) const;
  bool attemptBestMove(Node* node,
                       const std::vector<int>& row_ids,
                       const std::vector<DbuX>& anchors,
                       DetailedHPWL& hpwl_obj,
                       int& probes,
                       int& accepts,
                       double& gain);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);

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
