// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "detailed_generator.h"
#include "infrastructure/Coordinates.h"
#include "util/journal.h"

namespace odb {
class Rect;
}
namespace dpl_evolve {
class Edge;
class Architecture;
class DetailedMgr;
class DetailedHPWL;
class Network;
class Node;
class Grid;
struct GlobalSwapParams;

class DetailedGlobalSwap : public DetailedGenerator
{
 public:
  DetailedGlobalSwap(Architecture* arch, Network* network);
  DetailedGlobalSwap();

  // Interfaces for scripting.
  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, std::vector<std::string>& args);

  // Interface for move generation.
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr* mgr) override;

 private:
  struct ProbeCandidate
  {
    DbuX x;
    DbuY y;
    int row_id;
    int seg_id;
  };

  struct ProbeResult
  {
    Journal journal;
    double hpwl_delta = 0.0;
    double congestion_improvement = 0.0;

    ProbeResult(Grid* grid, DetailedMgr* mgr) : journal(grid, mgr) {}
  };

  void globalSwap();  // tries to avoid overlap.
  bool calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox);
  bool getRange(Node*, odb::Rect&);
  bool generate(Node* ndi);
  bool generateWirelengthOptimalMove(Node* ndi);
  bool generateRandomMove(Node* ndi);
  double calculateAdaptiveCongestionWeight();
  std::vector<ProbeCandidate> buildProbeCandidates(Node* ndi,
                                                   const odb::Rect& bbox,
                                                   int current_row,
                                                   int current_seg) const;
  std::optional<ProbeResult> exactScoreSourceTopK(
      Node* ndi,
      const std::vector<ProbeCandidate>& probes,
      DetailedHPWL& hpwl_obj);
  bool replayExactResult(Node* ndi,
                         const ProbeResult& probe,
                         DetailedHPWL& hpwl_obj,
                         double& hpwl_delta,
                         double& congestion_improvement);
  double calculateCongestionImprovement(const Journal& journal) const;
  int sourceTopKBudget() const;

  // Standard stuff.
  DetailedMgr* mgr_;
  Architecture* arch_;
  Network* network_;

  // Other.
  int skipNetsLargerThanThis_;
  std::vector<int> edgeMask_;
  int traversal_;

  std::vector<double> xpts_;
  std::vector<double> ypts_;

  // For use as a move generator.
  int attempts_;
  int moves_;
  int swaps_;

  // Two-pass optimization state
  double budget_hpwl_ = 0.0;
  bool is_profiling_pass_ = false;
  Journal* profiling_journal_ = nullptr;
  double tradeoff_ = 0.2;
  double congestion_weight_ = 0.0;
  std::vector<double> congestion_contribution_;
  const GlobalSwapParams* swap_params_ = nullptr;

  // Extra DPL tapering (0 = legacy-like, 1 = full extra).
  double extra_dpl_intensity_ = 1.0;
  double extra_dpl_alpha_ = 1.0;
  bool allow_random_moves_ = true;
};

}  // namespace dpl_evolve
