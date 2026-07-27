// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <chrono>

#include "LegalmCommon.h"

namespace dpl_evolve {

// Top-level student algorithm pipeline composition.
bool Opendp::runStudentAlgorithm(const EvolveContext& context)
{
  clearGuidedInitialLocations();
  (void) context;
  logger_->metric("dpl_evolve__student_algorithm__pipeline_version", 100);
  logger_->info(DPL,
                1220,
                "Running DPL-Evolve default-negotiation seed: negotiation "
                "primary closure.");

  using Clock = std::chrono::steady_clock;
  auto elapsed_ms
      = [](const Clock::time_point begin, const Clock::time_point end) {
          return std::chrono::duration<double, std::milli>(end - begin).count();
        };

  const auto repair_begin = Clock::now();
  const bool negotiation_ok = runEvolveNegotiationRepair(run_abacus_);
  const auto repair_end = Clock::now();
  const double repair_ms = elapsed_ms(repair_begin, repair_end);
  logger_->metric("dpl_evolve__pipeline__policy_repair_used", 1);
  logger_->metric("dpl_evolve__pipeline__negotiation_repair_ms", repair_ms);
  logger_->metric("dpl_evolve__pipeline__negotiation_status",
                  negotiation_ok ? 1 : 0);
  logger_->info(DPL,
                1221,
                "DPL-Evolve default-negotiation seed timing: negotiation "
                "closure {:.2f} ms.",
                repair_ms);
  return negotiation_ok;
}

}  // namespace dpl_evolve
