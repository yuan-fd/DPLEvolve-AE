// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <numeric>
#include <thread>
#include <utility>
#include <vector>

#include "EvolveContext.h"
#include "dpl_evolve/Opendp.h"
#include "infrastructure/Coordinates.h"
#include "infrastructure/Grid.h"
#include "infrastructure/Objects.h"
#include "infrastructure/network.h"
#include "optimization/detailed_orient.h"
#include "utl/Logger.h"

namespace dpl_evolve {

using utl::DPL;

struct LegalmPaperParams
{
  // LEGALM 2.0 implementation paragraph values.
  int kpart = 50;
  int kely = 2;
  int kthre = 300;
  int kh = 100;
  int local_optimum_window = 500;
  double alpha_sigma = 3.0;
  double alpha_lambda = 0.5;
  double alpha_hf = 0.2;
  double alpha_max = 1.5;
  double ptech = 1.0;
  double delta_threshold_rows = 3.0;
  double xhint_microns = 250.0;
  int yhint_rows = 25;
  int stage3_partition_schemes = 3;
  int stage3_rounds_per_scheme = 10;
};

struct LegalmCpuCaps
{
  // CPU bounded-candidate implementation caps.  These are not paper constants.
  int target_threads = 10;
  int candidate_vertical_radius = 3;
  int candidate_horizontal_steps = 17;
};

struct LegalmFootprint
{
  // Paper subcell footprint for one candidate placement of a cell.  The
  // footprint covers [site, site + width_sites) across height_rows rows.
  int row = 0;
  int site = 0;
  int width_sites = 1;
  int height_rows = 1;
  const Group* group = nullptr;
};

struct LegalmHpwlTerm
{
  int64_t other_min_x = 0;
  int64_t other_max_x = 0;
  int64_t other_min_y = 0;
  int64_t other_max_y = 0;
  int64_t original_hpwl_dbu = 0;
  int offset_x = 0;
  int offset_y = 0;
};

inline int64_t legalmMergedHpwlDbu(const LegalmHpwlTerm& term,
                                   const int64_t pin_x,
                                   const int64_t pin_y)
{
  const int64_t min_x = std::min(term.other_min_x, pin_x);
  const int64_t max_x = std::max(term.other_max_x, pin_x);
  const int64_t min_y = std::min(term.other_min_y, pin_y);
  const int64_t max_y = std::max(term.other_max_y, pin_y);
  return (max_x - min_x) + (max_y - min_y);
}

inline std::vector<LegalmHpwlTerm> legalmBuildHpwlTerms(
    const Node* cell,
    const int skip_nets_larger_than_this = 100)
{
  std::vector<LegalmHpwlTerm> terms;
  if (cell == nullptr) {
    return terms;
  }

  std::vector<const Edge*> seen_edges;
  seen_edges.reserve(cell->getPins().size());
  terms.reserve(cell->getPins().size());
  for (const Pin* cell_pin : cell->getPins()) {
    if (cell_pin == nullptr || cell_pin->getEdge() == nullptr) {
      continue;
    }
    const Edge* edge = cell_pin->getEdge();
    if (edge->getNumPins() <= 1
        || edge->getNumPins() >= skip_nets_larger_than_this
        || std::find(seen_edges.begin(), seen_edges.end(), edge)
               != seen_edges.end()) {
      continue;
    }
    seen_edges.push_back(edge);

    int64_t other_min_x = std::numeric_limits<int64_t>::max();
    int64_t other_max_x = std::numeric_limits<int64_t>::min();
    int64_t other_min_y = std::numeric_limits<int64_t>::max();
    int64_t other_max_y = std::numeric_limits<int64_t>::min();
    bool has_other_pin = false;
    for (const Pin* pin : edge->getPins()) {
      if (pin == nullptr || pin->getNode() == nullptr
          || pin->getNode() == cell) {
        continue;
      }
      const Node* node = pin->getNode();
      const int64_t pin_x
          = static_cast<int64_t>(node->getCenterX().v + pin->getOffsetX().v);
      const int64_t pin_y
          = static_cast<int64_t>(node->getCenterY().v + pin->getOffsetY().v);
      other_min_x = std::min(other_min_x, pin_x);
      other_max_x = std::max(other_max_x, pin_x);
      other_min_y = std::min(other_min_y, pin_y);
      other_max_y = std::max(other_max_y, pin_y);
      has_other_pin = true;
    }
    if (!has_other_pin) {
      continue;
    }

    LegalmHpwlTerm term;
    term.other_min_x = other_min_x;
    term.other_max_x = other_max_x;
    term.other_min_y = other_min_y;
    term.other_max_y = other_max_y;
    term.offset_x = cell_pin->getOffsetX().v;
    term.offset_y = cell_pin->getOffsetY().v;
    const int64_t original_pin_x
        = static_cast<int64_t>(cell->getCenterX().v + term.offset_x);
    const int64_t original_pin_y
        = static_cast<int64_t>(cell->getCenterY().v + term.offset_y);
    term.original_hpwl_dbu
        = legalmMergedHpwlDbu(term, original_pin_x, original_pin_y);
    terms.push_back(term);
  }
  return terms;
}

inline double legalmHpwlRegressionPenaltySites(
    const std::vector<LegalmHpwlTerm>& terms,
    const int64_t candidate_center_x,
    const int64_t candidate_center_y,
    const int site_width_dbu)
{
  if (terms.empty() || site_width_dbu <= 0) {
    return 0.0;
  }

  int64_t positive_delta_dbu = 0;
  for (const LegalmHpwlTerm& term : terms) {
    const int64_t candidate_hpwl
        = legalmMergedHpwlDbu(term,
                              candidate_center_x + term.offset_x,
                              candidate_center_y + term.offset_y);
    if (candidate_hpwl > term.original_hpwl_dbu) {
      positive_delta_dbu += candidate_hpwl - term.original_hpwl_dbu;
    }
  }
  return static_cast<double>(positive_delta_dbu)
         / (static_cast<double>(site_width_dbu)
            * static_cast<double>(std::max<size_t>(1, terms.size())));
}

inline double legalmHpwlDeltaSites(
    const std::vector<LegalmHpwlTerm>& terms,
    const int64_t candidate_center_x,
    const int64_t candidate_center_y,
    const int site_width_dbu)
{
  if (terms.empty() || site_width_dbu <= 0) {
    return 0.0;
  }

  int64_t delta_dbu = 0;
  for (const LegalmHpwlTerm& term : terms) {
    const int64_t candidate_hpwl
        = legalmMergedHpwlDbu(term,
                              candidate_center_x + term.offset_x,
                              candidate_center_y + term.offset_y);
    delta_dbu += candidate_hpwl - term.original_hpwl_dbu;
  }
  return static_cast<double>(delta_dbu)
         / (static_cast<double>(site_width_dbu)
            * static_cast<double>(std::max<size_t>(1, terms.size())));
}

using LegalmCandidateOffset = std::pair<int, int>;

struct LegalmBgdEvaluation
{
  bool feasible = false;
  double cost = std::numeric_limits<double>::infinity();
};

struct LegalmBgdCandidate
{
  int row = -1;
  int site = -1;
  double cost = std::numeric_limits<double>::infinity();
};

inline std::vector<LegalmCandidateOffset> legalmCandidateStencil(
    const LegalmCpuCaps& caps)
{
  std::vector<LegalmCandidateOffset> stencil;
  stencil.reserve((2 * caps.candidate_vertical_radius + 1)
                  * (2 * caps.candidate_horizontal_steps + 1));
  for (int dy = -caps.candidate_vertical_radius;
       dy <= caps.candidate_vertical_radius;
       ++dy) {
    for (int step = -caps.candidate_horizontal_steps;
         step <= caps.candidate_horizontal_steps;
         ++step) {
      stencil.push_back({step, dy});
    }
  }
  return stencil;
}

template <typename EvaluateCandidate>
LegalmBgdCandidate legalmBestBgdCandidate(
    const std::vector<LegalmCandidateOffset>& stencil,
    const int current_row,
    const int current_site,
    const int vertical_step_rows,
    const int width_sites,
    const int height_rows,
    const int row_count,
    const int site_count,
    const double initial_cost,
    EvaluateCandidate&& evaluate_candidate)
{
  LegalmBgdCandidate best;
  best.row = current_row;
  best.site = current_site;
  best.cost = initial_cost;
  for (const auto& [dx, dy] : stencil) {
    const int candidate_site = current_site + dx;
    const int candidate_row = current_row + dy * vertical_step_rows;
    if (candidate_site < 0 || candidate_row < 0
        || candidate_site + width_sites > site_count
        || candidate_row + height_rows > row_count) {
      continue;
    }
    const LegalmBgdEvaluation evaluation
        = evaluate_candidate(candidate_row, candidate_site, best.cost);
    if (evaluation.feasible && evaluation.cost < best.cost) {
      best.row = candidate_row;
      best.site = candidate_site;
      best.cost = evaluation.cost;
    }
  }
  return best;
}

inline constexpr LegalmPaperParams legalmPaperParams()
{
  return {};
}

inline constexpr LegalmCpuCaps legalmCpuCaps()
{
  return {};
}

inline LegalmCpuCaps resolveLegalmCpuCaps(const EvolveContext& context)
{
  LegalmCpuCaps caps = legalmCpuCaps();
  caps.target_threads = std::max(1, context.max_threads);
  caps.candidate_vertical_radius
      = std::max(1, context.legalm_candidate_vertical_radius);
  caps.candidate_horizontal_steps
      = std::max(1, context.legalm_candidate_horizontal_steps);
  return caps;
}

inline int resolveLegalmStage3PartitionSchemes(const EvolveContext& context)
{
  return std::max(1, context.legalm_stage3_partition_schemes);
}

inline int resolveLegalmStage3RoundsPerScheme(const EvolveContext& context)
{
  return std::max(1, context.legalm_stage3_rounds_per_scheme);
}

}  // namespace dpl_evolve
