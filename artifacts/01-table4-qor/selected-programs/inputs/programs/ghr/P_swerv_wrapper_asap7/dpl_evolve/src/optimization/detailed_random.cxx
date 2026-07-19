// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

///////////////////////////////////////////////////////////////////////////////
//
// Description:
// Essentially a zero temperature annealer that can use a variety of
// move generators, different objectives and a cost function in order
// to improve a placement.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <limits>
#include <stack>
#include <string>
#include <vector>

#include "boost/tokenizer.hpp"
#include "optimization/detailed_generator.h"
#include "util/utility.h"
#include "utl/Logger.h"
// For detailed improvement.
#include "detailed_manager.h"
#include "detailed_orient.h"
#include "detailed_random.h"
#include "infrastructure/detailed_segment.h"
// Detailed placement objectives.
#include "detailed_global.h"
#include "detailed_vertical.h"
#include "objective/detailed_abu.h"
#include "objective/detailed_displacement.h"
#include "objective/detailed_hpwl.h"
#include "objective/detailed_objective.h"

using utl::DPL;

namespace dpl_evolve {
namespace {

struct FocusRefreshStats
{
  int accepted_moves = 0;
  int anchor_nodes = 0;
  int frontier_edges = 0;
  int frontier_nodes = 0;
};

FocusRefreshStats noteAcceptedExactPolishFrontier(DetailedMgr* mgr,
                                                  const Journal& journal,
                                                  const double acceptedGain)
{
  FocusRefreshStats stats;
  if (mgr == nullptr || acceptedGain <= 0.0) {
    return stats;
  }

  const auto& movedNodes = journal.getAffectedNodes();
  if (!movedNodes.empty()) {
    stats.accepted_moves = 1;
    stats.anchor_nodes = static_cast<int>(movedNodes.size());
    const double anchorShare
        = acceptedGain / static_cast<double>(movedNodes.size());
    for (Node* node : movedNodes) {
      mgr->noteExactPolishAnchor(node, anchorShare);
    }
  }

  const auto& affectedEdges = journal.getAffectedEdges();
  if (affectedEdges.empty()) {
    return stats;
  }

  const double edgeShare
      = acceptedGain / static_cast<double>(affectedEdges.size());
  for (Edge* edge : affectedEdges) {
    if (edge == nullptr) {
      continue;
    }
    const int npins = edge->getNumPins();
    if (npins <= 1 || npins > 16) {
      continue;
    }

    int movablePins = 0;
    for (const Pin* pin : edge->getPins()) {
      const Node* node = pin->getNode();
      if (node != nullptr && node->isStdCell() && !node->isFixed()) {
        movablePins++;
      }
    }
    if (movablePins == 0) {
      continue;
    }

    stats.frontier_edges += 1;
    stats.frontier_nodes += movablePins;
    const double frontierShare = edgeShare / static_cast<double>(movablePins);
    for (const Pin* pin : edge->getPins()) {
      mgr->noteExactPolishFrontier(pin->getNode(), frontierShare);
    }
  }

  return stats;
}

}  // namespace

bool DetailedRandom::isOperator(char ch) const
{
  return ch == '+' || ch == '-' || ch == '*' || ch == '/' || ch == '^';
}

bool DetailedRandom::isObjective(char ch) const
{
  return ch >= 'a' && ch <= 'z';
}

bool DetailedRandom::isNumber(char ch) const
{
  return ch >= '0' && ch <= '9';
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DetailedRandom::DetailedRandom(Architecture* arch, Network* network)
    : arch_(arch), network_(network)
{
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::run(DetailedMgr* mgrPtr, const std::string& command)
{
  // A temporary interface to allow for a string which we will decode to create
  // the arguments.
  boost::char_separator<char> separators(" \r\t\n;");
  boost::tokenizer<boost::char_separator<char>> tokens(command, separators);
  std::vector<std::string> args;
  for (const auto& token : tokens) {
    args.push_back(token);
  }
  run(mgrPtr, args);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::run(DetailedMgr* mgrPtr, std::vector<std::string>& args)
{
  // This is, more or less, a greedy or low temperature anneal.  It is capable
  // of handling very complex objectives, etc.  There should be a lot of
  // arguments provided actually.  But, right now, I am just getting started.

  mgrPtr_ = mgrPtr;

  std::string generatorStr;
  std::string objectiveStr;
  std::string costStr;
  movesPerCandidate_ = 3.0;
  int passes = 1;
  double tol = 0.01;
  for (size_t i = 1; i < args.size(); i++) {
    if (args[i] == "-f" && i + 1 < args.size()) {
      movesPerCandidate_ = std::atof(args[++i].c_str());
    } else if (args[i] == "-p" && i + 1 < args.size()) {
      passes = std::atoi(args[++i].c_str());
    } else if (args[i] == "-t" && i + 1 < args.size()) {
      tol = std::atof(args[++i].c_str());
    } else if (args[i] == "-gen" && i + 1 < args.size()) {
      generatorStr = args[++i];
    } else if (args[i] == "-obj" && i + 1 < args.size()) {
      objectiveStr = args[++i];
    } else if (args[i] == "-cost" && i + 1 < args.size()) {
      costStr = args[++i];
    } else if (args[i] == "-focus" && i + 1 < args.size()) {
      const std::string focusStr = args[++i];
      if (focusStr == "polish") {
        focusMode_ = FocusMode_ExactPolish;
      } else {
        focusMode_ = FocusMode_All;
      }
    } else if (args[i] == "-focus_limit" && i + 1 < args.size()) {
      focusCandidateLimit_ = std::atoi(args[++i].c_str());
    }
  }
  tol = std::max(tol, 0.01);
  passes = std::max(passes, 1);
  focusCandidateLimit_ = std::max(16, focusCandidateLimit_);

  // Generators.
  for (auto generator : generators_) {
    delete generator;
  }
  generators_.clear();

  // Additional generators per the command. XXX: Need to write the code for
  // these objects; just a concept now.
  if (!generatorStr.empty()) {
    boost::char_separator<char> separators(" \r\t\n:");
    boost::tokenizer<boost::char_separator<char>> tokens(generatorStr,
                                                         separators);
    std::vector<std::string> gens;
    for (const auto& token : tokens) {
      gens.push_back(token);
    }

    for (const auto& gen : gens) {
      // if( gens[i] == "ro" )       std::cout << "reorder generator requested."
      // << std::endl; else if( gens[i] == "mis" ) std::cout << "set matching
      // generator requested." << std::endl;
      if (gen == "gs") {
        generators_.push_back(new DetailedGlobalSwap());
      } else if (gen == "vs") {
        generators_.push_back(new DetailedVerticalSwap());
      } else if (gen == "rng") {
        generators_.push_back(new RandomGenerator());
      } else if (gen == "disp") {
        generators_.push_back(new DisplacementGenerator());
      } else if (gen == "tpair") {
        generators_.push_back(new TouchedPairGenerator());
      } else if (gen == "tend") {
        generators_.push_back(new TouchedEndpointGenerator());
      }
    }
  }
  if (generators_.empty()) {
    // Default generator.
    generators_.push_back(new RandomGenerator());
  }
  for (auto generator : generators_) {
    generator->init(mgrPtr_);

    mgrPtr_->getLogger()->info(DPL,
                               324,
                               "Random improver is using {:s} generator.",
                               generator->getName().c_str());
  }

  // Objectives.
  for (auto objective : objectives_) {
    delete objective;
  }
  objectives_.clear();

  // Additional objectives per the command. XXX: Need to write the code for
  // these objects; just a concept now.
  if (!objectiveStr.empty()) {
    boost::char_separator<char> separators(" \r\t\n:");
    boost::tokenizer<boost::char_separator<char>> tokens(objectiveStr,
                                                         separators);
    std::vector<std::string> objs;
    for (boost::tokenizer<boost::char_separator<char>>::iterator it
         = tokens.begin();
         it != tokens.end();
         it++) {
      objs.push_back(*it);
    }

    for (const auto& obj : objs) {
      if (obj == "abu") {
        auto objABU = new DetailedABU(arch_, network_);
        objABU->init(mgrPtr_, nullptr);
        objectives_.push_back(objABU);
      } else if (obj == "disp") {
        auto objDisp = new DetailedDisplacement(arch_);
        objDisp->init(mgrPtr_, nullptr);
        objectives_.push_back(objDisp);
      } else if (obj == "hpwl") {
        auto objHpwl = new DetailedHPWL(network_);
        objHpwl->init(mgrPtr_, nullptr);
        objectives_.push_back(objHpwl);
      }
    }
  }
  if (objectives_.empty()) {
    // Default objective.
    auto objHpwl = new DetailedHPWL(network_);
    objHpwl->init(mgrPtr_, nullptr);
    objectives_.push_back(objHpwl);
  }

  for (auto objective : objectives_) {
    mgrPtr_->getLogger()->info(DPL,
                               325,
                               "Random improver is using {:s} objective.",
                               objective->getName().c_str());
  }

  // Should I just be figuring out the objectives needed from the cost string?
  if (!costStr.empty()) {
    // Replace substrings of objectives with a number.
    for (size_t i = objectives_.size(); i > 0;) {
      --i;
      for (;;) {
        size_t pos = costStr.find(objectives_[i]->getName());
        if (pos == std::string::npos) {
          break;
        }
        std::string val;
        val.append(1, (char) ('a' + i));
        costStr.replace(pos, objectives_[i]->getName().length(), val);
      }
    }

    mgrPtr_->getLogger()->info(
        DPL, 326, "Random improver cost string is {:s}.", costStr.c_str());

    expr_.clear();
    for (std::string::iterator it = costStr.begin(); it != costStr.end();
         ++it) {
      if (*it == '(' || *it == ')') {
      } else if (isOperator(*it) || isObjective(*it)) {
        expr_.emplace_back(1, *it);
      } else {
        std::string val;
        while (!isOperator(*it) && !isObjective(*it) && it != costStr.end()
               && *it != '(' && *it != ')') {
          val.append(1, *it);
          ++it;
        }
        expr_.push_back(val);
        --it;
      }
    }
  } else {
    expr_.clear();
    expr_.emplace_back(1, 'a');
    for (size_t i = 1; i < objectives_.size(); i++) {
      expr_.emplace_back(1, (char) ('a' + i));
      expr_.emplace_back(1, '+');
    }
  }

  currCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    currCost_[i] = objectives_[i]->curr();
  }
  double iCost = eval(currCost_, expr_);

  for (int p = 1; p <= passes; p++) {
    mgrPtr_->resortSegments();  // Needed?
    double change = go();
    mgrPtr_->getLogger()->info(
        DPL,
        327,
        "Pass {:3d} of random improver; improvement in cost is {:.2f} percent.",
        p,
        (change * 100));
    if (change < tol) {
      break;
    }
  }
  mgrPtr_->resortSegments();  // Needed?

  currCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    currCost_[i] = objectives_[i]->curr();
  }
  double fCost = eval(currCost_, expr_);

  double imp = (((iCost - fCost) / iCost) * 100.);
  mgrPtr_->getLogger()->info(
      DPL, 328, "End of random improver; improvement is {:.6f} percent.", imp);

  // Cleanup.
  for (auto generator : generators_) {
    delete generator;
  }
  generators_.clear();
  for (auto objective : objectives_) {
    delete objective;
  }
  objectives_.clear();
}

double DetailedRandom::doOperation(double a, double b, char op) const
{
  switch (op) {
    case '+':
      return b + a;
      break;
    case '-':
      return b - a;
      break;
    case '*':
      return b * a;
      break;
    case '/':
      return b / a;
      break;
    case '^':
      return std::pow(b, a);
      break;
    default:
      break;
  }
  return 0.0;
}

double DetailedRandom::eval(const std::vector<double>& costs,
                            const std::vector<std::string>& expr) const
{
  std::stack<double> stk;
  for (const std::string& val : expr) {
    if (isOperator(val[0])) {
      double a = stk.top();
      stk.pop();
      double b = stk.top();
      stk.pop();
      stk.push(doOperation(a, b, val[0]));
    } else if (isObjective(val[0])) {
      stk.push(costs[(int) (val[0] - 'a')]);
    } else {
      // Assume number.
      stk.push(std::stod(val));
    }
  }
  if (stk.size() != 1) {
    // Cost function should never be negative.  If we have a problem,
    // then return some negative value and we can catch this error.
    return -1.0;
  }
  return stk.top();
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
double DetailedRandom::go()
{
  if (generators_.empty()) {
    mgrPtr_->getLogger()->info(
        DPL, 329, "Random improver requires at least one generator.");
    return 0.0;
  }

  // Collect candidate cells.
  collectCandidates();
  if (candidates_.empty()) {
    mgrPtr_->getLogger()->info(DPL, 203, "No movable cells found");
    return 0.0;
  }

  // Try to improve.
  int maxAttempts
      = (int) std::ceil(movesPerCandidate_ * (double) candidates_.size());
  mgrPtr_->shuffle(candidates_);

  if (focusMode_ == FocusMode_ExactPolish) {
    mgrPtr_->getLogger()->info(
        DPL,
        342,
        "Focused exact polish candidates: anchors={} frontier={} union={} "
        "limit={}.",
        mgrPtr_->getExactPolishAnchorNodes(focusCandidateLimit_).size(),
        mgrPtr_->getExactPolishFrontierNodes(focusCandidateLimit_).size(),
        candidates_.size(),
        focusCandidateLimit_);
  }

  deltaCost_.resize(objectives_.size());
  initCost_.resize(objectives_.size());
  currCost_.resize(objectives_.size());
  nextCost_.resize(objectives_.size());
  for (size_t i = 0; i < objectives_.size(); i++) {
    deltaCost_[i] = 0.;
    initCost_[i] = objectives_[i]->curr();
    currCost_[i] = initCost_[i];
    nextCost_[i] = initCost_[i];

    if (objectives_[i]->getName() == "abu") {
      auto ptr = dynamic_cast<DetailedABU*>(objectives_[i]);
      if (ptr != nullptr) {
        ptr->measureABU(true);
      }
    }
  }

  // Test.
  if (eval(currCost_, expr_) < 0.0) {
    mgrPtr_->getLogger()->info(DPL,
                               330,
                               "Test objective function failed, possibly due "
                               "to a badly formed cost function.");
    return 0.0;
  }

  double currTotalCost;
  double initTotalCost;
  initTotalCost = eval(currCost_, expr_);
  currTotalCost = initTotalCost;

  std::vector<int> gen_count(generators_.size());
  std::vector<int> gen_generated(generators_.size());
  std::vector<int> gen_accepted(generators_.size());
  std::vector<int> gen_rejected(generators_.size());
  std::vector<int> gen_failed(generators_.size());
  std::vector<double> gen_accepted_delta(generators_.size());
  FocusRefreshStats focus_refresh_stats;
  std::ranges::fill(gen_count, 0);
  for (int attempt = 0; attempt < maxAttempts; attempt++) {
    // Pick a generator at random.
    int g = mgrPtr_->getRandom(generators_.size());
    ++gen_count[g];
    // Generate a move list.
    if (!generators_[g]->generate(mgrPtr_, candidates_)) {
      // Failed to generate anything so just move on to the next attempt.
      ++gen_failed[g];
      continue;
    }
    ++gen_generated[g];

    // The generator has provided a successful move which is stored in the
    // manager.  We need to evaluate that move to see if we should accept
    // or reject it.  Scan over the objective functions and use the move
    // information to compute the weighted deltas; an overall weighted delta
    // better than zero implies improvement.
    for (size_t i = 0; i < objectives_.size(); i++) {
      // XXX: NEED TO WEIGHT EACH OBJECTIVE!
      double change = objectives_[i]->delta(mgrPtr_->getJournal());

      deltaCost_[i] = change;
      nextCost_[i] = currCost_[i] - deltaCost_[i];  // -delta is +ve is less.
    }
    const double nextTotalCost = eval(nextCost_, expr_);
    if (nextTotalCost <= currTotalCost) {
      gen_accepted[g] += 1;
      const double acceptedGain = currTotalCost - nextTotalCost;
      gen_accepted_delta[g] += acceptedGain;
      if (focusMode_ == FocusMode_ExactPolish) {
        const auto refresh_stats = noteAcceptedExactPolishFrontier(
            mgrPtr_, mgrPtr_->getJournal(), acceptedGain);
        focus_refresh_stats.accepted_moves += refresh_stats.accepted_moves;
        focus_refresh_stats.anchor_nodes += refresh_stats.anchor_nodes;
        focus_refresh_stats.frontier_edges += refresh_stats.frontier_edges;
        focus_refresh_stats.frontier_nodes += refresh_stats.frontier_nodes;
      }
      mgrPtr_->acceptMove();
      for (auto objective : objectives_) {
        objective->accept();
      }

      // A great, but time-consuming, check here is to recompute the costs from
      // scratch and make sure they are the same as the incrementally computed
      // costs.  Very useful for debugging!  Could do this check ever so often
      // or just at the end...
      ;
      for (size_t i = 0; i < objectives_.size(); i++) {
        currCost_[i] = nextCost_[i];
      }
      currTotalCost = nextTotalCost;
    } else {
      gen_rejected[g] += 1;
      mgrPtr_->rejectMove();
      for (auto objective : objectives_) {
        objective->reject();
      }
    }
  }
  for (size_t i = 0; i < gen_count.size(); i++) {
    mgrPtr_->getLogger()->info(DPL,
                               332,
                               "End of pass, Generator {:s} called {:d} times.",
                               generators_[i]->getName().c_str(),
                               gen_count[i]);
  }
  for (auto generator : generators_) {
    generator->stats();
  }
  for (size_t i = 0; i < generators_.size(); i++) {
    mgrPtr_->getLogger()->info(
        DPL,
        343,
        "Focused generator {:s}: probes={} generated={} accepted={} "
        "rejected={} replay_failures={} accepted_delta={:.2f}.",
        generators_[i]->getName().c_str(),
        gen_count[i],
        gen_generated[i],
        gen_accepted[i],
        gen_rejected[i],
        gen_failed[i],
        gen_accepted_delta[i]);
  }
  if (focusMode_ == FocusMode_ExactPolish) {
    mgrPtr_->getLogger()->info(
        DPL,
        346,
        "Focused exact polish refresh: accepted_moves={} anchor_nodes={} "
        "frontier_edges={} frontier_nodes={}.",
        focus_refresh_stats.accepted_moves,
        focus_refresh_stats.anchor_nodes,
        focus_refresh_stats.frontier_edges,
        focus_refresh_stats.frontier_nodes);
  }

  for (size_t i = 0; i < objectives_.size(); i++) {
    double scratch = objectives_[i]->curr();
    nextCost_[i] = scratch;  // Temporary.
    bool error = (std::fabs(scratch - currCost_[i]) > 1.0e-3);
    mgrPtr_->getLogger()->info(
        DPL,
        333,
        "End of pass, Objective {:s}, Initial cost {:.6e}, Scratch cost "
        "{:.6e}, Incremental cost {:.6e}, Mismatch? {:c}",
        objectives_[i]->getName().c_str(),
        initCost_[i],
        scratch,
        currCost_[i],
        ((error) ? 'Y' : 'N'));

    if (objectives_[i]->getName() == "abu") {
      auto ptr = dynamic_cast<DetailedABU*>(objectives_[i]);
      if (ptr != nullptr) {
        ptr->measureABU(true);
      }
    }
  }
  const double nextTotalCost = eval(nextCost_, expr_);
  mgrPtr_->getLogger()->info(
      DPL, 338, "End of pass, Total cost is {:.6e}.", nextTotalCost);

  return ((initTotalCost - currTotalCost) / initTotalCost);
}

//////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////////////////////////////////////////////////////
void DetailedRandom::collectCandidates()
{
  candidates_.clear();
  if (focusMode_ == FocusMode_ExactPolish) {
    candidates_ = mgrPtr_->getExactPolishNodes(focusCandidateLimit_);
    return;
  }
  candidates_.insert(candidates_.end(),
                     mgrPtr_->getSingleHeightCells().begin(),
                     mgrPtr_->getSingleHeightCells().end());
  for (size_t i = 2; i < mgrPtr_->getNumMultiHeights(); i++) {
    candidates_.insert(candidates_.end(),
                       mgrPtr_->getMultiHeightCells(i).begin(),
                       mgrPtr_->getMultiHeightCells(i).end());
  }
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
RandomGenerator::RandomGenerator() : DetailedGenerator("random")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool RandomGenerator::generate(DetailedMgr* mgr, std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();

  const int ydim = mgr_->getNumSingleHeightRows();
  double xwid = arch_->getRow(0)->getSiteSpacing().v;
  const int xdim
      = std::max(0, (int) ((arch_->getMaxX() - arch_->getMinX()).v / xwid));

  xwid = (arch_->getMaxX() - arch_->getMinX()).v / (double) xdim;
  double ywid = (arch_->getMaxY() - arch_->getMinY()).v / (double) ydim;

  Node* ndi = candidates[mgr_->getRandom(candidates.size())];
  const int spanned_i = arch_->getCellHeightInRows(ndi);
  if (spanned_i != 1) {
    return false;
  }
  // Segments for the source.
  const std::vector<DetailedSeg*>& segs_i
      = mgr_->getReverseCellToSegs(ndi->getId());
  if (segs_i.size() != 1) {
    mgr_->getLogger()->error(
        DPL, 385, "Only working with single height cells currently.");
  }

  // For the window size.  This should be parameterized.
  const int rly = 10;
  const int rlx = 10;

  const int tries = 5;
  for (int t = 1; t <= tries; t++) {
    // Position of the source.
    const double yi = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
    const double xi = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

    // Segment for the source.
    const int si = segs_i[0]->getSegId();

    // Random position within a box centered about (xi,yi).
    const int grid_xi = std::min(
        xdim - 1, std::max(0, (int) ((xi - arch_->getMinX().v) / xwid)));
    const int grid_yi = std::min(
        ydim - 1, std::max(0, (int) ((yi - arch_->getMinY().v) / ywid)));

    const int rel_x = mgr_->getRandom(2 * rlx + 1);
    const int rel_y = mgr_->getRandom(2 * rly + 1);

    const int grid_xj
        = std::min(xdim - 1, std::max(0, (grid_xi - rlx + rel_x)));
    const int grid_yj
        = std::min(ydim - 1, std::max(0, (grid_yi - rly + rel_y)));

    // Position of the destination.
    const double xj = arch_->getMinX().v + grid_xj * xwid;
    double yj = arch_->getMinY().v + grid_yj * ywid;

    // Row and segment for the destination.
    int rj = (int) ((yj - arch_->getMinY().v) / mgr_->getSingleRowHeight().v);
    rj = std::min(mgr_->getNumSingleHeightRows() - 1, std::max(0, rj));
    yj = arch_->getRow(rj)->getBottom().v;
    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      const DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }

    // Need to determine validity of things.
    if (sj == -1 || ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
      // The target segment cannot support the candidate cell.
      continue;
    }

    if (mgr_->tryMove(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++moves_;
      return true;
    }
    if (mgr_->trySwap(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++swaps_;
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void RandomGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      335,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
DisplacementGenerator::DisplacementGenerator()
    : DetailedGenerator("displacement")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool DisplacementGenerator::generate(DetailedMgr* mgr,
                                     std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr->getArchitecture();
  network_ = mgr->getNetwork();

  const int ydim = mgr_->getNumSingleHeightRows();
  double xwid = arch_->getRow(0)->getSiteSpacing().v;
  const int xdim
      = std::max(0, (int) ((arch_->getMaxX() - arch_->getMinX()).v / xwid));

  xwid = (arch_->getMaxX() - arch_->getMinX()).v / (double) xdim;
  double ywid = (arch_->getMaxY() - arch_->getMinY()).v / (double) ydim;

  Node* ndi = candidates[mgr_->getRandom(candidates.size())];

  // Segments for the source.
  const std::vector<DetailedSeg*>& segs_i
      = mgr_->getReverseCellToSegs(ndi->getId());

  // For the window size.  This should be parameterized.
  const int rly = 5;
  const int rlx = 5;

  const int tries = 5;
  for (int t = 1; t <= tries; t++) {
    // Position of the source.
    // yi = ndi->getBottom()+0.5*ndi->getHeight();
    // xi = ndi->getLeft()+0.5*ndi->getWidth();

    // Segment for the source.
    const int si = segs_i[0]->getSegId();

    // Choices: (i) random position within a box centered at the original
    // position; (ii) random position within a box between the current
    // and original position; (iii) the original position itself.  Should
    // this also be a randomized choice??????????????????????????????????
    double xj, yj;
    constexpr bool option_1 = true;
    constexpr bool option_2 = false;
    if (option_1) {
      // Centered at the original position within a box.
      const double orig_yc = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
      const double orig_xc = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;

      const int grid_xi = std::min(
          xdim - 1, std::max(0, (int) ((orig_xc - arch_->getMinX().v) / xwid)));
      const int grid_yi = std::min(
          ydim - 1, std::max(0, (int) ((orig_yc - arch_->getMinY().v) / ywid)));

      const int rel_x = mgr_->getRandom(2 * rlx + 1);
      const int rel_y = mgr_->getRandom(2 * rly + 1);

      const int grid_xj
          = std::min(xdim - 1, std::max(0, (grid_xi - rlx + rel_x)));
      const int grid_yj
          = std::min(ydim - 1, std::max(0, (grid_yi - rly + rel_y)));

      xj = arch_->getMinX().v + grid_xj * xwid;
      yj = arch_->getMinY().v + grid_yj * ywid;
    } else if (option_2) {
      // The original position.
      xj = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;
      yj = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
    } else {
      // Somewhere between current position and original position.
      double orig_yc = ndi->getOrigBottom().v + 0.5 * ndi->getHeight().v;
      double orig_xc = ndi->getOrigLeft().v + 0.5 * ndi->getWidth().v;

      double curr_yc = ndi->getBottom().v + 0.5 * ndi->getHeight().v;
      double curr_xc = ndi->getLeft().v + 0.5 * ndi->getWidth().v;

      int grid_xi = std::min(
          xdim - 1, std::max(0, (int) ((curr_xc - arch_->getMinX().v) / xwid)));
      int grid_yi = std::min(
          ydim - 1, std::max(0, (int) ((curr_yc - arch_->getMinY().v) / ywid)));

      int grid_xj = std::min(
          xdim - 1, std::max(0, (int) ((orig_xc - arch_->getMinX().v) / xwid)));
      int grid_yj = std::min(
          ydim - 1, std::max(0, (int) ((orig_yc - arch_->getMinY().v) / ywid)));

      if (grid_xi > grid_xj) {
        std::swap(grid_xi, grid_xj);
      }
      if (grid_yi > grid_yj) {
        std::swap(grid_yi, grid_yj);
      }

      const int w = grid_xj - grid_xi;
      const int h = grid_yj - grid_yi;

      const int rel_x = mgr_->getRandom(w + 1);
      const int rel_y = mgr_->getRandom(h + 1);

      grid_xj = std::min(xdim - 1, std::max(0, (grid_xi + rel_x)));
      grid_yj = std::min(ydim - 1, std::max(0, (grid_yi + rel_y)));

      xj = arch_->getMinX().v + grid_xj * xwid;
      yj = arch_->getMinY().v + grid_yj * ywid;
    }

    // Row and segment for the destination.
    int rj = (int) ((yj - arch_->getMinY().v) / mgr_->getSingleRowHeight().v);
    rj = std::min(mgr_->getNumSingleHeightRows() - 1, std::max(0, rj));
    yj = arch_->getRow(rj)->getBottom().v;
    int sj = -1;
    for (int s = 0; s < mgr_->getNumSegsInRow(rj); s++) {
      DetailedSeg* segPtr = mgr_->getSegsInRow(rj)[s];
      if (xj >= segPtr->getMinX() && xj <= segPtr->getMaxX()) {
        sj = segPtr->getSegId();
        break;
      }
    }

    // Need to determine validity of things.
    if (sj == -1 || ndi->getGroupId() != mgr_->getSegment(sj)->getRegId()) {
      // The target segment cannot support the candidate cell.
      continue;
    }

    if (mgr_->tryMove(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++moves_;
      return true;
    }
    if (mgr_->trySwap(ndi,
                      ndi->getLeft(),
                      ndi->getBottom(),
                      si,
                      DbuX{(int) std::round(xj)},
                      DbuY{(int) std::round(yj)},
                      sj)) {
      ++swaps_;
      return true;
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void DisplacementGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      337,
      "Generator {:s}, "
      "Cumulative attempts {:d}, swaps {:d}, moves {:5d} since last reset.",
      getName().c_str(),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
TouchedPairGenerator::TouchedPairGenerator()
    : DetailedGenerator("touched_pair")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedPairGenerator::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  anchors_ = mgr_->getExactPolishAnchorNodes(192);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool TouchedPairGenerator::generate(DetailedMgr* mgr,
                                    std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  if (anchors_.empty()) {
    anchors_ = candidates.empty() ? mgr_->getExactPolishAnchorNodes(192)
                                  : candidates;
  }
  if (anchors_.empty()) {
    return false;
  }

  const std::array<int, 6> neighbor_offsets{{-1, 1, -2, 2, -3, 3}};
  const int anchor_trials = std::min(6, static_cast<int>(anchors_.size()));
  for (int trial = 0; trial < anchor_trials; trial++) {
    Node* ndi = anchors_[mgr_->getRandom(anchors_.size())];
    if (ndi == nullptr || ndi->isFixed() || !ndi->isStdCell()
        || !arch_->isSingleHeightCell(ndi)) {
      continue;
    }
    const auto& segs_i = mgr_->getReverseCellToSegs(ndi->getId());
    if (segs_i.size() != 1) {
      continue;
    }
    const int si = segs_i[0]->getSegId();
    const int src_row = segs_i[0]->getRowId();

    mgr_->sortCellsInSeg(si);
    const std::vector<Node*>& same_seg_nodes = mgr_->getCellsInSeg(si);
    auto anchor_it = std::find(same_seg_nodes.begin(), same_seg_nodes.end(), ndi);
    if (anchor_it != same_seg_nodes.end()) {
      const int anchor_idx
          = static_cast<int>(std::distance(same_seg_nodes.begin(), anchor_it));
      for (const int offset : neighbor_offsets) {
        const int neighbor_idx = anchor_idx + offset;
        if (neighbor_idx < 0 || neighbor_idx >= same_seg_nodes.size()) {
          continue;
        }
        Node* ndj = same_seg_nodes[neighbor_idx];
        if (ndj == nullptr || ndj == ndi || ndj->isFixed()
            || !ndj->isStdCell() || !arch_->isSingleHeightCell(ndj)) {
          continue;
        }
        if (mgr_->trySwap(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          ndj->getLeft(),
                          ndj->getBottom(),
                          si)) {
          ++swaps_;
          return true;
        }
        if (mgr_->tryMove(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          ndj->getLeft(),
                          ndj->getBottom(),
                          si)) {
          ++moves_;
          return true;
        }
      }
    }

    for (const int row_delta : {-1, 1, -2, 2}) {
      const int row_id = src_row + row_delta;
      if (row_id < 0 || row_id >= mgr_->getNumSingleHeightRows()) {
        continue;
      }
      Node* best_partner = nullptr;
      int best_seg = -1;
      int best_dist = std::numeric_limits<int>::max();
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        const int sj = seg_ptr->getSegId();
        mgr_->sortCellsInSeg(sj);
        const std::vector<Node*>& row_nodes = mgr_->getCellsInSeg(sj);
        for (Node* ndj : row_nodes) {
          if (ndj == nullptr || ndj->isFixed() || !ndj->isStdCell()
              || !arch_->isSingleHeightCell(ndj)) {
            continue;
          }
          const int dist = std::abs((ndj->getCenterX() - ndi->getCenterX()).v);
          if (dist < best_dist) {
            best_dist = dist;
            best_partner = ndj;
            best_seg = sj;
          }
        }
      }
      if (best_partner != nullptr
          && mgr_->trySwap(ndi,
                           ndi->getLeft(),
                           ndi->getBottom(),
                           si,
                           best_partner->getLeft(),
                           best_partner->getBottom(),
                           best_seg)) {
        ++swaps_;
        return true;
      }
    }
  }
  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedPairGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      344,
      "Generator {:s}, anchors {:d}, cumulative attempts {:d}, swaps {:d}, "
      "moves {:5d} since last reset.",
      getName().c_str(),
      static_cast<int>(anchors_.size()),
      attempts_,
      swaps_,
      moves_);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
TouchedEndpointGenerator::TouchedEndpointGenerator()
    : DetailedGenerator("touched_endpoint")
{
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedEndpointGenerator::init(DetailedMgr* mgr)
{
  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  frontier_ = mgr_->getExactPolishFrontierNodes(256);
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
bool TouchedEndpointGenerator::generate(DetailedMgr* mgr,
                                        std::vector<Node*>& candidates)
{
  ++attempts_;

  mgr_ = mgr;
  arch_ = mgr_->getArchitecture();
  network_ = mgr_->getNetwork();
  if (frontier_.empty()) {
    frontier_ = candidates.empty() ? mgr_->getExactPolishFrontierNodes(256)
                                   : candidates;
  }
  if (frontier_.empty()) {
    return false;
  }

  const int site_width = arch_->getRow(0)->getSiteWidth().v;
  const int row_height = mgr_->getSingleRowHeight().v;
  const int trials = std::min(8, static_cast<int>(frontier_.size()));
  for (int trial = 0; trial < trials; trial++) {
    Node* ndi = frontier_[mgr_->getRandom(frontier_.size())];
    if (ndi == nullptr || ndi->isFixed() || !ndi->isStdCell()
        || !arch_->isSingleHeightCell(ndi)) {
      continue;
    }
    const auto& segs_i = mgr_->getReverseCellToSegs(ndi->getId());
    if (segs_i.size() != 1) {
      continue;
    }
    const int si = segs_i[0]->getSegId();
    const int src_row = segs_i[0]->getRowId();
    const int curr_center_x = ndi->getCenterX().v;
    const int curr_center_y = ndi->getCenterY().v;

    double target_center_x = curr_center_x;
    double target_center_y = curr_center_y;
    int x_votes = 0;
    int y_votes = 0;
    double fallback_span = -1.0;
    double fallback_x = curr_center_x;
    double fallback_y = curr_center_y;
    for (const Pin* pin : ndi->getPins()) {
      Edge* edge = pin->getEdge();
      if (edge == nullptr) {
        continue;
      }
      const int npins = edge->getNumPins();
      if (npins <= 1 || npins > 16) {
        continue;
      }

      int xmin = std::numeric_limits<int>::max();
      int xmax = std::numeric_limits<int>::min();
      int ymin = std::numeric_limits<int>::max();
      int ymax = std::numeric_limits<int>::min();
      for (const Pin* edge_pin : edge->getPins()) {
        const Node* ndj = edge_pin->getNode();
        if (ndj == nullptr || ndj == ndi) {
          continue;
        }
        const int pin_x = (ndj->getCenterX() + edge_pin->getOffsetX()).v;
        const int pin_y = (ndj->getCenterY() + edge_pin->getOffsetY()).v;
        xmin = std::min(xmin, pin_x);
        xmax = std::max(xmax, pin_x);
        ymin = std::min(ymin, pin_y);
        ymax = std::max(ymax, pin_y);
      }
      if (xmin > xmax || ymin > ymax) {
        continue;
      }

      const int curr_pin_x = (ndi->getCenterX() + pin->getOffsetX()).v;
      const int curr_pin_y = (ndi->getCenterY() + pin->getOffsetY()).v;
      const int desired_pin_x = std::clamp(curr_pin_x, xmin, xmax);
      const int desired_pin_y = std::clamp(curr_pin_y, ymin, ymax);
      if (desired_pin_x != curr_pin_x) {
        target_center_x += desired_pin_x - pin->getOffsetX().v;
        x_votes++;
      }
      if (desired_pin_y != curr_pin_y) {
        target_center_y += desired_pin_y - pin->getOffsetY().v;
        y_votes++;
      }

      const double span = static_cast<double>((xmax - xmin) + (ymax - ymin));
      if (span > fallback_span) {
        fallback_span = span;
        fallback_x = (0.5 * static_cast<double>(xmin + xmax))
                     - static_cast<double>(pin->getOffsetX().v);
        fallback_y = (0.5 * static_cast<double>(ymin + ymax))
                     - static_cast<double>(pin->getOffsetY().v);
      }
    }

    if (x_votes > 0) {
      target_center_x /= (x_votes + 1);
    } else {
      target_center_x = fallback_x;
    }
    if (y_votes > 0) {
      target_center_y /= (y_votes + 1);
    } else {
      target_center_y = fallback_y;
    }

    const int target_left = static_cast<int>(
        std::llround(target_center_x - (0.5 * ndi->getWidth().v)));
    const int target_row = std::clamp(
        static_cast<int>((target_center_y - arch_->getMinY().v) / row_height),
        0,
        mgr_->getNumSingleHeightRows() - 1);

    std::vector<int> row_candidates;
    auto add_row = [&](const int row_id) {
      if (row_id < 0 || row_id >= mgr_->getNumSingleHeightRows()) {
        return;
      }
      if (std::find(row_candidates.begin(), row_candidates.end(), row_id)
          == row_candidates.end()) {
        row_candidates.push_back(row_id);
      }
    };
    add_row(src_row);
    add_row(target_row);
    add_row(target_row - 1);
    add_row(target_row + 1);
    add_row(src_row - 1);
    add_row(src_row + 1);

    for (const int row_id : row_candidates) {
      int best_seg = -1;
      int best_left = 0;
      int best_dist = std::numeric_limits<int>::max();
      for (DetailedSeg* seg_ptr : mgr_->getSegsInRow(row_id)) {
        if (seg_ptr == nullptr || seg_ptr->getRegId() != ndi->getGroupId()) {
          continue;
        }
        const int seg_min = seg_ptr->getMinX().v;
        const int seg_max = seg_ptr->getMaxX().v - ndi->getWidth().v;
        if (seg_max < seg_min) {
          continue;
        }
        const int clamped_left = std::max(seg_min, std::min(target_left, seg_max));
        const int dist = std::abs(clamped_left - target_left);
        if (dist < best_dist) {
          best_dist = dist;
          best_left = clamped_left;
          best_seg = seg_ptr->getSegId();
        }
      }
      if (best_seg < 0) {
        continue;
      }

      for (const int site_delta : {0, -1, 1, -2, 2}) {
        DbuX candidate_left{best_left + (site_delta * site_width)};
        if (!mgr_->alignPos(
                ndi,
                candidate_left,
                mgr_->getSegment(best_seg)->getMinX(),
                mgr_->getSegment(best_seg)->getMaxX())) {
          continue;
        }
        DbuY candidate_bottom = arch_->getRow(row_id)->getBottom();
        if (candidate_left == ndi->getLeft()
            && candidate_bottom == ndi->getBottom()) {
          continue;
        }
        if (mgr_->tryMove(ndi,
                          ndi->getLeft(),
                          ndi->getBottom(),
                          si,
                          candidate_left,
                          candidate_bottom,
                          best_seg)) {
          ++moves_;
          return true;
        }
      }
    }
  }

  return false;
}

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
void TouchedEndpointGenerator::stats()
{
  mgr_->getLogger()->info(
      DPL,
      345,
      "Generator {:s}, frontier {:d}, cumulative attempts {:d}, swaps {:d}, "
      "moves {:5d} since last reset.",
      getName().c_str(),
      static_cast<int>(frontier_.size()),
      attempts_,
      swaps_,
      moves_);
}

}  // namespace dpl_evolve
