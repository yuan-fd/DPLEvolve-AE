// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <unordered_set>
#include <vector>

#include "detailed_generator.h"
#include "infrastructure/Coordinates.h"

namespace odb {
class Rect;
}
namespace dpl_evolve {
class Edge;
class Architecture;
class DetailedMgr;
class Network;
class Journal;
class DetailedHPWL;
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
  struct SourceCandidate
  {
    DbuX x{0};
    DbuY y{0};
    int seg_id{-1};
    double outside_improvement{0.0};
    double center_distance{0.0};
    double move_distance{0.0};
    bool swap_only{false};
  };

  struct ReplayChoice
  {
    bool valid{false};
    bool swap{false};
    DbuX x{0};
    DbuY y{0};
    int seg_id{-1};
    double hpwl_delta{0.0};
  };

  void globalSwap();  // tries to avoid overlap.
  bool calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox);
  bool getRange(Node*, odb::Rect&);
  bool generate(Node* ndi);
  bool generateWirelengthOptimalMove(Node* ndi);
  bool generateRandomMove(Node* ndi);
  double calculateAdaptiveCongestionWeight();
  bool tryReplayCandidate(Node* ndi,
                          int source_seg,
                          const SourceCandidate& candidate,
                          ReplayChoice& choice,
                          DetailedHPWL& hpwl_obj,
                          bool commit);
  void collectAcceptedFootprint(std::vector<Node*>& nodes,
                                std::vector<int>& hot_segments) const;
  double cheapSourceScore(Node* ndi, const odb::Rect& bbox) const;
  void buildCandidates(Node* ndi,
                       int source_seg,
                       const odb::Rect& bbox,
                       std::vector<SourceCandidate>& candidates) const;

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

  // Per-pass sourceTopK counters.
  int64_t source_topk_sources_seen_ = 0;
  int64_t source_topk_sources_admitted_ = 0;
  int64_t source_topk_cheap_profiled_ = 0;
  int64_t source_topk_generated_ = 0;
  int64_t source_topk_exact_scored_ = 0;
  int64_t source_topk_replay_attempts_ = 0;
  int64_t source_topk_replay_failures_ = 0;
  int64_t source_topk_rollbacks_ = 0;
  int64_t source_topk_accepts_ = 0;
  double source_topk_accepted_delta_ = 0.0;
};

}  // namespace dpl_evolve
