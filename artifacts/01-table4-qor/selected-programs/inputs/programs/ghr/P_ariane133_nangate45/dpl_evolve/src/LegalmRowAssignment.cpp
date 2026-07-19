// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"

#include <unordered_set>

#include "objective/detailed_hpwl.h"
#include "optimization/detailed_manager.h"
#include "util/utility.h"

namespace dpl_evolve {

namespace {

struct RowAssignCell
{
  Node* cell = nullptr;
  int row = 0;
  int original_site = 0;
  int desired_site = 0;
  int assigned_site = 0;
  int width_sites = 1;
  bool seeded = false;
};

bool calculateEdgeBB(Edge* edge, const Node* seed, odb::Rect& bbox)
{
  DbuX curX;
  DbuY curY;

  bbox.mergeInit();

  int count = 0;
  for (Pin* pin : edge->getPins()) {
    const Node* other = pin->getNode();
    if (other == seed) {
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

bool getMedianRange(Architecture* arch, const Node* node, odb::Rect& node_bbox)
{
  constexpr int kSkipNetsLargerThanThis = 100;
  const DbuX xmin = arch->getMinX();
  const DbuX xmax = arch->getMaxX();
  const DbuY ymin = arch->getMinY();
  const DbuY ymax = arch->getMaxY();

  std::vector<int> xpts;
  std::vector<int> ypts;
  xpts.reserve(node->getNumPins() * 2);
  ypts.reserve(node->getNumPins() * 2);

  unsigned t = 0;
  for (int n = 0; n < node->getNumPins(); n++) {
    Pin* pin = node->getPins()[n];
    Edge* edge = pin->getEdge();

    node_bbox.mergeInit();

    const int numPins = edge->getNumPins();
    if (numPins <= 1 || numPins > kSkipNetsLargerThanThis) {
      continue;
    }
    if (!calculateEdgeBB(edge, node, node_bbox)) {
      continue;
    }

    node_bbox.set_xlo(std::min(
        std::max(xmin.v, node_bbox.xMin() - pin->getOffsetX().v), xmax.v));
    node_bbox.set_xhi(std::max(
        std::min(xmax.v, node_bbox.xMax() - pin->getOffsetX().v), xmin.v));
    node_bbox.set_ylo(std::min(
        std::max(ymin.v, node_bbox.yMin() - pin->getOffsetY().v), ymax.v));
    node_bbox.set_yhi(std::max(
        std::min(ymax.v, node_bbox.yMax() - pin->getOffsetY().v), ymin.v));

    xpts.push_back(node_bbox.xMin());
    xpts.push_back(node_bbox.xMax());
    ypts.push_back(node_bbox.yMin());
    ypts.push_back(node_bbox.yMax());
    ++t;
    ++t;
  }

  if (t <= 1) {
    return false;
  }

  const unsigned mid = t >> 1;
  std::ranges::sort(xpts);
  std::ranges::sort(ypts);
  node_bbox.set_xlo(xpts[mid - 1]);
  node_bbox.set_xhi(xpts[mid]);
  node_bbox.set_ylo(ypts[mid - 1]);
  node_bbox.set_yhi(ypts[mid]);
  return true;
}

}  // namespace

// Bounded row assignment for ALM/BGD guided starts before full legalization.
void Opendp::runRowAssignmentGuidance(const EvolveContext&)
{
  if (network_ == nullptr || grid_ == nullptr
      || guided_initial_valid_.empty()) {
    logger_->metric("dpl_evolve__row_assignment__status", 0);
    return;
  }

  const int row_count = grid_->getRowCount().v;
  const int site_count = grid_->getRowSiteCount().v;
  const DbuX site_width = grid_->getSiteWidth();
  if (row_count <= 0 || site_count <= 0 || site_width.v <= 0) {
    logger_->metric("dpl_evolve__row_assignment__status", 0);
    return;
  }

  std::vector<RowAssignCell> cells;
  cells.reserve(network_->getNumCells());
  std::vector<unsigned char> touched_rows(row_count, 0);
  int skipped_cells = 0;
  int seed_guided_cells = 0;

  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell() || cell->inGroup() || isMultiRow(cell)) {
      ++skipped_cells;
      continue;
    }

    const DbuPt init = initialLocation(cell, false);
    DbuPt desired = init;
    const int id = cell->getId();
    const bool seeded = id >= 0
                        && id < static_cast<int>(guided_initial_valid_.size())
                        && guided_initial_valid_[id] != 0;
    if (seeded) {
      const auto& guided = guided_initial_locations_[id];
      desired = {DbuX{guided.first}, DbuY{guided.second}};
      ++seed_guided_cells;
    }

    const GridPt desired_grid = legalGridPt(cell, desired);
    const GridPt init_grid = legalGridPt(cell, init);
    const int row = std::clamp(desired_grid.y.v, 0, row_count - 1);
    const int desired_site = std::clamp(desired_grid.x.v, 0, site_count - 1);
    const int original_site = std::clamp(init_grid.x.v, 0, site_count - 1);
    const int width_sites = std::max(
        1,
        static_cast<int>(std::ceil(static_cast<double>(cell->getWidth().v)
                                   / static_cast<double>(site_width.v))));
    if (seeded) {
      touched_rows[row] = 1;
    }
    cells.push_back({cell,
                     row,
                     original_site,
                     desired_site,
                     desired_site,
                     width_sites,
                     seeded});
  }

  if (seed_guided_cells == 0) {
    logger_->metric("dpl_evolve__row_assignment__status", 1);
    logger_->metric("dpl_evolve__row_assignment__seed_guided_cells", 0);
    return;
  }

  std::vector<std::vector<int>> row_seed_cells(row_count);
  std::vector<std::vector<int>> row_blocking_cells(row_count);
  for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
    const RowAssignCell& cell = cells[i];
    if (touched_rows[cell.row] == 0) {
      continue;
    }
    if (cell.seeded) {
      row_seed_cells[cell.row].push_back(i);
    } else {
      row_blocking_cells[cell.row].push_back(i);
    }
  }

  int touched_row_count = 0;
  int assigned_seed_cells = 0;
  int changed_cells = 0;
  int max_abs_shift = 0;
  int64_t total_abs_shift = 0;
  int failed_assignments = 0;
  int occupied_sites = 0;
  constexpr int kLocalAssignRadius = 8;

  for (int row_id = 0; row_id < row_count; ++row_id) {
    auto& seed_row = row_seed_cells[row_id];
    if (seed_row.empty()) {
      continue;
    }
    ++touched_row_count;
    std::sort(
        seed_row.begin(), seed_row.end(), [&](const int lhs, const int rhs) {
          const RowAssignCell& a = cells[lhs];
          const RowAssignCell& b = cells[rhs];
          if (a.desired_site != b.desired_site) {
            return a.desired_site < b.desired_site;
          }
          return a.cell->getId() < b.cell->getId();
        });

    std::vector<unsigned char> occupied(site_count, 0);
    auto reserve_sites = [&](const int x, const int width) {
      const int x0 = std::clamp(x, 0, std::max(0, site_count - 1));
      const int x1 = std::clamp(x + width, 0, site_count);
      for (int site = x0; site < x1; ++site) {
        if (occupied[site] == 0) {
          ++occupied_sites;
        }
        occupied[site] = 1;
      }
    };
    auto can_place = [&](const int x, const int width) {
      if (x < 0 || x + width > site_count) {
        return false;
      }
      for (int site = x; site < x + width; ++site) {
        if (occupied[site] != 0) {
          return false;
        }
      }
      return true;
    };

    for (const int idx : row_blocking_cells[row_id]) {
      const RowAssignCell& cell = cells[idx];
      reserve_sites(cell.original_site, cell.width_sites);
    }

    for (const int idx : seed_row) {
      RowAssignCell& cell = cells[idx];
      const int desired = std::clamp(
          cell.desired_site, 0, std::max(0, site_count - cell.width_sites));
      int best_site = -1;
      for (int radius = 0; radius <= kLocalAssignRadius && best_site < 0;
           ++radius) {
        const int left = desired - radius;
        const int right = desired + radius;
        if (can_place(left, cell.width_sites)) {
          best_site = left;
          break;
        }
        if (radius != 0 && can_place(right, cell.width_sites)) {
          best_site = right;
          break;
        }
      }

      if (best_site < 0) {
        // Keep the ALM/BGD target when there is no near free slot.  The
        // downstream repair/legalization stage, if a student explicitly
        // installs one, should resolve local conflicts better than a distant
        // row-assignment jump.
        cell.assigned_site = desired;
        ++failed_assignments;
        continue;
      }

      cell.assigned_site = best_site;
      reserve_sites(cell.assigned_site, cell.width_sites);
      ++assigned_seed_cells;
    }
  }

  guided_initial_locations_.assign(network_->getNumNodes(), {0, 0});
  guided_initial_valid_.assign(network_->getNumNodes(), 0);
  for (const RowAssignCell& cell : cells) {
    if (!cell.seeded) {
      continue;
    }
    const int id = cell.cell->getId();
    if (id < 0 || id >= static_cast<int>(guided_initial_valid_.size())) {
      continue;
    }
    const DbuPt assigned_pt{gridToDbu(GridX{cell.assigned_site}, site_width),
                            grid_->gridYToDbu(GridY{cell.row})};
    const DbuPt snapped = legalPt(cell.cell, assigned_pt);
    guided_initial_locations_[id] = {snapped.x.v, snapped.y.v};
    guided_initial_valid_[id] = 1;
    const int abs_shift = std::abs(cell.assigned_site - cell.original_site);
    if (abs_shift > 0) {
      ++changed_cells;
    }
    max_abs_shift = std::max(max_abs_shift, abs_shift);
    total_abs_shift += abs_shift;
  }

  const double avg_shift = assigned_seed_cells == 0
                               ? 0.0
                               : static_cast<double>(total_abs_shift)
                                     / static_cast<double>(assigned_seed_cells);
  logger_->metric("dpl_evolve__row_assignment__status", 1);
  logger_->metric("dpl_evolve__row_assignment__eligible_cells",
                  static_cast<int>(cells.size()));
  logger_->metric("dpl_evolve__row_assignment__skipped_cells", skipped_cells);
  logger_->metric("dpl_evolve__row_assignment__seed_guided_cells",
                  seed_guided_cells);
  logger_->metric("dpl_evolve__row_assignment__touched_rows",
                  touched_row_count);
  logger_->metric("dpl_evolve__row_assignment__assigned_seed_cells",
                  assigned_seed_cells);
  logger_->metric("dpl_evolve__row_assignment__changed_cells", changed_cells);
  logger_->metric("dpl_evolve__row_assignment__failed_assignments",
                  failed_assignments);
  logger_->metric("dpl_evolve__row_assignment__occupied_sites", occupied_sites);
  logger_->metric("dpl_evolve__row_assignment__avg_shift_sites", avg_shift);
  logger_->metric("dpl_evolve__row_assignment__max_shift_sites", max_abs_shift);
  logger_->info(DPL,
                1213,
                "Row assignment guidance assigned {} ALM seed cells across {} "
                "touched rows from {} seeds (changed {}, failed {}, avg shift "
                "{:.2f} sites).",
                assigned_seed_cells,
                touched_row_count,
                seed_guided_cells,
                changed_cells,
                failed_assignments,
                avg_shift);
}

void Opendp::runCriticalRowMicroStart(DetailedMgr& mgr,
                                      CriticalRowMicroStartStats& stats)
{
  DetailedHPWL hpwl_obj(network_.get());
  hpwl_obj.init(&mgr, nullptr);
  hpwl_obj.curr();

  const auto& critical_frontier = mgr.getCriticalNetFrontierNodes();
  const auto& accepted_nodes = mgr.getAcceptedMoveNodes();
  const std::vector<Node*>& active_frontier
      = critical_frontier.empty() ? accepted_nodes : critical_frontier;
  if (active_frontier.empty()) {
    return;
  }

  const auto& hot_segments = mgr.getHotSegments();
  if (hot_segments.empty()) {
    return;
  }

  std::unordered_set<int> critical_frontier_ids;
  critical_frontier_ids.reserve(critical_frontier.size());
  for (Node* node : critical_frontier) {
    if (node != nullptr) {
      critical_frontier_ids.insert(node->getId());
    }
  }

  std::vector<unsigned char> hot_segment_mask(mgr.getNumSegments(), 0);
  for (const int seg_id : hot_segments) {
    if (seg_id >= 0 && seg_id < mgr.getNumSegments()) {
      hot_segment_mask[seg_id] = 1;
    }
  }
  std::vector<unsigned char> critical_segment_mask(mgr.getNumSegments(), 0);
  for (Node* node : critical_frontier) {
    if (node == nullptr || mgr.getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const int seg_id = mgr.getReverseCellToSegs(node->getId())[0]->getSegId();
    if (seg_id >= 0 && seg_id < mgr.getNumSegments()) {
      critical_segment_mask[seg_id] = 1;
    }
  }

  struct RankedFrontierNode
  {
    Node* node = nullptr;
    int seg_id = -1;
    int row_id = -1;
    int64_t displacement = 0;
  };

  std::vector<RankedFrontierNode> ranked_frontier;
  ranked_frontier.reserve(active_frontier.size());
  std::vector<unsigned char> seen_segments(mgr.getNumSegments(), 0);
  for (Node* node : active_frontier) {
    if (node == nullptr || node->isFixed() || arch_->isMultiHeightCell(node)
        || mgr.getNumReverseCellToSegs(node->getId()) != 1) {
      continue;
    }
    const auto& segs = mgr.getReverseCellToSegs(node->getId());
    const int seg_id = segs[0]->getSegId();
    if (seg_id < 0 || seg_id >= mgr.getNumSegments()
        || hot_segment_mask[seg_id] == 0) {
      continue;
    }
    const int row_id = segs[0]->getRowId();
    const DbuX dx = abs(node->getLeft() - node->getOrigLeft());
    const DbuY dy = abs(node->getBottom() - node->getOrigBottom());
    const int64_t displacement = dx.v + dy.v;
    if (displacement == 0) {
      continue;
    }
    ranked_frontier.push_back({node, seg_id, row_id, displacement});
    if (!seen_segments[seg_id]) {
      seen_segments[seg_id] = 1;
      ++stats.frontier_segments;
    }
  }

  stats.frontier_cells = ranked_frontier.size();
  if (ranked_frontier.empty()) {
    return;
  }

  std::ranges::sort(
      ranked_frontier,
      [](const RankedFrontierNode& lhs, const RankedFrontierNode& rhs) {
        if (lhs.displacement != rhs.displacement) {
          return lhs.displacement > rhs.displacement;
        }
        return lhs.node->getId() < rhs.node->getId();
      });

  constexpr int kMaxSeeds = 48;
  constexpr int kMaxCandidatesPerSeed = 4;
  constexpr int kEarlyScoreCap = 192;
  const std::array<int, 3> row_offsets = {0, -1, 1};

  std::vector<Node*> appended_nodes;
  std::vector<int> appended_segments;
  std::unordered_set<int> appended_node_ids;
  std::unordered_set<int> appended_segment_ids;

  int disp_x = 0;
  int disp_y = 0;
  mgr.getMaxDisplacement(disp_x, disp_y);

  for (int seed_idx = 0;
       seed_idx < std::min(kMaxSeeds, static_cast<int>(ranked_frontier.size()));
       ++seed_idx) {
    if (stats.exact_scored >= kEarlyScoreCap && stats.accepts == 0) {
      stats.early_stopped = true;
      break;
    }

    Node* seed = ranked_frontier[seed_idx].node;
    const int source_seg_id = ranked_frontier[seed_idx].seg_id;
    const int source_row_id = ranked_frontier[seed_idx].row_id;
    if (seed == nullptr) {
      continue;
    }

    odb::Rect bbox;
    if (!getMedianRange(arch_.get(), seed, bbox)) {
      continue;
    }

    ++stats.seeds_selected;
    const int width = seed->getWidth().v;
    const int height = seed->getHeight().v;
    const int current_center_x = seed->getCenterX().v;
    const int current_center_y = seed->getCenterY().v;
    const int old_dist
        = std::max(0, bbox.xMin() - current_center_x)
          + std::max(0, current_center_x - bbox.xMax())
          + std::max(0, bbox.yMin() - current_center_y)
          + std::max(0, current_center_y - bbox.yMax());
    const int projected_left = static_cast<int>(std::floor(
        0.5 * (bbox.xMin() + bbox.xMax()) - 0.5 * width));
    const int left_anchor = bbox.xMin() - (width / 2);
    const int right_anchor = bbox.xMax() - (width / 2);
    const int projected_bottom = static_cast<int>(std::floor(
        0.5 * (bbox.yMin() + bbox.yMax()) - 0.5 * height));
    const int target_row = arch_->find_closest_row(DbuY{projected_bottom});
    const std::array<int, 3> raw_anchors
        = {left_anchor, projected_left, right_anchor};

    struct MoveCandidate
    {
      DbuX target_left{0};
      DbuY target_bottom{0};
      int target_seg_id = -1;
      int improvement = 0;
      int move_distance = 0;
      double risk_penalty = 0.0;
      double adjusted_gain = 0.0;
      bool critical_target = false;
      bool hot_only_target = false;
    };
    std::vector<MoveCandidate> candidates;
    candidates.reserve(12);

    for (const int row_offset : row_offsets) {
      const int row_id = target_row + row_offset;
      if (row_id < 0 || row_id >= arch_->getNumRows()) {
        continue;
      }
      const DbuY row_bottom = arch_->getRow(row_id)->getBottom();
      if (std::abs((row_bottom - seed->getBottom()).v) > disp_y) {
        continue;
      }
      for (int s = 0; s < mgr.getNumSegsInRow(row_id); ++s) {
        DetailedSeg* seg_ptr = mgr.getSegsInRow(row_id)[s];
        const int seg_id = seg_ptr->getSegId();
        if (seg_id < 0 || seg_id >= mgr.getNumSegments()
            || hot_segment_mask[seg_id] == 0
            || seed->getGroupId() != mgr.getSegment(seg_id)->getRegId()) {
          continue;
        }
        for (const int raw_anchor : raw_anchors) {
          DbuX aligned_left{raw_anchor};
          if (!mgr.alignPos(
                  seed, aligned_left, seg_ptr->getMinX(), seg_ptr->getMaxX())) {
            continue;
          }
          const int dx = std::abs((aligned_left - seed->getLeft()).v);
          const int dy = std::abs((row_bottom - seed->getBottom()).v);
          if (dx > disp_x || dy > disp_y) {
            continue;
          }

          const int cand_center_x = aligned_left.v + (width / 2);
          const int cand_center_y = row_bottom.v + (height / 2);
          const int new_dist
              = std::max(0, bbox.xMin() - cand_center_x)
                + std::max(0, cand_center_x - bbox.xMax())
                + std::max(0, bbox.yMin() - cand_center_y)
                + std::max(0, cand_center_y - bbox.yMax());
          const int improvement = old_dist - new_dist;
          if (improvement <= 0 && seg_id == source_seg_id && row_id == source_row_id) {
            continue;
          }
          const bool critical_target
              = seg_id >= 0 && seg_id < mgr.getNumSegments()
                && critical_segment_mask[seg_id] != 0;
          const bool hot_only_target
              = seg_id >= 0 && seg_id < mgr.getNumSegments()
                && hot_segment_mask[seg_id] != 0 && !critical_target;
          const double row_risk
              = (row_id == source_row_id) ? 0.0 : 250.0;
          const double segment_risk = hot_only_target ? 1750.0 : 0.0;
          const double source_escape_bonus
              = (source_seg_id >= 0 && source_seg_id < mgr.getNumSegments()
                 && critical_segment_mask[source_seg_id] == 0
                 && critical_target)
                    ? 300.0
                    : 0.0;
          const double risk_penalty = std::max(0.0, row_risk + segment_risk
                                                        - source_escape_bonus);
          candidates.push_back(
              {aligned_left,
               row_bottom,
               seg_id,
               improvement,
               dx + dy,
               risk_penalty,
               0.0,
               critical_target,
               hot_only_target});
        }
      }
    }

    std::sort(candidates.begin(),
              candidates.end(),
              [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
                if (lhs.target_seg_id != rhs.target_seg_id) {
                  return lhs.target_seg_id < rhs.target_seg_id;
                }
                if (lhs.target_bottom != rhs.target_bottom) {
                  return lhs.target_bottom < rhs.target_bottom;
                }
                return lhs.target_left < rhs.target_left;
              });
    candidates.erase(std::unique(candidates.begin(),
                                 candidates.end(),
                                 [](const MoveCandidate& lhs,
                                    const MoveCandidate& rhs) {
                                   return lhs.target_seg_id == rhs.target_seg_id
                                          && lhs.target_bottom == rhs.target_bottom
                                          && lhs.target_left == rhs.target_left;
                                 }),
                     candidates.end());
    std::ranges::sort(
        candidates,
        [](const MoveCandidate& lhs, const MoveCandidate& rhs) {
          if (lhs.improvement != rhs.improvement) {
            return lhs.improvement > rhs.improvement;
          }
          return lhs.move_distance < rhs.move_distance;
        });
    if (static_cast<int>(candidates.size()) > kMaxCandidatesPerSeed) {
      candidates.resize(kMaxCandidatesPerSeed);
    }

    stats.candidate_moves += candidates.size();

    double best_gain = 0.0;
    double best_adjusted_gain = 0.0;
    double best_risk_penalty = 0.0;
    int best_target_seg_id = -1;
    DbuX best_target_left{0};
    DbuY best_target_bottom{0};
    bool best_critical_target = false;
    bool best_hot_only_target = false;
    double best_critical_gain = 0.0;
    double best_critical_adjusted_gain = 0.0;
    double best_critical_risk_penalty = 0.0;
    int best_critical_target_seg_id = -1;
    DbuX best_critical_target_left{0};
    DbuY best_critical_target_bottom{0};

    for (MoveCandidate& candidate : candidates) {
      ++stats.probes;
      if (!mgr.tryMove(seed,
                       seed->getLeft(),
                       seed->getBottom(),
                       source_seg_id,
                       candidate.target_left,
                       candidate.target_bottom,
                       candidate.target_seg_id,
                       true)) {
        ++stats.failed_moves;
        mgr.rejectMove();
        continue;
      }

      const double gain = hpwl_obj.delta(mgr.getJournal());
      ++stats.exact_scored;
      mgr.rejectMove();
      ++stats.rollbacks;
      candidate.adjusted_gain = gain - candidate.risk_penalty;
      if (candidate.adjusted_gain <= 0.0) {
        ++stats.risk_filtered;
        continue;
      }
      if (candidate.adjusted_gain > best_adjusted_gain
          || (candidate.adjusted_gain == best_adjusted_gain
              && gain > best_gain)) {
        best_gain = gain;
        best_adjusted_gain = candidate.adjusted_gain;
        best_risk_penalty = candidate.risk_penalty;
        best_target_left = candidate.target_left;
        best_target_bottom = candidate.target_bottom;
        best_target_seg_id = candidate.target_seg_id;
        best_critical_target = candidate.critical_target;
        best_hot_only_target = candidate.hot_only_target;
      }
      if (candidate.critical_target
          && (candidate.adjusted_gain > best_critical_adjusted_gain
              || (candidate.adjusted_gain == best_critical_adjusted_gain
                  && gain > best_critical_gain))) {
        best_critical_gain = gain;
        best_critical_adjusted_gain = candidate.adjusted_gain;
        best_critical_risk_penalty = candidate.risk_penalty;
        best_critical_target_left = candidate.target_left;
        best_critical_target_bottom = candidate.target_bottom;
        best_critical_target_seg_id = candidate.target_seg_id;
      }
    }

    constexpr double kCriticalTargetPreferenceSlack = 750.0;
    if (!best_critical_target && best_critical_target_seg_id >= 0
        && best_critical_adjusted_gain > 0.0
        && best_critical_adjusted_gain + kCriticalTargetPreferenceSlack
               >= best_adjusted_gain) {
      best_gain = best_critical_gain;
      best_adjusted_gain = best_critical_adjusted_gain;
      best_risk_penalty = best_critical_risk_penalty;
      best_target_left = best_critical_target_left;
      best_target_bottom = best_critical_target_bottom;
      best_target_seg_id = best_critical_target_seg_id;
      best_critical_target = true;
      best_hot_only_target = false;
    }

    if (best_adjusted_gain <= 0.0 || best_target_seg_id < 0) {
      continue;
    }

    if (!mgr.tryMove(seed,
                     seed->getLeft(),
                     seed->getBottom(),
                     source_seg_id,
                     best_target_left,
                     best_target_bottom,
                     best_target_seg_id,
                     true)) {
      ++stats.failed_moves;
      mgr.rejectMove();
      continue;
    }

    const double accepted_delta = hpwl_obj.delta(mgr.getJournal());
    const double adjusted_delta = accepted_delta - best_risk_penalty;
    if (accepted_delta <= 0.0 || adjusted_delta <= 0.0) {
      mgr.rejectMove();
      ++stats.rollbacks;
      if (accepted_delta > 0.0 && adjusted_delta <= 0.0) {
        ++stats.risk_filtered;
      }
      continue;
    }

    const std::set<Node*>& affected_nodes = mgr.getJournal().getAffectedNodes();
    for (Node* affected : affected_nodes) {
      if (affected == nullptr) {
        continue;
      }
      if (appended_node_ids.insert(affected->getId()).second) {
        appended_nodes.push_back(affected);
      }
      if (mgr.getNumReverseCellToSegs(affected->getId()) == 1) {
        const int affected_seg_id
            = mgr.getReverseCellToSegs(affected->getId())[0]->getSegId();
        if (appended_segment_ids.insert(affected_seg_id).second) {
          appended_segments.push_back(affected_seg_id);
        }
      }
    }

    hpwl_obj.accept();
    mgr.acceptMove();
    ++stats.accepts;
    stats.accepted_gain += accepted_delta;
    stats.adjusted_accepted_gain += adjusted_delta;
    stats.accepted_risk_penalty += best_risk_penalty;
    if (best_critical_target) {
      ++stats.critical_target_accepts;
    } else {
      ++stats.base_target_accepts;
      if (best_hot_only_target) {
        ++stats.hot_only_target_accepts;
      }
    }
  }

  mgr.appendCriticalNetFrontierNodes(appended_nodes);
  mgr.appendHotSegments(appended_segments);
  stats.appended_nodes = appended_nodes.size();
  stats.appended_segments = appended_segments.size();
}

}  // namespace dpl_evolve
