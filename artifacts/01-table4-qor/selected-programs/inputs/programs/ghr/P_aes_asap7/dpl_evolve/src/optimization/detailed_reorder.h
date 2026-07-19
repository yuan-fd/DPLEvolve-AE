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
  void runAssignmentBoundaryPolish(DetailedMgr* mgrPtr,
                                   const std::string& command);
  void runAssignmentBoundaryPolish(DetailedMgr* mgrPtr,
                                   const std::vector<std::string>& args);

 private:
  struct BoundaryProbe
  {
    int seg_id = -1;
    DbuX left{0};
    DbuY bottom{0};
    bool use_swap = false;
    bool endpoint = false;
    bool cross_row = false;
  };

  void assignmentBoundaryPolish(const std::vector<std::string>& args);
  std::vector<int> collectAssignmentBoundarySeedIndices(
      const std::vector<Node*>& nodes,
      int segId,
      DbuX regionLeft,
      DbuX regionRight,
      int maxSeeds) const;
  void addBoundaryProbe(std::vector<BoundaryProbe>& probes,
                        int segId,
                        DbuX targetLeft,
                        DbuY targetBottom,
                        bool useSwap,
                        bool endpoint,
                        bool crossRow);
  bool scoreAssignmentBoundaryProbe(Node* node,
                                    int sourceSegId,
                                    const BoundaryProbe& probe,
                                    class DetailedHPWL& hpwlObj,
                                    double& currHpwl,
                                    double minImprovement);
  void reorder();
  void reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId);
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
  bool focusedMode_ = false;
  bool assignmentSelectedMode_ = false;
  int focusedSegmentsVisited_ = 0;
  int focusedWindowsTried_ = 0;
  int focusedWindowsAccepted_ = 0;
  int assignmentSegmentsVisited_ = 0;
  int assignmentWindowsTried_ = 0;
  int assignmentWindowsAccepted_ = 0;
  int stickyWindowsSkipped_ = 0;
  int stickyNodesGuarded_ = 0;
  int assignmentBoundarySegmentsVisited_ = 0;
  int assignmentBoundarySeedNodes_ = 0;
  int assignmentBoundaryProbes_ = 0;
  int assignmentBoundaryExactScored_ = 0;
  int assignmentBoundaryAccepts_ = 0;
  int assignmentBoundaryMoves_ = 0;
  int assignmentBoundarySwaps_ = 0;
  int assignmentBoundaryEndpointProbes_ = 0;
  int assignmentBoundaryPairProbes_ = 0;
  int assignmentBoundaryCrossRowProbes_ = 0;
  int assignmentBoundaryDuplicateProbes_ = 0;
  double assignmentBoundaryAcceptedDelta_ = 0.0;
};

}  // namespace dpl_evolve
