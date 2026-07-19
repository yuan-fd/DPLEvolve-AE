// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "detailed_generator.h"

namespace odb {
class Rect;
}
namespace dpl_evolve {
class Edge;
class Architecture;
class DetailedMgr;
class Network;
class Journal;
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
  struct ExactPassStats
  {
    int ranked_source_edge_cells = 0;
    int selected_source_edge_cells = 0;
    int focused_node_seed_cells = 0;
    int hot_segment_seed_cells = 0;
    int accepted_hot_segment_seed_cells = 0;
    int cells_considered = 0;
    int proposals = 0;
    int same_segment_assignment_candidates = 0;
    int exact_scored = 0;
    int same_segment_assignment_exact_scored = 0;
    int accepts = 0;
    int same_segment_assignment_accepts = 0;
    int accepted_moves = 0;
    int accepted_swaps = 0;
    int focus_segments_added = 0;
    int replay_failures = 0;
    int sticky_nodes = 0;
    int fragile_nodes = 0;
    int fragility_rejects = 0;
    int supported_row_changes = 0;
    int unsupported_row_changes = 0;
    int sticky_dominant_accepts = 0;
    int balanced_accepts = 0;
    int fragile_dominant_accepts = 0;
    int fragile_override_accepts = 0;
    int extreme_fragile_rejects = 0;
    int quota_fragile_rejects = 0;
    int fragile_override_quota = 0;
    int protected_segments = 0;
    long runtime_ms = 0;
    double accepted_delta = 0.0;
    double same_segment_assignment_accepted_delta = 0.0;
    double quality_adjustment = 0.0;
    double hpwl_change = 0.0;
  };

  ExactPassStats globalSwap(int cell_budget, int top_k);  // tries to avoid overlap.
  void focusedSourceEdgeSwap(int passes, double tol);
  bool calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox);
  bool getRange(Node*,
                odb::Rect&,
                double* source_edge_criticality = nullptr,
                int* critical_edge_count = nullptr);
  bool generate(Node* ndi);
  bool generateWirelengthOptimalMove(Node* ndi);
  bool generateRandomMove(Node* ndi);
  double calculateAdaptiveCongestionWeight();

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
