// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <chrono>

#include "LegalmCommon.h"

namespace dpl_evolve {

// Top-level student algorithm pipeline composition.
bool Opendp::runStudentAlgorithm(const EvolveContext& context)
{
  legalm_stage3_exact_handoff_cells_ = 0;
  legalm_stage3_exact_handoff_target_misses_ = 0;
  clearGuidedInitialLocations();
  logger_->metric("dpl_evolve__student_algorithm__pipeline_version", 3);
  logger_->metric("dpl_evolve__student_algorithm__elite_runtime_guard", 1);
  logger_->info(DPL,
                1220,
                "Running DPL-Evolve pipeline: LEGALM guidance, complete target "
                "handoff, LEGALM legalization.");

  using Clock = std::chrono::steady_clock;
  auto elapsed_ms
      = [](const Clock::time_point begin, const Clock::time_point end) {
          return std::chrono::duration<double, std::milli>(end - begin).count();
        };

  const auto guidance_begin = Clock::now();
  runDifferentialGuidance(context);
  const auto guidance_end = Clock::now();
  const double guidance_ms = elapsed_ms(guidance_begin, guidance_end);
  logger_->metric("dpl_evolve__pipeline__legalm_guidance_ms", guidance_ms);
  reportEvolvePlacementMetrics("legalm_guidance");

  logger_->metric("dpl_evolve__pipeline__overflow_fallback_used", 0);
  logger_->metric("dpl_evolve__pipeline__residual_overflow_handoff_used",
                  legalm_stage2_overflow_free_ ? 0 : 1);
  if (!legalm_stage2_overflow_free_) {
    logger_->info(DPL,
                  1226,
                  "LEGALM guidance kept {} residual overflow sites across "
                  "{} bins; continuing into LEGALM full legalization with the "
                  "best bounded target field instead of Diamond fallback.",
                  legalm_stage2_final_overflow_sites_,
                  legalm_stage2_final_overflow_bins_);
  }

  const auto legalm_begin = Clock::now();
  const bool legalm_ok = runLegalmFullLegalization(context);
  const auto legalm_end = Clock::now();
  const double legalm_ms = elapsed_ms(legalm_begin, legalm_end);
  logger_->metric("dpl_evolve__pipeline__legalm_full_ms", legalm_ms);
  logger_->metric("dpl_evolve__pipeline__legalm_status", legalm_ok ? 1 : 0);
  logger_->metric("dpl_evolve__pipeline__legalm_stage3_exact_handoff_cells",
                  legalm_stage3_exact_handoff_cells_);
  logger_->metric(
      "dpl_evolve__pipeline__legalm_stage3_exact_handoff_target_misses",
      legalm_stage3_exact_handoff_target_misses_);

  if (legalm_ok) {
    logger_->metric("dpl_evolve__pipeline__policy_repair_used", 0);
    logger_->info(DPL,
                  1221,
                  "DPL-Evolve LEGALM-only pipeline timing: guidance {:.2f} ms, "
                  "full legalization {:.2f} ms.",
                  guidance_ms,
                  legalm_ms);
    reportEvolvePlacementMetrics("legalm_only");
    return true;
  }

  if (!use_negotiation_) {
    logger_->metric("dpl_evolve__pipeline__policy_repair_used", 0);
    logger_->warn(DPL,
                  1222,
                  "LEGALM full legalization did not close legality; no "
                  "policy-assisted repair was requested.");
    return false;
  }

  logger_->warn(DPL,
                1223,
                "LEGALM full legalization did not close legality; running "
                "explicit policy-assisted negotiation repair.");
  const auto repair_begin = Clock::now();
  const bool negotiation_ok = runEvolveNegotiationRepair(run_abacus_);
  const auto repair_end = Clock::now();
  const double repair_ms = elapsed_ms(repair_begin, repair_end);
  logger_->metric("dpl_evolve__pipeline__policy_repair_used", 1);
  logger_->metric("dpl_evolve__pipeline__negotiation_repair_ms", repair_ms);
  logger_->metric("dpl_evolve__pipeline__negotiation_status",
                  negotiation_ok ? 1 : 0);
  logger_->info(DPL,
                1224,
                "DPL-Evolve policy-assisted pipeline timing: guidance {:.2f} "
                "ms, full legalization {:.2f} ms, negotiation repair {:.2f} "
                "ms.",
                guidance_ms,
                legalm_ms,
                repair_ms);
  return negotiation_ok;
}

}  // namespace dpl_evolve
