// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <cstdint>
#include <string>
#include <unordered_set>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "detailed_generator.h"
#include "infrastructure/Coordinates.h"

namespace odb {
class Rect;
}
namespace dpl_evolve {
class Edge;
class Architecture;
class DetailedMgr;
class DetailedHPWL;
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
  struct ExactCandidate
  {
    enum class Kind
    {
      Move,
      Swap
    };

    Kind kind = Kind::Move;
    DbuX targetX{0};
    DbuY targetY{0};
    int targetSeg = -1;
    double cheapScore = 0.0;
  };

  void globalSwap();  // tries to avoid overlap.
  bool calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox);
  bool getRange(Node*, odb::Rect&);
  bool generate(Node* ndi);
  bool generateWirelengthOptimalMove(Node* ndi, DetailedHPWL& hpwlObj);
  bool generateRandomMove(Node* ndi);
  double calculateAdaptiveCongestionWeight();
  double scoreCandidate(const Node* ndi,
                        const odb::Rect& bbox,
                        int sourceSeg,
                        const ExactCandidate& candidate) const;
  void appendAnchoredCandidates(Node* ndi,
                                const odb::Rect& bbox,
                                int sourceSeg,
                                int rowId,
                                std::vector<ExactCandidate>& candidates,
                                std::unordered_set<uint64_t>& seen) const;
  void appendSwapCandidates(Node* ndi,
                            const odb::Rect& bbox,
                            int sourceSeg,
                            int rowId,
                            std::vector<ExactCandidate>& candidates,
                            std::unordered_set<uint64_t>& seen) const;
  static uint64_t candidateKey(ExactCandidate::Kind kind,
                               int targetSeg,
                               DbuX targetX);
  void captureAcceptedJournal();

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
  int64_t source_cells_considered_ = 0;
  int64_t source_candidates_generated_ = 0;
  int64_t source_exact_scored_ = 0;
  int64_t source_rollbacks_ = 0;
  int64_t source_replay_attempts_ = 0;
  int64_t source_replay_failures_ = 0;
  int64_t source_accepts_ = 0;
  double source_accepted_delta_ = 0.0;
};

}  // namespace dpl_evolve
