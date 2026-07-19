// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <vector>

#include "detailed_generator.h"
#include "detailed_manager.h"

namespace dpl_evolve {

class Architecture;
class DetailedObjective;
class Network;

class DetailedRandom
{
 public:
  enum DrcMode
  {
    DrcMode_NoPenalty = 0,
    DrcMode_NormalPenalty,
    DrcMode_Eliminate,
    DrcMode_Unknown
  };
  enum MoveMode
  {
    MoveMode_Median = 0,
    MoveMode_CellDensity1,
    MoveMode_RandomWindow,
    MoveMode_Unknown
  };
  enum MoveSource
  {
    MoveSource_All = 0,
    MoveSource_Wirelength,
    MoveSource_DrcViolators,
    MoveSource_Unknown
  };
  enum FocusMode
  {
    FocusMode_All = 0,
    FocusMode_ExactPolish,
    FocusMode_Unknown
  };

 public:
  DetailedRandom(Architecture* arch, Network* network);

  void run(DetailedMgr* mgrPtr, const std::string& command);
  void run(DetailedMgr* mgrPtr, std::vector<std::string>& args);

 private:
  double go();

  double eval(const std::vector<double>& costs,
              const std::vector<std::string>& expr) const;
  double doOperation(double a, double b, char op) const;
  bool isOperator(char ch) const;
  bool isObjective(char ch) const;
  bool isNumber(char ch) const;

  void collectCandidates();

  // Standard stuff.
  DetailedMgr* mgrPtr_ = nullptr;

  Architecture* arch_;
  Network* network_;

  // Candidate cells.
  std::vector<Node*> candidates_;

  // For generating move lists.
  std::vector<DetailedGenerator*> generators_;

  // For evaluating objectives.
  std::vector<DetailedObjective*> objectives_;

  // Parameters controlling the moves.
  double movesPerCandidate_ = 3;
  FocusMode focusMode_ = FocusMode_All;
  int focusCandidateLimit_ = 192;

  // For costing.
  std::vector<double> initCost_;
  std::vector<double> currCost_;
  std::vector<double> nextCost_;
  std::vector<double> deltaCost_;

  // For obj evaluation.
  std::vector<std::string> expr_;
};

class RandomGenerator : public DetailedGenerator
{
 public:
  RandomGenerator();

 public:
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr*) override {}

 private:
  DetailedMgr* mgr_ = nullptr;
  Architecture* arch_ = nullptr;
  Network* network_ = nullptr;

  int attempts_ = 0;
  int moves_ = 0;
  int swaps_ = 0;
};

class DisplacementGenerator : public DetailedGenerator
{
 public:
  DisplacementGenerator();

 public:
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr*) override {}

 private:
  DetailedMgr* mgr_ = nullptr;
  Architecture* arch_ = nullptr;
  Network* network_ = nullptr;

  int attempts_ = 0;
  int moves_ = 0;
  int swaps_ = 0;
};

class TouchedPairGenerator : public DetailedGenerator
{
 public:
  TouchedPairGenerator();

 public:
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr* mgr) override;

 private:
  DetailedMgr* mgr_ = nullptr;
  Architecture* arch_ = nullptr;
  Network* network_ = nullptr;
  std::vector<Node*> anchors_;

  int attempts_ = 0;
  int moves_ = 0;
  int swaps_ = 0;
};

class TouchedEndpointGenerator : public DetailedGenerator
{
 public:
  TouchedEndpointGenerator();

 public:
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr* mgr) override;

 private:
  DetailedMgr* mgr_ = nullptr;
  Architecture* arch_ = nullptr;
  Network* network_ = nullptr;
  std::vector<Node*> frontier_;

  int attempts_ = 0;
  int moves_ = 0;
  int swaps_ = 0;
};

class ChangedNetTransactionGenerator : public DetailedGenerator
{
 public:
  ChangedNetTransactionGenerator();

 public:
  bool generate(DetailedMgr* mgr, std::vector<Node*>& candidates) override;
  void stats() override;
  void init(DetailedMgr* mgr) override;
  void noteAccepted(double acceptedGain, const Journal& journal) override;
  void noteRejected() override;

 private:
  enum class ProposalKind
  {
    None = 0,
    PayloadWindow,
    ChangedNetFallback
  };

  DetailedMgr* mgr_ = nullptr;
  Architecture* arch_ = nullptr;
  Network* network_ = nullptr;
  std::vector<Edge*> changedNets_;
  std::vector<Node*> payload_;
  std::vector<DetailedMgr::ExactPolishPayloadWindow> payloadWindows_;
  ProposalKind lastProposalKind_ = ProposalKind::None;

  int attempts_ = 0;
  int payload_window_candidates_ = 0;
  int payload_node_pools_ = 0;
  int payload_assignment_proposals_ = 0;
  int payload_proxy_positive_assignments_ = 0;
  int payload_generated_ = 0;
  int payload_accepted_ = 0;
  int payload_rejected_ = 0;
  double payload_accepted_delta_ = 0.0;
  int payload_changed_cells_ = 0;
  int payload_changed_nets_ = 0;
  int fallback_slot_pool_candidates_ = 0;
  int fallback_assignment_proposals_ = 0;
  int fallback_proxy_positive_assignments_ = 0;
  int fallback_generated_ = 0;
  int fallback_accepted_ = 0;
  int fallback_rejected_ = 0;
  double fallback_accepted_delta_ = 0.0;
};

}  // namespace dpl_evolve
