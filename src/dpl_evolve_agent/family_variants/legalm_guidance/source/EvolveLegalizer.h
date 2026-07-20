// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#pragma once

#include <functional>
#include <string>
#include <utility>

#include "EvolveContext.h"
#include "EvolveTelemetry.h"

namespace utl {
class Logger;
}

namespace dpl_evolve {

struct EvolveHooks
{
  // Student-owned top-level implementation.  This is a coarse stage boundary,
  // not a per-cell callback surface; hot data stays inside the C++ DPL process.
  std::function<bool(const EvolveContext& context)> run_student_algorithm;
};

class EvolveLegalizer
{
 public:
  EvolveLegalizer(utl::Logger* logger, EvolveHooks hooks);

  void run(const EvolveContext& context);
  const EvolveTelemetry& telemetry() const { return telemetry_; }
  void recordStage(const char* name, const char* status, double elapsed_ms);

 private:
  bool runStudentAlgorithmStage(const EvolveContext& context);

  utl::Logger* logger_ = nullptr;
  EvolveHooks hooks_;
  EvolveTelemetry telemetry_;
};

}  // namespace dpl_evolve
