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
