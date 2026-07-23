// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

#include "detailed_global.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <limits>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

#include "boost/token_functions.hpp"
#include "boost/tokenizer.hpp"
#include "detailed_manager.h"
#include "dpl_evolve/Opendp.h"
#include "infrastructure/Objects.h"
#include "infrastructure/architecture.h"
#include "infrastructure/detailed_segment.h"
#include "objective/detailed_hpwl.h"
#include "util/utility.h"
#include "utl/Logger.h"

namespace dpl_evolve {

using utl::DPL;

namespace {
struct CandidateMove
{
  int seg_id = -1;
  DbuX left{0};
  DbuY bottom{0};
  bool swap_only = false;
  int outside_gain = 0;
  int center_distance = 0;
  int move_distance = 0;
};

struct ReplayChoice
{
  CandidateMove candidate;
  double delta = 0.0;
  bool use_swap = false;
  bool valid = false;
};

template <typename T>
void sortUnique(std::vector<T>& values)
{
  std::sort(values.begin(), values.end());
  values.erase(std::unique(values.begin(), values.end()), values.end());
}
}  // namespace

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap(Architecture* arch, Network* network)
    : DetailedGenerator("global swap"),
      mgr_(nullptr),
      arch_(arch),
      network_(network),
      skipNetsLargerThanThis_(100),
      traversal_(0),
      attempts_(0),
      moves_(0),
      swaps_(0)
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedGlobalSwap::DetailedGlobalSwap() : DetailedGlobalSwap(nullptr, nullptr)
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr, const std::string& command)
{
  boost::char_separator<char> separators(" \r\t\n;");
  boost::tokenizer<boost::char_separator<char>> tokens(command, separators);
  std::vector<std::string> args;
  for (const auto& token : tokens) {
    args.push_back(token);
  }
  run(mgrPtr, args);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::run(DetailedMgr* mgrPtr, std::vector<std::string>& args)
{
  mgr_ = mgrPtr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  swap_params_ = &mgr_->getGlobalSwapParams();

  int passes = swap_params_->passes;
  double tol = swap_params_->tolerance;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    }
  }
  passes = std::max(passes, 1);
  tol = std::max(tol, 0.001);

  uint64_t hpwl_x = 0;
  uint64_t hpwl_y = 0;
  int64_t curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
  const int64_t init_hpwl = curr_hpwl;
  if (init_hpwl == 0) {
    return;
  }

  mgr_->clearAcceptedSourceState();
  for (int p = 1; p <= passes; p++) {
    const int64_t last_hpwl = curr_hpwl;
    globalSwap();
    curr_hpwl = Utility::hpwl(network_, hpwl_x, hpwl_y);
    mgr_->getLogger()->info(
        DPL, 316, "sourceTopK pass {:d}; hpwl is {:.6e}.", p, (double) curr_hpwl);
    if (last_hpwl == 0
        || std::abs(curr_hpwl - last_hpwl) / (double) last_hpwl <= tol) {
      break;
    }
  }
  const double curr_imp
      = (((init_hpwl - curr_hpwl) / static_cast<double>(init_hpwl)) * 100.0);
  mgr_->getLogger()->info(DPL,
                          910,
                          "sourceTopK global swap complete: final HPWL={:.6e}, "
                          "improvement={:.2f}%",
                          static_cast<double>(curr_hpwl),
                          curr_imp);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::globalSwap()
{
  traversal_ = 0;
  edgeMask_.resize(network_->getNumEdges());
  std::ranges::fill(edgeMask_, 0);
  mgr_->resortSegments();
  mgr_->clearAcceptedSourceState();

  std::vector<Node*> candidates = mgr_->getSingleHeightCells();
  mgr_->shuffle(candidates);

  DetailedHPWL hpwlObj(network_);
  hpwlObj.init(mgr_, nullptr);
  double curr_hpwl = hpwlObj.curr();
  (void) curr_hpwl;

  const int pass_exact_probe_cap
      = std::max(6000, static_cast<int>(candidates.size() / 20));
  const int admitted_source_cap
      = std::max(2000, static_cast<int>(candidates.size() / 12));
  const int per_source_topk = 3;
  const int prerank_cap = 8;
  const auto start = std::chrono::steady_clock::now();
  const auto elapsed_limit = std::chrono::milliseconds(90000);

  int sources_seen = 0;
  int sources_admitted = 0;
  int cheap_profiled = 0;
  int generated = 0;
  int exact_scored = 0;
  int replay_attempts = 0;
  int replay_failures = 0;
  int rollbacks = 0;
  int accepts = 0;
  double accepted_delta = 0.0;

  auto elapsed = [&]() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - start);
  };

  for (Node* ndi : candidates) {
    if (ndi == nullptr) {
      continue;
    }
    sources_seen++;
    cheap_profiled++;
    if (sources_admitted >= admitted_source_cap
        || exact_scored >= pass_exact_probe_cap
        || elapsed() >= elapsed_limit) {
      break;
    }

    odb::Rect bbox;
    if (!getRange(ndi, bbox)) {
      continue;
    }

    const int curr_left = ndi->getLeft().v;
    const int curr_bottom = ndi->getBottom().v;
    const int curr_center_x = ndi->getCenterX().v;
    const int curr_center_y = ndi->getCenterY().v;
    if (curr_center_x >= bbox.xMin() && curr_center_x <= bbox.xMax()
        && curr_center_y >= bbox.yMin() && curr_center_y <= bbox.yMax()) {
      continue;
    }

    int dispX = 0;
    int dispY = 0;
    mgr_->getMaxDisplacement(dispX, dispY);
    const int outside_x = curr_center_x < bbox.xMin()
                              ? bbox.xMin() - curr_center_x
                              : (curr_center_x > bbox.xMax()
                                     ? curr_center_x - bbox.xMax()
                                     : 0);
    const int outside_y = curr_center_y < bbox.yMin()
                              ? bbox.yMin() - curr_center_y
                              : (curr_center_y > bbox.yMax()
                                     ? curr_center_y - bbox.yMax()
                                     : 0);
    const int displacement_headroom
        = std::max(0, dispX - std::abs(ndi->getLeft().v - ndi->getOrigLeft().v));
    const int cheap_score
        = outside_x + outside_y + (displacement_headroom / 4) + ndi->getNumPins();
    if (cheap_score <= 0) {
      continue;
    }
    sources_admitted++;

    if (mgr_->getNumReverseCellToSegs(ndi->getId()) != 1) {
      continue;
    }
    const int source_seg = mgr_->getReverseCellToSegs(ndi->getId())[0]->getSegId();

    odb::Rect lbox(ndi->getLeft().v - dispX,
                   ndi->getBottom().v - dispY,
                   ndi->getLeft().v + dispX,
                   ndi->getBottom().v + dispY);
    bbox.set_xlo(std::max(bbox.xMin(), lbox.xMin()));
    bbox.set_xhi(std::min(bbox.xMax(), lbox.xMax()));
    bbox.set_ylo(std::max(bbox.yMin(), lbox.yMin()));
    bbox.set_yhi(std::min(bbox.yMax(), lbox.yMax()));

    const int target_row = arch_->find_closest_row(DbuY{bbox.yMin()});
    std::vector<int> candidate_rows;
    for (int delta_row = -2; delta_row <= 2; delta_row++) {
      const int row = target_row + delta_row;
      if (row >= 0 && row < mgr_->getNumSingleHeightRows()) {
        candidate_rows.push_back(row);
      }
    }
    sortUnique(candidate_rows);

    std::vector<CandidateMove> candidate_moves;
    std::vector<int> anchors = {
        bbox.xMin(),
        (bbox.xMin() + bbox.xMax()) / 2,
        bbox.xMax()};
    sortUnique(anchors);

    for (const int row_id : candidate_rows) {
      const DbuY yj = arch_->getRow(row_id)->getBottom();
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        for (const int anchor : anchors) {
          DbuX xj{anchor - (ndi->getWidth().v / 2)};
          if (!mgr_->alignPos(ndi, xj, seg_ptr->getMinX(), seg_ptr->getMaxX())) {
            continue;
          }
          if (xj == ndi->getLeft() && yj == ndi->getBottom()) {
            continue;
          }
          if (std::abs(xj.v - ndi->getOrigLeft().v) > dispX
              || std::abs(yj.v - ndi->getOrigBottom().v) > dispY) {
            continue;
          }
          const int new_center_x = xj.v + (ndi->getWidth().v / 2);
          const int new_center_y = yj.v + (ndi->getHeight().v / 2);
          const int new_outside_x = new_center_x < bbox.xMin()
                                        ? bbox.xMin() - new_center_x
                                        : (new_center_x > bbox.xMax()
                                               ? new_center_x - bbox.xMax()
                                               : 0);
          const int new_outside_y = new_center_y < bbox.yMin()
                                        ? bbox.yMin() - new_center_y
                                        : (new_center_y > bbox.yMax()
                                               ? new_center_y - bbox.yMax()
                                               : 0);
          CandidateMove move;
          move.seg_id = seg_ptr->getSegId();
          move.left = xj;
          move.bottom = yj;
          move.outside_gain
              = (outside_x + outside_y) - (new_outside_x + new_outside_y);
          move.center_distance
              = std::abs(new_center_x - ((bbox.xMin() + bbox.xMax()) / 2));
          move.move_distance = std::abs(xj.v - curr_left) + std::abs(yj.v - curr_bottom);
          candidate_moves.push_back(move);
        }
      }
    }

    std::sort(candidate_moves.begin(),
              candidate_moves.end(),
              [](const CandidateMove& lhs, const CandidateMove& rhs) {
                return std::tie(rhs.outside_gain,
                                lhs.center_distance,
                                lhs.move_distance,
                                lhs.seg_id,
                                lhs.left.v,
                                lhs.bottom.v)
                       < std::tie(lhs.outside_gain,
                                   rhs.center_distance,
                                   rhs.move_distance,
                                   rhs.seg_id,
                                   rhs.left.v,
                                   rhs.bottom.v);
              });

    std::vector<CandidateMove> deduped;
    deduped.reserve(candidate_moves.size());
    for (const CandidateMove& move : candidate_moves) {
      bool duplicate = false;
      for (const CandidateMove& kept : deduped) {
        if (kept.seg_id == move.seg_id && kept.left == move.left
            && kept.bottom == move.bottom) {
          duplicate = true;
          break;
        }
      }
      if (!duplicate) {
        deduped.push_back(move);
      }
      if (static_cast<int>(deduped.size()) >= prerank_cap) {
        break;
      }
    }

    ReplayChoice best;
    const int probes = std::min(per_source_topk, static_cast<int>(deduped.size()));
    for (int i = 0; i < probes && exact_scored < pass_exact_probe_cap; i++) {
      const CandidateMove& move = deduped[i];
      generated++;
      bool legal = mgr_->tryMove(ndi,
                                 ndi->getLeft(),
                                 ndi->getBottom(),
                                 source_seg,
                                 move.left,
                                 move.bottom,
                                 move.seg_id);
      bool used_swap = false;
      if (!legal) {
        legal = mgr_->trySwap(ndi,
                              ndi->getLeft(),
                              ndi->getBottom(),
                              source_seg,
                              move.left,
                              move.bottom,
                              move.seg_id);
        used_swap = legal;
      }
      if (!legal) {
        continue;
      }
      exact_scored++;
      const double delta = hpwlObj.delta(mgr_->getJournal());
      mgr_->rejectMove();
      rollbacks++;
      if (delta > best.delta) {
        best.candidate = move;
        best.delta = delta;
        best.use_swap = used_swap;
        best.valid = delta > 0.0;
      }
    }

    if (!best.valid) {
      continue;
    }

    replay_attempts++;
    bool replay_ok = best.use_swap
                         ? mgr_->trySwap(ndi,
                                         ndi->getLeft(),
                                         ndi->getBottom(),
                                         source_seg,
                                         best.candidate.left,
                                         best.candidate.bottom,
                                         best.candidate.seg_id)
                         : mgr_->tryMove(ndi,
                                         ndi->getLeft(),
                                         ndi->getBottom(),
                                         source_seg,
                                         best.candidate.left,
                                         best.candidate.bottom,
                                         best.candidate.seg_id);
    if (!replay_ok) {
      replay_failures++;
      continue;
    }
    const double replay_delta = hpwlObj.delta(mgr_->getJournal());
    if (replay_delta <= 0.0) {
      mgr_->rejectMove();
      rollbacks++;
      replay_failures++;
      continue;
    }

    mgr_->setAcceptedSourceDelta(replay_delta);
    mgr_->recordAcceptedSourceJournal();
    hpwlObj.accept();
    mgr_->acceptMove();
    accepts++;
    accepted_delta += replay_delta;
  }

  const double elapsed_ms = static_cast<double>(elapsed().count());
  const double gain_per_runtime
      = elapsed_ms > 0.0 ? accepted_delta / elapsed_ms : accepted_delta;
  mgr_->getLogger()->info(
      DPL,
      912,
      "source_topk_sources_seen={} source_topk_sources_admitted={} "
      "source_topk_cheap_profiled={} source_topk_generated={} "
      "source_topk_exact_scored={} source_topk_replay_attempts={} "
      "source_topk_replay_failures={} source_topk_rollbacks={} "
      "source_topk_accepts={} source_topk_accepted_delta={:.0f} "
      "source_topk_elapsed_ms={:.0f} source_topk_gain_per_runtime={:.3f} "
      "accepted_nodes={} hot_segments={}",
      sources_seen,
      sources_admitted,
      cheap_profiled,
      generated,
      exact_scored,
      replay_attempts,
      replay_failures,
      rollbacks,
      accepts,
      accepted_delta,
      elapsed_ms,
      gain_per_runtime,
      mgr_->getAcceptedNodes().size(),
      mgr_->getHotSegments().size());
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::getRange(Node* nd, odb::Rect& nodeBbox)
{
  Edge* ed;
  unsigned mid;

  Pin* pin;
  unsigned t = 0;

  DbuX xmin = arch_->getMinX();
  DbuX xmax = arch_->getMaxX();
  DbuY ymin = arch_->getMinY();
  DbuY ymax = arch_->getMaxY();

  xpts_.clear();
  ypts_.clear();
  for (int n = 0; n < nd->getNumPins(); n++) {
    pin = nd->getPins()[n];
    ed = pin->getEdge();

    nodeBbox.mergeInit();
    const int numPins = ed->getNumPins();
    if (numPins <= 1 || numPins > skipNetsLargerThanThis_) {
      continue;
    }
    if (!calculateEdgeBB(ed, nd, nodeBbox)) {
      continue;
    }

    nodeBbox.set_xlo(std::min(
        std::max(xmin.v, nodeBbox.xMin() - pin->getOffsetX().v), xmax.v));
    nodeBbox.set_xhi(std::max(
        std::min(xmax.v, nodeBbox.xMax() - pin->getOffsetX().v), xmin.v));
    nodeBbox.set_ylo(std::min(
        std::max(ymin.v, nodeBbox.yMin() - pin->getOffsetY().v), ymax.v));
    nodeBbox.set_yhi(std::max(
        std::min(ymax.v, nodeBbox.yMax() - pin->getOffsetY().v), ymin.v));

    xpts_.push_back(nodeBbox.xMin());
    xpts_.push_back(nodeBbox.xMax());
    ypts_.push_back(nodeBbox.yMin());
    ypts_.push_back(nodeBbox.yMax());
    ++t;
    ++t;
  }

  if (t <= 1) {
    return false;
  }

  mid = t >> 1;
  std::ranges::sort(xpts_);
  std::ranges::sort(ypts_);

  nodeBbox.set_xlo(xpts_[mid - 1]);
  nodeBbox.set_xhi(xpts_[mid]);
  nodeBbox.set_ylo(ypts_[mid - 1]);
  nodeBbox.set_yhi(ypts_[mid]);

  return true;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::calculateEdgeBB(Edge* ed, Node* nd, odb::Rect& bbox)
{
  DbuX curX;
  DbuY curY;

  bbox.mergeInit();
  int count = 0;
  for (Pin* pin : ed->getPins()) {
    auto other = pin->getNode();
    if (other == nd) {
      continue;
    }
    curX = other->getCenterX() + pin->getOffsetX().v;
    curY = other->getCenterY() + pin->getOffsetY().v;

    bbox.set_xlo(std::min(curX.v, bbox.xMin()));
    bbox.set_xhi(std::max(curX.v, bbox.xMax()));
    bbox.set_ylo(std::min(curY.v, bbox.yMin()));
    bbox.set_yhi(std::max(curY.v, bbox.yMax()));
    ++count;
  }
  return count != 0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generateWirelengthOptimalMove(Node* ndi)
{
  (void) ndi;
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generateRandomMove(Node* ndi)
{
  (void) ndi;
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(Node* ndi)
{
  (void) ndi;
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();
  swap_params_ = &mgr->getGlobalSwapParams();
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DetailedGlobalSwap::generate(DetailedMgr* mgr,
                                  std::vector<Node*>& candidates)
{
  ++attempts_;
  if (candidates.empty()) {
    return false;
  }
  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();
  swap_params_ = &mgr->getGlobalSwapParams();
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
double DetailedGlobalSwap::calculateAdaptiveCongestionWeight()
{
  return 0.0;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DetailedGlobalSwap::stats()
{
  mgr_->getLogger()->info(
      DPL,
      334,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

}  // namespace dpl_evolve
