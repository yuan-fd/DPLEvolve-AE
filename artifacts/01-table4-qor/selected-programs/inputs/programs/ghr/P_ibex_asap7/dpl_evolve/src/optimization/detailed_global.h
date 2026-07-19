// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "detailed_generator.h"
#include "infrastructure/Coordinates.h"
#include "objective/detailed_hpwl.h"

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
  struct OrderedPairStats
  {
    int stage1_exact = 0;
    int candidates = 0;
    int scored = 0;
    int cached_overlap_edges = 0;
    int audit_gap = 0;
    int promoted = 0;
    int accepted = 0;
    double accepted_delta = 0.0;
  };

  struct EndpointEscapeStats
  {
    int edges = 0;
    int partners = 0;
    int candidates = 0;
    int scored = 0;
    int cached_overlap_edges = 0;
    int audit_gap = 0;
    int promoted = 0;
    int accepted = 0;
    double accepted_delta = 0.0;
  };

  struct MoveProbe
  {
    DbuX left{0};
    DbuY bottom{0};
    int seg_id{-1};
    double heuristic{0.0};
  };

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
  void globalSwap();  // tries to avoid overlap.
  bool calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox);
  bool getRange(Node*, odb::Rect&);
  bool generate(Node* ndi);
  bool generateWirelengthOptimalMove(Node* ndi);
  bool generateRandomMove(Node* ndi);
  double calculateAdaptiveCongestionWeight();
  double calculateCongestionImprovement(const Journal& journal) const;
  double calculateSourceEdgeImprovement(Node* ndi,
                                        const odb::Rect& bbox,
                                        DbuX left,
                                        DbuY bottom) const;
  void appendJournal(const Journal& src, Journal& dst) const;
  void cloneJournal(const Journal& src, Journal& dst) const;
  bool tryOrderedPairTransaction(Node* anchor,
                                 DbuX source_left,
                                 DbuY source_bottom,
                                 int source_seg_id,
                                 const Journal& stage1_journal,
                                 const DetailedHPWL::DeltaCache& stage1_cache,
                                 double stage1_profit,
                                 Journal& best_journal,
                                 double& best_profit,
                                 bool& used_escape);
  bool findSegmentForProbe(Node* ndi,
                           int row_id,
                           DbuX target_left,
                           int& seg_id,
                           DbuX& clamped_left) const;
  void addProbe(Node* ndi,
                const odb::Rect& bbox,
                int row_id,
                DbuX target_left,
                std::vector<MoveProbe>& probes) const;
  bool trySimpleWirelengthOptimalMove(Node* ndi,
                                      const odb::Rect& bbox,
                                      int source_seg_id);

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
  DetailedHPWL* active_hpwl_obj_ = nullptr;

  // Extra DPL tapering (0 = legacy-like, 1 = full extra).
  double extra_dpl_intensity_ = 1.0;
  double extra_dpl_alpha_ = 1.0;
  bool allow_random_moves_ = true;
  bool exact_probe_scoring_enabled_ = true;
  bool last_move_used_exact_probe_ = false;
  bool last_move_used_pair_probe_ = false;
  bool last_move_used_escape_probe_ = false;
  int exact_probe_cells_ = 0;
  int exact_probe_candidates_ = 0;
  int exact_probe_scored_ = 0;
  int exact_probe_generated_ = 0;
  int exact_probe_accepted_ = 0;
  double exact_probe_accepted_delta_ = 0.0;
  int hot_source_edge_cells_ = 0;
  int hot_source_edge_generated_ = 0;
  int hot_source_edge_accepted_ = 0;
  double hot_source_edge_accepted_delta_ = 0.0;
  OrderedPairStats ordered_pair_stats_;
  EndpointEscapeStats endpoint_escape_stats_;
};

}  // namespace dpl_evolve
