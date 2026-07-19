// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <chrono>

#include "LegalmCommon.h"

namespace dpl_evolve {

// Top-level student algorithm pipeline composition.
bool Opendp::runStudentAlgorithm(const EvolveContext& context)
{
  (void) context;
  using Clock = std::chrono::steady_clock;
  auto elapsed_ms
      = [](const Clock::time_point begin, const Clock::time_point end) {
          return std::chrono::duration<double, std::milli>(end - begin).count();
        };

  clearGuidedInitialLocations();
  logger_->metric("dpl_evolve__student_algorithm__pipeline_version", 2);
  logger_->info(DPL,
                1220,
                "Running DPL-Evolve Diamond pipeline: HPWL-aware bounded "
                "diamond legalization.");

  const auto diamond_begin = Clock::now();
  diamondDPL();
  const auto diamond_end = Clock::now();
  const double diamond_ms = elapsed_ms(diamond_begin, diamond_end);
  logger_->metric("dpl_evolve__pipeline__hpwl_diamond_ms", diamond_ms);
  logger_->metric("dpl_evolve__pipeline__policy_repair_used", 0);
  logger_->info(DPL,
                1221,
                "DPL-Evolve HPWL-aware Diamond legalization timing: {:.2f} ms.",
                diamond_ms);
  reportEvolvePlacementMetrics("hpwl_diamond");
  return true;
}

}  // namespace dpl_evolve
