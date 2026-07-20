// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#pragma once

#include <string>

namespace dpl_evolve {

inline constexpr int kDefaultEvolveThreads = 10;
inline constexpr int kDefaultLegalmIterationLimit = 800;
inline constexpr int kDefaultLegalmCandidateVerticalRadius = 3;
inline constexpr int kDefaultLegalmCandidateHorizontalSteps = 17;
inline constexpr int kDefaultLegalmStage3PartitionSchemes = 3;
inline constexpr int kDefaultLegalmStage3RoundsPerScheme = 10;

struct EvolveContext
{
  // Control-plane values only.  Do not route per-cell/per-net hot data through
  // this context: stage implementations should build in-process contiguous
  // vectors, row/segment indexes, and node-id keyed side arrays from Opendp.
  int max_threads = kDefaultEvolveThreads;
  int max_displacement_x = 0;
  int max_displacement_y = 0;
  // LEGALM Algorithm 1 input T.  The default is Kthre plus the paper's
  // local-optimum detection window so the escape branch can be exercised.
  int legalm_iteration_limit = kDefaultLegalmIterationLimit;
  // CPU execution caps.  The default stencil has (2*3+1)*(2*17+1) = 245
  // candidates, matching the paper's D = 245 BGD candidate set.
  int legalm_candidate_vertical_radius = kDefaultLegalmCandidateVerticalRadius;
  int legalm_candidate_horizontal_steps = kDefaultLegalmCandidateHorizontalSteps;
  int legalm_stage3_partition_schemes
      = kDefaultLegalmStage3PartitionSchemes;
  int legalm_stage3_rounds_per_scheme
      = kDefaultLegalmStage3RoundsPerScheme;
  bool incremental = false;
  std::string report_file_name;
};

}  // namespace dpl_evolve
