// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#pragma once

#include <string>
#include <utility>
#include <vector>

namespace dpl_evolve {

struct EvolveStageRecord
{
  std::string name;
  std::string status;
  double elapsed_ms = 0.0;
};

class EvolveTelemetry
{
 public:
  void record(std::string name, std::string status, const double elapsed_ms)
  {
    stages_.push_back({std::move(name), std::move(status), elapsed_ms});
  }

  const std::vector<EvolveStageRecord>& stages() const { return stages_; }

 private:
  std::vector<EvolveStageRecord> stages_;
};

}  // namespace dpl_evolve
