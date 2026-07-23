// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2021-2025, The OpenROAD Authors

// Description: Primarily for maintaining the segments.

#pragma once

#include <algorithm>
#include <cstdint>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "dpl_evolve/Opendp.h"
#include "infrastructure/Grid.h"
#include "infrastructure/network.h"
#include "util/journal.h"
#include "util/utility.h"
namespace utl {
class Logger;
}  // namespace utl
namespace dpl_evolve {
class PlacementDRC;

class Architecture;
class DetailedSeg;
class Network;

struct DetailedChainStats
{
  int source_topk_generated = 0;
  int source_topk_exact_scored = 0;
  int source_topk_replay_attempts = 0;
  int source_topk_replay_failures = 0;
  int source_topk_rollbacks = 0;
  int source_topk_accepts = 0;
  double source_topk_accepted_delta = 0.0;
  int accepted_nodes = 0;
  int hot_segments = 0;

  int selected_reorder_frontier = 0;
  int selected_reorder_windows = 0;
  int selected_reorder_accepts = 0;
  double selected_reorder_gain = 0.0;

  int critical_micro_start_probes = 0;
  int critical_micro_start_accepts = 0;
  double critical_micro_start_gain = 0.0;

  int critical_net_chain_probes = 0;
  int critical_net_chain_accepts = 0;
  double critical_net_chain_gain = 0.0;

  int exact_closure_windows = 0;
  int exact_closure_accepts = 0;
  double exact_closure_gain = 0.0;

  int multi_row_residual_probes = 0;
  int multi_row_residual_accepts = 0;
  double multi_row_residual_gain = 0.0;

  int segment_residual_swap_probes = 0;
  int segment_residual_swap_accepts = 0;
  double segment_residual_swap_gain = 0.0;
};

enum class BlockageType
{
  Placement,
  Routing,
  FixedInstance,
  None
};

struct Blockage
{
  DbuX x_min;
  DbuX x_max;
  DbuX pad_left;
  DbuX pad_right;
  BlockageType type;

  Blockage(DbuX xmin, DbuX xmax, DbuX pad_l, DbuX pad_r, BlockageType t)
      : x_min(xmin), x_max(xmax), pad_left(pad_l), pad_right(pad_r), type(t)
  {
  }
  DbuX getXMin() const { return x_min; }
  DbuX getXMax() const { return x_max; }
  DbuX getPaddedXMin() const { return x_min - pad_left; }
  DbuX getPaddedXMax() const { return x_max + pad_right; }
  bool isFixedInstance() const { return type == BlockageType::FixedInstance; }
  bool isPlacement() const { return type == BlockageType::Placement; }
};

////////////////////////////////////////////////////////////////////////////////
// Classes.
////////////////////////////////////////////////////////////////////////////////

////////////////////////////////////////////////////////////////////////////////
////////////////////////////////////////////////////////////////////////////////
class DetailedMgr
{
 public:
  DetailedMgr(Architecture* arch,
              Network* network,
              Grid* grid,
              PlacementDRC* drc_engine);
  virtual ~DetailedMgr();

  void cleanup();

  Architecture* getArchitecture() const { return arch_; }
  Network* getNetwork() const { return network_; }

  void setLogger(utl::Logger* logger) { logger_ = logger; }
  utl::Logger* getLogger() const { return logger_; }

  Grid* getGrid() const { return grid_; }

  void setSeed(int s);

  void setMaxDisplacement(int x, int y);
  void setDisallowOneSiteGaps(bool disallowOneSiteGaps);
  void getMaxDisplacement(int& x, int& y) const
  {
    x = maxDispX_;
    y = maxDispY_;
  }
  void setGlobalSwapParams(const GlobalSwapParams& params)
  {
    global_swap_params_ = params;
  }
  const GlobalSwapParams& getGlobalSwapParams() const
  {
    return global_swap_params_;
  }
  void setExtraDplEnabled(bool enabled) { extra_dpl_enabled_ = enabled; }
  bool isExtraDplEnabled() const { return extra_dpl_enabled_; }
  int getMaxDisplacementX() const { return maxDispX_; }
  int getMaxDisplacementY() const { return maxDispY_; }
  bool getDisallowOneSiteGaps() const { return disallowOneSiteGaps_; }

  void internalError(const std::string& msg);

  void findBlockages(bool includeRouteBlockages = true);
  void findRegionIntervals(
      int regId,
      std::vector<std::vector<std::pair<DbuX, DbuX>>>& intervals);

  void findSegments();
  DetailedSeg* findClosestSegment(const Node* nd);
  void findClosestSpanOfSegmentsDfs(
      const Node* ndi,
      DetailedSeg* segPtr,
      DbuX xmin,
      DbuX xmax,
      int bot,
      int top,
      std::vector<DetailedSeg*>& stack,
      std::vector<std::vector<DetailedSeg*>>& candidates);
  bool findClosestSpanOfSegments(Node* nd, std::vector<DetailedSeg*>& segments);
  bool isInsideABlockage(const Node* nd, DbuX position);
  void assignCellsToSegments(const std::vector<Node*>& nodesToConsider);
  int checkOverlapInSegments();
  int checkEdgeSpacingInSegments();
  bool hasPlacementViolation(const Node* node) const;
  int checkSiteAlignment();
  int checkRowAlignment();
  int checkRegionAssignment();
  void getOneSiteGapViolationsPerSegment(
      std::vector<std::vector<int>>& violating_cells,
      bool fix_violations);
  bool fixOneSiteGapViolations(Node* cell,
                               DbuX one_site_gap,
                               int segment,
                               Node* violatingNode);
  void removeCellFromSegment(const Node* nd, int seg);
  void addCellToSegment(Node* nd, int seg);
  int getCellSpacing(const Node* ndl, const Node* ndr);

  void collectSingleHeightCells();
  void collectMultiHeightCells();
  void collectFixedCells();
  void collectWideCells();

  void restoreOriginalPositions();
  void recordOriginalPositions();

  void resortSegments();
  void resortSegment(DetailedSeg* segPtr);
  void removeAllCellsFromSegments();

  int getNumSegments() const { return static_cast<int>(segments_.size()); }
  DetailedSeg* getSegment(int s) const { return segments_[s]; }
  int getNumSingleHeightRows() const { return numSingleHeightRows_; }
  DbuY getSingleRowHeight() const { return singleRowHeight_; }

  int getNumCellsInSeg(int segId) const { return cellsInSeg_[segId].size(); }
  const std::vector<Node*>& getCellsInSeg(int segId) const
  {
    return cellsInSeg_[segId];
  }
  void addToFrontCellsInSeg(int segId, Node* node)
  {
    cellsInSeg_[segId].insert(cellsInSeg_[segId].begin(), node);
  }
  void addToBackCellsInSeg(int segId, Node* node)
  {
    cellsInSeg_[segId].push_back(node);
  }
  void popFrontCellsInSeg(int segId)
  {
    cellsInSeg_[segId].erase(cellsInSeg_[segId].begin());
  }
  void popBackCellsInSeg(int segId) { cellsInSeg_[segId].pop_back(); }
  void sortCellsInSeg(int segId)
  {
    std::sort(cellsInSeg_[segId].begin(),
              cellsInSeg_[segId].end(),
              DetailedMgr::compareNodesX());
  }
  void sortCellsInSeg(int segId, int start, int end)
  {
    std::sort(cellsInSeg_[segId].begin() + start,
              cellsInSeg_[segId].begin() + end,
              DetailedMgr::compareNodesX());
  }

  int getNumSegsInRow(int rowId) const { return segsInRow_[rowId].size(); }
  const std::vector<DetailedSeg*>& getSegsInRow(int rowId) const
  {
    return segsInRow_[rowId];
  }

  int getNumReverseCellToSegs(int nodeId) const
  {
    return reverseCellToSegs_[nodeId].size();
  }
  const std::vector<DetailedSeg*>& getReverseCellToSegs(int nodeId) const
  {
    return reverseCellToSegs_[nodeId];
  }

  const std::vector<Node*>& getSingleHeightCells() const
  {
    return singleHeightCells_;
  }

  int getNumMultiHeights() const { return multiHeightCells_.size(); }
  const std::vector<Node*>& getMultiHeightCells(int height) const
  {
    return multiHeightCells_[height];
  }
  const std::vector<Node*>& getWideCells() const { return wideCells_; }

  void shuffle(std::vector<Node*>& nodes);
  int getRandom(int limit) const { return (*rng_)() % limit; }
  Placer_RNG getRngState() const { return *rng_; }
  void setRngState(const Placer_RNG& state) { *rng_ = state; }

  void getSpaceAroundCell(int seg,
                          int ix,
                          DbuX& space,
                          DbuX& larger,
                          int limit = 3);
  void getSpaceAroundCell(int seg,
                          int ix,
                          DbuX& space_left,
                          DbuX& space_right,
                          DbuX& large_left,
                          DbuX& large_right,
                          int limit = 3);

  void removeSegmentOverlapSingle(int regId = -1);
  void removeSegmentOverlapSingleInner(std::vector<Node*>& nodes,
                                       int l,
                                       int r,
                                       int rowId);

  double getTargetUt() const { return targetUt_; }
  void setTargetUt(double ut) { targetUt_ = ut; }

  // Routines for generating moves and swaps.
  bool tryMove(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);
  bool trySwap(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);

  // For accepting or rejecting moves and swaps.
  void acceptMove();
  void rejectMove();

  // For help aligning cells to sites.
  bool alignPos(const Node* ndi, DbuX& xi, DbuX xl, DbuX xr);
  int getMoveLimit() { return moveLimit_; }
  void setMoveLimit(unsigned int newMoveLimit) { moveLimit_ = newMoveLimit; }

  // Journal operations
  Journal& getJournal() { return journal_; }
  const Journal& getJournal() const { return journal_; }
  bool replayMoveActions(const std::vector<MoveCellAction>& actions);
  void resetDetailedChainState();
  void clearDetailedFrontier();
  void addAcceptedNode(Node* node);
  void addCriticalFrontierNode(Node* node);
  void addHotSegment(int seg_id);
  void addTouchedEdge(Edge* edge);
  void noteAcceptedJournal(const Journal& journal,
                           double accepted_delta,
                           bool producer_transaction = false);
  const std::vector<Node*>& getAcceptedNodes() const { return accepted_nodes_; }
  const std::vector<Node*>& getCriticalFrontier() const
  {
    return critical_frontier_;
  }
  const std::vector<Edge*>& getTouchedEdges() const { return touched_edges_; }
  const std::vector<int>& getHotSegments() const { return hot_segments_; }
  double getAcceptedDelta() const { return accepted_delta_; }
  DetailedChainStats& getDetailedChainStats() { return detailed_chain_stats_; }
  const DetailedChainStats& getDetailedChainStats() const
  {
    return detailed_chain_stats_;
  }
  void eraseFromGrid(Node* node);
  void paintInGrid(Node* node);
  struct compareNodesX
  {
    // Needs cell centers.
    bool operator()(Node* p, Node* q) const
    {
      return p->getCenterX() < q->getCenterX();
    }
    bool operator()(Node*& s, DbuX i) const { return s->getCenterX() < i; }
    bool operator()(DbuX i, Node*& s) const { return i < s->getCenterX(); }
  };

 private:
  struct compareBlockages
  {
    bool operator()(const Blockage& i1, const Blockage& i2) const
    {
      if (i1.getPaddedXMin() == i2.getPaddedXMin()) {
        return i1.getPaddedXMax() < i2.getPaddedXMax();
      }
      return i1.getPaddedXMin() < i2.getPaddedXMin();
    }
  };

  struct compareIntervals
  {
    bool operator()(std::pair<DbuX, DbuX> i1, std::pair<DbuX, DbuX> i2) const
    {
      if (i1.first == i2.first) {
        return i1.second < i2.second;
      }
      return i1.first < i2.first;
    }
  };

  struct compareNodesL
  {
    bool operator()(Node* p, Node* q) const
    {
      return p->getLeft() < q->getLeft();
    }
  };

  // Different routines for trying moves and swaps.
  bool verifyMove();
  bool tryMove1(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);
  bool tryMove2(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);
  bool tryMove3(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);

  bool trySwap1(Node* ndi, DbuX xi, DbuY yi, int si, DbuX xj, DbuY yj, int sj);

  // Helper routines for making moves and swaps.
  bool checkSiteOrientation(Node* node, DbuX x, DbuY y);
  bool shift(std::vector<Node*>& cells,
             std::vector<DbuX>& targetLeft,
             std::vector<DbuX>& posLeft,
             DbuX leftLimit,
             DbuX rightLimit,
             int segId,
             int rowId);
  bool shiftRightHelper(Node* ndi, DbuX xj, int sj, Node* ndr);
  bool shiftLeftHelper(Node* ndi, DbuX xj, int sj, Node* ndl);

  // For composing list of cells for moves or swaps.
  void clearMoveList();
  bool addToMoveList(Node* ndi,
                     DbuX curLeft,
                     DbuY curBottom,
                     int curSeg,
                     DbuX newLeft,
                     DbuY newBottom,
                     int newSeg);
  bool addToMoveList(Node* ndi,
                     DbuX curLeft,
                     DbuY curBottom,
                     const std::vector<int>& curSegs,
                     DbuX newLeft,
                     DbuY newBottom,
                     const std::vector<int>& newSegs);

  // Standard stuff.
  Architecture* arch_;
  Network* network_;
  Grid* grid_;
  PlacementDRC* drc_engine_;
  Journal journal_;

  // For output.
  utl::Logger* logger_ = nullptr;

  // Info about rows.
  int numSingleHeightRows_;
  DbuY singleRowHeight_;

  // Generic place for utilization.
  double targetUt_{1.0};
  GlobalSwapParams global_swap_params_;
  bool extra_dpl_enabled_ = false;

  // Target displacement limits.
  int maxDispX_;
  int maxDispY_;
  bool disallowOneSiteGaps_{false};
  std::vector<Node*> fixedCells_;  // Fixed; filler, macros, temporary, etc.

  // Blockages and segments.
  std::vector<std::vector<Blockage>> blockages_;
  std::vector<std::vector<Node*>> cellsInSeg_;
  std::vector<std::vector<DetailedSeg*>> segsInRow_;
  std::vector<DetailedSeg*> segments_;
  // size == #nodes
  std::vector<std::vector<DetailedSeg*>> reverseCellToSegs_;

  // Random number generator.
  std::unique_ptr<Placer_RNG> rng_;

  // Info about cells:
  // Single height cells.
  std::vector<Node*> singleHeightCells_;
  // Multi height cells by height.
  std::vector<std::vector<Node*>> multiHeightCells_;
  // Wide movable cells.  Can be single or multi height.
  std::vector<Node*> wideCells_;

  // Original cell positions.
  std::vector<DbuY> origBottom_;
  std::vector<DbuX> origLeft_;

  std::vector<Node*> accepted_nodes_;
  std::vector<Node*> critical_frontier_;
  std::vector<Edge*> touched_edges_;
  std::vector<int> hot_segments_;
  std::vector<uint8_t> accepted_node_mask_;
  std::vector<uint8_t> critical_frontier_mask_;
  std::vector<uint8_t> touched_edge_mask_;
  std::vector<uint8_t> hot_segment_mask_;
  double accepted_delta_{0.0};
  DetailedChainStats detailed_chain_stats_;

  // For generating a move list... (size = moveLimit_)
  int moveLimit_;
};

}  // namespace dpl_evolve
