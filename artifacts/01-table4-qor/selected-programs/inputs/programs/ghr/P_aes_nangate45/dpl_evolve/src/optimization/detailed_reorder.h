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
class DetailedHPWL;
class Network;
class DetailedReorderer
{
 public:
  DetailedReorderer(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, const std::vector<std::string>& args);

 private:
  void reorder();
  void noteAcceptedWindowReplaySeed(const std::vector<Node*>& nodes,
                                    int segId,
                                    DbuX leftLimit,
                                    DbuX rightLimit,
                                    double acceptedDelta,
                                    int endpointCells,
                                    int sourceEdgeCells,
                                    int sourceEdgeSegmentWeight,
                                    bool fromMicroPack);
  std::vector<int> expandHotFrontier(const std::vector<int>& seedSegments,
                                     int maxSegments);
  void runHotPolishPasses();
  void runHotMicroPackPasses(const std::vector<int>& seedSegments);
  void runAcceptedWindowResidualReplay();
  bool reorderSegment(int segId, int windowSize, bool collectHotMetrics);
  bool microPackSegment(int segId,
                        int windowSize,
                        int maxWindows,
                        int maxShiftCandidates);
  bool reorder(const std::vector<Node*>& nodes,
               int jstrt,
               int jstop,
               DbuX leftLimit,
               DbuX rightLimit,
               int segId,
               int rowId,
               bool* replayFailed = nullptr,
               double* acceptedDelta = nullptr);
  double cost(const std::vector<Node*>& nodes, int istrt, int istop);

  struct AcceptedWindowReplaySeed
  {
    std::vector<Node*> nodes;
    int segId = -1;
    DbuX leftLimit{0};
    DbuX rightLimit{0};
    double priority = 0.0;
    double acceptedDelta = 0.0;
    int endpointCells = 0;
    int sourceEdgeCells = 0;
    int sourceEdgeSegmentWeight = 0;
    bool fromMicroPack = false;
  };

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
  int prioritizedSegmentsLastPass_ = 0;
  int visitedHotSegmentsLastPass_ = 0;
  int hotWindowSizeLastPass_ = 4;
  int hotSeedSegmentsLastPass_ = 0;
  int hotExpandedSegmentsLastPass_ = 0;
  int hotReplayFrontierLastPass_ = 0;
  int hotPolishPassesLastRun_ = 0;
  int hotExactWindowsScoredLastRun_ = 0;
  int hotExactEdgesScoredLastRun_ = 0;
  int hotWindowsAttemptedLastRun_ = 0;
  int hotWindowsAcceptedLastRun_ = 0;
  int hotWindowsRejectedLastRun_ = 0;
  int hotReplayFailuresLastRun_ = 0;
  int64_t hotAcceptedDeltaLastRun_ = 0;
  int hotMicroPackFrontierLastPass_ = 0;
  int hotMicroPackPassesLastRun_ = 0;
  int hotMicroPackWindowsLastRun_ = 0;
  int hotMicroPackEndpointWindowsLastRun_ = 0;
  int hotMicroPackBoundarySourceEdgeWindowsLastRun_ = 0;
  int hotMicroPackSourceEdgeWindowsLastRun_ = 0;
  int hotMicroPackProbesLastRun_ = 0;
  int hotMicroPackAcceptsLastRun_ = 0;
  int hotMicroPackReplayFailuresLastRun_ = 0;
  int hotMicroPackEdgesScoredLastRun_ = 0;
  int64_t hotMicroPackAcceptedDeltaLastRun_ = 0;
  std::vector<AcceptedWindowReplaySeed> acceptedWindowReplaySeeds_;
  int residualReplaySeedCountLastRun_ = 0;
  int residualReplayHotSeedCountLastRun_ = 0;
  int residualReplayMicroSeedCountLastRun_ = 0;
  int residualReplayWindowsLastRun_ = 0;
  int residualReplayProbesLastRun_ = 0;
  int residualReplayAcceptsLastRun_ = 0;
  int residualReplayPass1AcceptsLastRun_ = 0;
  int residualReplayCascadeSeedCountLastRun_ = 0;
  int residualReplayExpandedCascadeSeedsLastRun_ = 0;
  int residualReplayCascadeProbesLastRun_ = 0;
  int residualReplayCascadeAcceptsLastRun_ = 0;
  int residualReplayRollbackRejectsLastRun_ = 0;
  int residualReplayLowConversionRejectsLastRun_ = 0;
  int residualReplayFailuresLastRun_ = 0;
  int residualReplayEdgesScoredLastRun_ = 0;
  int64_t residualReplayAcceptedDeltaLastRun_ = 0;
  DetailedHPWL* activeHpwlObj_ = nullptr;
};

}  // namespace dpl_evolve
