// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "EvolveLegalizer.h"

#include <chrono>

#include "utl/Logger.h"

namespace dpl_evolve {

using utl::DPL;

namespace {

class StageTimer
{
 public:
  StageTimer(EvolveLegalizer* legalizer, const char* name, const char* status)
      : legalizer_(legalizer),
        name_(name),
        status_(status),
        start_(std::chrono::steady_clock::now())
  {
  }

  ~StageTimer()
  {
    const auto end = std::chrono::steady_clock::now();
    const auto elapsed
        = std::chrono::duration<double, std::milli>(end - start_).count();
    legalizer_->recordStage(name_, status_, elapsed);
  }

 private:
  EvolveLegalizer* legalizer_ = nullptr;
  const char* name_ = nullptr;
  const char* status_ = nullptr;
  std::chrono::steady_clock::time_point start_;
};

}  // namespace

EvolveLegalizer::EvolveLegalizer(utl::Logger* logger, EvolveHooks hooks)
    : logger_(logger), hooks_(std::move(hooks))
{
}

void EvolveLegalizer::recordStage(const char* name,
                                  const char* status,
                                  const double elapsed_ms)
{
  telemetry_.record(name, status, elapsed_ms);
  logger_->info(DPL,
                1201,
                "DPL-Evolve stage {}: {} ({:.2f} ms).",
                name,
                status,
                elapsed_ms);
}

void EvolveLegalizer::run(const EvolveContext& context)
{
  logger_->info(DPL,
                1200,
                "Legalizing using constrained DPL-Evolve framework.");
  logger_->metric("dpl_evolve__framework__entry", 1);

  if (runStudentAlgorithmStage(context)) {
    return;
  }

  logger_->error(DPL,
                 1206,
                 "detailed_placement_evolve framework is prepared, but no "
                 "student algorithm implementation is installed.  Run the "
                 "OpenROAD DPL flow baseline separately, then implement the "
                 "case-specific evolve algorithm in a private variant source.");
}

bool EvolveLegalizer::runStudentAlgorithmStage(const EvolveContext& context)
{
  if (!hooks_.run_student_algorithm) {
    StageTimer timer(this, "student_algorithm", "missing");
    logger_->metric("dpl_evolve__student_algorithm__installed", 0);
    return false;
  }

  StageTimer timer(this, "student_algorithm", "active");
  logger_->metric("dpl_evolve__student_algorithm__installed", 1);
  const bool ok = hooks_.run_student_algorithm(context);
  logger_->metric("dpl_evolve__student_algorithm__success", ok ? 1 : 0);
  return ok;
}

}  // namespace dpl_evolve
