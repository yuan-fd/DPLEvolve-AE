// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <queue>
#include <random>
#include <set>
#include <string>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "PlacementDRC.h"
#include "boost/geometry/geometry.hpp"
#include "boost/random/uniform_int_distribution.hpp"
#include "dpl_evolve/Opendp.h"
#include "graphics/DplObserver.h"
#include "infrastructure/Coordinates.h"
#include "infrastructure/Grid.h"
#include "infrastructure/Objects.h"
#include "infrastructure/Padding.h"
#include "infrastructure/architecture.h"
#include "infrastructure/network.h"
#include "odb/db.h"
#include "odb/dbTransform.h"
#include "odb/geom.h"
#include "optimization/detailed_orient.h"
#include "util/journal.h"
#include "util/symmetry.h"
#include "utl/Logger.h"
// #define ODP_DEBUG

namespace dpl_evolve {
using std::max;
using std::min;
using std::numeric_limits;
using std::string;
using std::vector;

using utl::DPL;

using utl::format_as;  // NOLINT(misc-unused-using-decls)

std::string Opendp::printBgBox(
    const boost::geometry::model::box<bgPoint>& queryBox)
{
  return fmt::format("({0}, {1}) - ({2}, {3})",
                     queryBox.min_corner().x(),
                     queryBox.min_corner().y(),
                     queryBox.max_corner().x(),
                     queryBox.max_corner().y());
}

struct Opendp::DiamondSourceState
{
  struct DenseFragmentMetric
  {
    int fragmentation_dbu = 0;
    int free_gap_dbu = 0;
    int edge_rows = 0;
    int split_rows = 0;
  };

  struct DenseEdgeSignature
  {
    bool left_open = false;
    bool right_open = false;

    bool isSplit() const { return left_open && right_open; }
    bool isEdge() const { return !left_open || !right_open; }
  };

  struct DenseGapShape
  {
    int left_gap_dbu = 0;
    int right_gap_dbu = 0;
    int min_gap_dbu = 0;
    int max_gap_dbu = 0;

    bool isSplit() const { return left_gap_dbu > 0 && right_gap_dbu > 0; }
    bool isEdge() const { return left_gap_dbu == 0 || right_gap_dbu == 0; }
  };

  struct FamilyBandTelemetry
  {
    std::array<std::array<int, kDiamondBandCount>, kDiamondBandCount> chosen{};
    std::array<std::array<int, kDiamondBandCount>, kDiamondBandCount>
        committed{};
  };

  struct ComparatorTelemetry
  {
    int bfs_scored = 0;
    int bfs_compared = 0;
    int bfs_hpwl_replaced = 0;
    int bfs_band_replaced = 0;
    uint64_t bfs_hpwl_before = 0;
    uint64_t bfs_hpwl_after = 0;
    int span_trial_legal = 0;
    int span_trial_replaced = 0;
  };

  struct DenseCandidateInfo
  {
    bool active = false;
    bool pressure_selected = false;
    bool pressure_over_hpwl = false;
    bool rank_selected = false;
    bool shape_selected = false;
    int candidate_grid_x = 0;
    int candidate_grid_y = 0;
    uint64_t source_hpwl = 0;
    uint64_t candidate_hpwl = 0;
    int source_pressure = 0;
    int candidate_pressure = 0;
    int source_distance = 0;
    int candidate_distance = 0;
    int source_fragmentation = 0;
    int candidate_fragmentation = 0;
    int source_free_gap = 0;
    int candidate_free_gap = 0;
    int source_edge_rows = 0;
    int candidate_edge_rows = 0;
    int source_split_rows = 0;
    int candidate_split_rows = 0;
    int source_minor_gap = 0;
    int candidate_minor_gap = 0;
    int source_major_gap = 0;
    int candidate_major_gap = 0;
    int cell_width_sites = 0;
    int cell_height_rows = 0;
  };

  struct DenseRowGapContext
  {
    bool valid = false;
    std::vector<uint8_t> left_open_sites;
    std::vector<uint8_t> right_open_sites;
  };

  struct DenseTelemetry
  {
    int eligible_cells = 0;
    int anchor_ready_cells = 0;
    int window_gate_cells = 0;
    int gain_gate_cells = 0;
    int legal_probe_cells = 0;
    int fixed_probe_candidates = 0;
    int interval_probe_cells = 0;
    int interval_probe_candidates = 0;
    int interval_rows = 0;
    int interval_site_checks = 0;
    int pressure_guard_rejects = 0;
    int rank_scored = 0;
    int rank_compared = 0;
    int rank_selected = 0;
    int rank_returned = 0;
    int rank_committed = 0;
    int rank_replaced = 0;
    int rank_pattern_replaced = 0;
    int rank_pattern_exact_evaluated = 0;
    int rank_pattern_reject_width = 0;
    int rank_pattern_reject_source = 0;
    int rank_pattern_reject_candidate = 0;
    int rank_pattern_reject_gap = 0;
    int rank_pattern_reject_exact = 0;
    int rank_shape_evaluated = 0;
    int rank_shape_selected = 0;
    int rank_shape_committed = 0;
    int rank_shape_reject_hpwl = 0;
    int rank_shape_reject_gap = 0;
    int rank_fragment_hits = 0;
    int rank_fragment_misses = 0;
    int rank_fragment_avoided_band = 0;
    int rank_fragment_avoided_guard = 0;
    int row_context_uses = 0;
    int row_context_builds = 0;
    uint64_t rank_hpwl_before = 0;
    uint64_t rank_hpwl_after = 0;
    int64_t rank_fragment_before = 0;
    int64_t rank_fragment_after = 0;
    int64_t rank_free_gap_before = 0;
    int64_t rank_free_gap_after = 0;
    int64_t rank_edge_rows_before = 0;
    int64_t rank_edge_rows_after = 0;
    int64_t rank_split_rows_before = 0;
    int64_t rank_split_rows_after = 0;
    int64_t rank_minor_gap_before = 0;
    int64_t rank_minor_gap_after = 0;
    int64_t rank_major_gap_before = 0;
    int64_t rank_major_gap_after = 0;
    int64_t rank_pressure_before = 0;
    int64_t rank_pressure_after = 0;
    int64_t rank_distance_before = 0;
    int64_t rank_distance_after = 0;
    int rank_width_selected_small = 0;
    int rank_width_selected_medium = 0;
    int rank_width_selected_large = 0;
    int rank_row_selected_single = 0;
    int rank_row_selected_multi = 0;
    int active_cells = 0;
    int hpwl_selected = 0;
    int hpwl_returned = 0;
    int hpwl_committed = 0;
    int pressure_selected = 0;
    int pressure_returned = 0;
    int pressure_committed = 0;
    int pressure_over_hpwl_selected = 0;
    int pressure_over_hpwl_returned = 0;
    int pressure_over_hpwl_committed = 0;
    int pressure_over_hpwl_rejected = 0;
    uint64_t selected_hpwl_before = 0;
    uint64_t selected_hpwl_after = 0;
    int64_t selected_pressure_before = 0;
    int64_t selected_pressure_after = 0;
    int64_t selected_distance_before = 0;
    int64_t selected_distance_after = 0;
  };

  struct OrderTelemetry
  {
    bool active = false;
    int queue_bucket_size = 0;
    int active_earlier_buckets = 0;
    int active_later_buckets = 0;
    int candidate_cells = 0;
    int critical_candidates = 0;
    int critical_selected = 0;
    int critical_rejected_bucket = 0;
    int critical_rejected_quota = 0;
    int earlier_pool = 0;
    int earlier_selected = 0;
    int later_pool = 0;
    int later_selected = 0;
    int moved_earlier_cells = 0;
    int moved_later_cells = 0;
    int earlier_capped = 0;
    int later_capped = 0;
    int gain_pool_low = 0;
    int gain_pool_mid = 0;
    int gain_pool_high = 0;
    int gain_selected_low = 0;
    int gain_selected_mid = 0;
    int gain_selected_high = 0;
    int shift_selected_small = 0;
    int shift_selected_mid = 0;
    int shift_selected_large = 0;
    int64_t earlier_slot_shift = 0;
    int64_t later_slot_shift = 0;
    int64_t max_bucket_span = 0;
    int64_t bucket_span_sum = 0;
    int64_t max_earlier_slot_shift = 0;
    int64_t max_later_slot_shift = 0;
    int64_t earlier_pressure = 0;
    int64_t later_pressure = 0;
    int64_t earlier_critical_edges = 0;
    int64_t later_critical_edges = 0;
    uint64_t earlier_hpwl_before = 0;
    uint64_t earlier_hpwl_after = 0;
    uint64_t later_hpwl_before = 0;
    uint64_t later_hpwl_after = 0;
  };

  DiamondSourceCandidateInfo last_source_candidate;
  DenseCandidateInfo last_dense_candidate;
  std::array<DiamondSourceRuntime, kDiamondSourceFamilyCount> runtime{};
  std::array<FamilyBandTelemetry, kDiamondSourceFamilyCount> band_telemetry{};
  ComparatorTelemetry comparator{};
  DenseTelemetry dense{};
  OrderTelemetry order{};
  std::vector<DenseRowGapContext> dense_row_gap_contexts;
};

namespace {

constexpr int kDenseRowGapCapSites = 24;

template <typename State>
void invalidateDenseRowGapRange(State& state,
                                const int row_begin,
                                const int row_end)
{
  if (state.dense_row_gap_contexts.empty()) {
    return;
  }
  const int row_count = static_cast<int>(state.dense_row_gap_contexts.size());
  const int clamped_begin = std::max(0, row_begin);
  const int clamped_end = std::min(row_count, row_end);
  for (int row = clamped_begin; row < clamped_end; ++row) {
    state.dense_row_gap_contexts[row].valid = false;
  }
}

int denseWidthBucketIndex(const int width_sites)
{
  if (width_sites <= 2) {
    return 0;
  }
  if (width_sites <= 4) {
    return 1;
  }
  return 2;
}

template <typename DenseTelemetry, typename DenseCandidateInfo>
void recordDenseRankPattern(DenseTelemetry& dense,
                            const DenseCandidateInfo& candidate)
{
  switch (denseWidthBucketIndex(candidate.cell_width_sites)) {
    case 0:
      ++dense.rank_width_selected_small;
      break;
    case 1:
      ++dense.rank_width_selected_medium;
      break;
    default:
      ++dense.rank_width_selected_large;
      break;
  }
  if (candidate.cell_height_rows <= 1) {
    ++dense.rank_row_selected_single;
  } else {
    ++dense.rank_row_selected_multi;
  }
}

template <typename State>
void recordDenseCandidateCommit(State& state)
{
  auto& dense_candidate = state.last_dense_candidate;
  if (!dense_candidate.active) {
    return;
  }

  auto& dense = state.dense;
  if (dense_candidate.rank_selected) {
    ++dense.rank_committed;
    if (dense_candidate.shape_selected) {
      ++dense.rank_shape_committed;
    }
    dense.rank_hpwl_before += dense_candidate.source_hpwl;
    dense.rank_hpwl_after += dense_candidate.candidate_hpwl;
    dense.rank_fragment_before += dense_candidate.source_fragmentation;
    dense.rank_fragment_after += dense_candidate.candidate_fragmentation;
    dense.rank_free_gap_before += dense_candidate.source_free_gap;
    dense.rank_free_gap_after += dense_candidate.candidate_free_gap;
    dense.rank_edge_rows_before += dense_candidate.source_edge_rows;
    dense.rank_edge_rows_after += dense_candidate.candidate_edge_rows;
    dense.rank_split_rows_before += dense_candidate.source_split_rows;
    dense.rank_split_rows_after += dense_candidate.candidate_split_rows;
    dense.rank_minor_gap_before += dense_candidate.source_minor_gap;
    dense.rank_minor_gap_after += dense_candidate.candidate_minor_gap;
    dense.rank_major_gap_before += dense_candidate.source_major_gap;
    dense.rank_major_gap_after += dense_candidate.candidate_major_gap;
    dense.rank_pressure_before += dense_candidate.source_pressure;
    dense.rank_pressure_after += dense_candidate.candidate_pressure;
    dense.rank_distance_before += dense_candidate.source_distance;
    dense.rank_distance_after += dense_candidate.candidate_distance;
    recordDenseRankPattern(dense, dense_candidate);
  } else if (dense_candidate.pressure_selected) {
    ++dense.pressure_committed;
    if (dense_candidate.pressure_over_hpwl) {
      ++dense.pressure_over_hpwl_committed;
    }
    dense.selected_hpwl_before += dense_candidate.source_hpwl;
    dense.selected_hpwl_after += dense_candidate.candidate_hpwl;
    dense.selected_pressure_before += dense_candidate.source_pressure;
    dense.selected_pressure_after += dense_candidate.candidate_pressure;
    dense.selected_distance_before += dense_candidate.source_distance;
    dense.selected_distance_after += dense_candidate.candidate_distance;
  } else {
    ++dense.hpwl_committed;
    dense.selected_hpwl_before += dense_candidate.source_hpwl;
    dense.selected_hpwl_after += dense_candidate.candidate_hpwl;
    dense.selected_pressure_before += dense_candidate.source_pressure;
    dense.selected_pressure_after += dense_candidate.candidate_pressure;
    dense.selected_distance_before += dense_candidate.source_distance;
    dense.selected_distance_after += dense_candidate.candidate_distance;
  }
  dense_candidate = {};
}

template <typename State>
void recordDenseCandidateReject(State& state)
{
  auto& dense_candidate = state.last_dense_candidate;
  if (dense_candidate.active && !dense_candidate.rank_selected
      && dense_candidate.pressure_over_hpwl) {
    ++state.dense.pressure_over_hpwl_rejected;
  }
  dense_candidate = {};
}

}  // namespace

Opendp::DiamondSourceState& Opendp::diamondSourceState()
{
  static std::unordered_map<const Opendp*, DiamondSourceState> states;
  return states[this];
}

const Opendp::DiamondSourceState& Opendp::diamondSourceState() const
{
  return const_cast<Opendp*>(this)->diamondSourceState();
}

Opendp::DiamondSourceCandidateInfo& Opendp::diamondLastSourceCandidate()
{
  return diamondSourceState().last_source_candidate;
}

const Opendp::DiamondSourceCandidateInfo& Opendp::diamondLastSourceCandidate()
    const
{
  return diamondSourceState().last_source_candidate;
}

std::array<Opendp::DiamondSourceRuntime, Opendp::kDiamondSourceFamilyCount>&
Opendp::diamondSourceRuntime()
{
  return diamondSourceState().runtime;
}

const std::array<Opendp::DiamondSourceRuntime,
                 Opendp::kDiamondSourceFamilyCount>&
Opendp::diamondSourceRuntime() const
{
  return diamondSourceState().runtime;
}

void Opendp::resetDiamondSourceRuntime()
{
  DiamondSourceState& state = diamondSourceState();
  state = {};
  state.dense_row_gap_contexts.resize(grid_ ? grid_->getRowCount().v : 0);
  DiamondSourceRuntime& endpoint
      = state.runtime[static_cast<int>(DiamondSourceFamily::kEndpoint)];
  endpoint.enabled = diamond_design_utilization_ < 80.0;
  DiamondSourceRuntime& span
      = state.runtime[static_cast<int>(DiamondSourceFamily::kSpanContraction)];
  span.trial_budget
      = diamond_design_utilization_ >= 80.0 && diamond_design_utilization_ < 90.0
            ? 2
            : 0;
}

bool Opendp::isDiamondSourceFamilyEnabled(const DiamondSourceFamily family) const
{
  const DiamondSourceRuntime& runtime
      = diamondSourceRuntime()[static_cast<int>(family)];
  return runtime.enabled || runtime.trial_used < runtime.trial_budget;
}

void Opendp::recordDiamondSourceSelection(
    const DiamondSourceCandidateInfo& info)
{
  if (!info.active) {
    return;
  }
  const int family_idx = static_cast<int>(info.family);
  DiamondSourceState& state = diamondSourceState();
  DiamondSourceRuntime& runtime = state.runtime[family_idx];
  if (!runtime.enabled && runtime.trial_used < runtime.trial_budget) {
    ++runtime.trial_used;
  }
  ++runtime.chosen;
  ++runtime.chosen_gain_bands[static_cast<int>(info.gain_band)];
  ++runtime.chosen_distance_bands[static_cast<int>(info.distance_band)];
  ++state.band_telemetry[family_idx]
         .chosen[static_cast<int>(info.gain_band)]
                [static_cast<int>(info.distance_band)];
}

void Opendp::updateDiamondSourceFamilyState(const DiamondSourceFamily family)
{
  DiamondSourceRuntime& runtime
      = diamondSourceRuntime()[static_cast<int>(family)];
  const int medium_or_strong_committed
      = runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kMedium)]
        + runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kStrong)];
  const int far_committed
      = runtime.committed_distance_bands[static_cast<int>(DiamondDistanceBand::kFar)];
  const bool positive_gain
      = runtime.committed_hpwl_after < runtime.committed_hpwl_before;
  if (!runtime.enabled && runtime.trial_budget > 0) {
    const bool strong_trial_evidence
        = runtime.committed >= runtime.trial_budget
          && runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kWeak)]
                 == 0
          && runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kStrong)]
                 >= runtime.trial_budget
          && medium_or_strong_committed == runtime.committed
          && runtime.rejected == 0 && positive_gain && far_committed == 0;
    if (strong_trial_evidence) {
      runtime.enabled = true;
      runtime.positive_evidence = true;
      runtime.disable_reason = DiamondDisableReason::kNone;
      return;
    }
    if (runtime.trial_used < runtime.trial_budget) {
      return;
    }
    runtime.enabled = false;
    runtime.positive_evidence = false;
    runtime.disable_reason = runtime.committed == 0
                                 ? DiamondDisableReason::kNoCommit
                                 : DiamondDisableReason::kWeakGain;
    return;
  }
  if (!runtime.enabled) {
    return;
  }
  const bool sufficient_positive_commits
      = family == DiamondSourceFamily::kSpanContraction
            ? runtime.committed_gain_bands[static_cast<int>(
                  DiamondGainBand::kStrong)]
                      > 0
                  || medium_or_strong_committed >= 2
            : medium_or_strong_committed > 0;
  if (runtime.committed > 0 && sufficient_positive_commits
      && positive_gain && far_committed * 2 <= runtime.committed) {
    runtime.positive_evidence = true;
    return;
  }

  const int disable_probe_limit = family == DiamondSourceFamily::kEndpoint ? 6 : 2;
  if (runtime.chosen < disable_probe_limit) {
    return;
  }

  runtime.enabled = false;
  runtime.positive_evidence = false;
  if (runtime.committed == 0) {
    runtime.disable_reason = DiamondDisableReason::kNoCommit;
  } else if (medium_or_strong_committed == 0 || !positive_gain) {
    runtime.disable_reason = DiamondDisableReason::kWeakGain;
  } else {
    runtime.disable_reason = DiamondDisableReason::kPoorTradeoff;
  }
}

void Opendp::recordDiamondSourceOutcome(const DiamondSourceCandidateInfo& info,
                                        const bool committed)
{
  if (!info.active) {
    return;
  }
  const int family_idx = static_cast<int>(info.family);
  DiamondSourceState& state = diamondSourceState();
  DiamondSourceRuntime& runtime = state.runtime[family_idx];
  if (committed) {
    ++runtime.committed;
    runtime.committed_hpwl_before += info.reference_hpwl;
    runtime.committed_hpwl_after += info.candidate_hpwl;
    ++runtime.committed_gain_bands[static_cast<int>(info.gain_band)];
    ++runtime.committed_distance_bands[static_cast<int>(info.distance_band)];
    ++state.band_telemetry[family_idx]
           .committed[static_cast<int>(info.gain_band)]
                     [static_cast<int>(info.distance_band)];
  } else {
    ++runtime.rejected;
  }
  updateDiamondSourceFamilyState(info.family);
}

void Opendp::reportDiamondSourceTelemetry() const
{
  const auto family_name = [](const DiamondSourceFamily family) {
    switch (family) {
      case DiamondSourceFamily::kEndpoint:
        return "endpoint";
      case DiamondSourceFamily::kSpanContraction:
        return "span";
    }
    return "unknown";
  };
  const auto disable_reason = [](const DiamondDisableReason reason) {
    switch (reason) {
      case DiamondDisableReason::kNone:
        return "none";
      case DiamondDisableReason::kNoCommit:
        return "no_commit";
      case DiamondDisableReason::kWeakGain:
        return "weak_gain";
      case DiamondDisableReason::kPoorTradeoff:
        return "poor_tradeoff";
    }
    return "unknown";
  };

  for (int family_idx = 0; family_idx < kDiamondSourceFamilyCount; ++family_idx) {
    const DiamondSourceFamily family
        = static_cast<DiamondSourceFamily>(family_idx);
    const DiamondSourceRuntime& runtime = diamondSourceRuntime()[family_idx];
    const auto& band_telemetry = diamondSourceState().band_telemetry[family_idx];
    const int chosen_far_weak_medium
        = band_telemetry.chosen[static_cast<int>(DiamondGainBand::kWeak)]
                           [static_cast<int>(DiamondDistanceBand::kFar)]
          + band_telemetry.chosen[static_cast<int>(DiamondGainBand::kMedium)]
                               [static_cast<int>(DiamondDistanceBand::kFar)];
    const int committed_far_weak_medium
        = band_telemetry.committed[static_cast<int>(DiamondGainBand::kWeak)]
                               [static_cast<int>(DiamondDistanceBand::kFar)]
          + band_telemetry.committed[static_cast<int>(DiamondGainBand::kMedium)]
                                    [static_cast<int>(DiamondDistanceBand::kFar)];
    const int chosen_far_strong
        = band_telemetry.chosen[static_cast<int>(DiamondGainBand::kStrong)]
                              [static_cast<int>(DiamondDistanceBand::kFar)];
    const int committed_far_strong
        = band_telemetry.committed[static_cast<int>(DiamondGainBand::kStrong)]
                                 [static_cast<int>(DiamondDistanceBand::kFar)];
    logger_->info(
        DPL,
        1226 + family_idx,
        "DPL-Evolve Diamond source telemetry {}: enabled {}, positive {}, "
        "disable {}, trial {}/{}, chosen {}, committed {}, rejected {}, HPWL {} -> {}, "
        "far weak/medium chosen {}, committed {}, far strong chosen {}, committed {}, "
        "gain bands chosen {}/{}/{}, committed {}/{}/{}, distance bands "
        "chosen {}/{}/{}, committed {}/{}/{}.",
        family_name(family),
        runtime.enabled,
        runtime.positive_evidence,
        disable_reason(runtime.disable_reason),
        runtime.trial_used,
        runtime.trial_budget,
        runtime.chosen,
        runtime.committed,
        runtime.rejected,
        runtime.committed_hpwl_before,
        runtime.committed_hpwl_after,
        chosen_far_weak_medium,
        committed_far_weak_medium,
        chosen_far_strong,
        committed_far_strong,
        runtime.chosen_gain_bands[static_cast<int>(DiamondGainBand::kWeak)],
        runtime.chosen_gain_bands[static_cast<int>(DiamondGainBand::kMedium)],
        runtime.chosen_gain_bands[static_cast<int>(DiamondGainBand::kStrong)],
        runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kWeak)],
        runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kMedium)],
        runtime.committed_gain_bands[static_cast<int>(DiamondGainBand::kStrong)],
        runtime.chosen_distance_bands[static_cast<int>(
            DiamondDistanceBand::kNear)],
        runtime.chosen_distance_bands[static_cast<int>(DiamondDistanceBand::kMid)],
        runtime.chosen_distance_bands[static_cast<int>(DiamondDistanceBand::kFar)],
        runtime.committed_distance_bands[static_cast<int>(
            DiamondDistanceBand::kNear)],
        runtime.committed_distance_bands[static_cast<int>(
            DiamondDistanceBand::kMid)],
        runtime.committed_distance_bands[static_cast<int>(
            DiamondDistanceBand::kFar)]);
  }

  const auto& comparator = diamondSourceState().comparator;
  logger_->info(
      DPL,
      1230,
      "DPL-Evolve Diamond comparator telemetry: active {}, bfs scored {}, "
      "compared {}, hpwl replacements {}, band replacements {}, HPWL {} -> {}, "
      "span trial legal {}, span trial replacements {}.",
      comparator.bfs_scored > 0 || comparator.span_trial_legal > 0,
      comparator.bfs_scored,
      comparator.bfs_compared,
      comparator.bfs_hpwl_replaced,
      comparator.bfs_band_replaced,
      comparator.bfs_hpwl_before,
      comparator.bfs_hpwl_after,
      comparator.span_trial_legal,
      comparator.span_trial_replaced);

  const auto& dense = diamondSourceState().dense;
  logger_->info(
      DPL,
      1231,
      "DPL-Evolve Diamond dense telemetry: active cells {}, HPWL "
      "selected/returned/committed {}/{}/{}, pressure "
      "selected/returned/committed {}/{}/{}, pressure-over-HPWL "
      "selected/returned/committed/rejected {}/{}/{}/{}, local HPWL {} -> {}, "
      "pressure {} -> {}, displacement {} -> {}.",
      dense.active_cells,
      dense.hpwl_selected,
      dense.hpwl_returned,
      dense.hpwl_committed,
      dense.pressure_selected,
      dense.pressure_returned,
      dense.pressure_committed,
      dense.pressure_over_hpwl_selected,
      dense.pressure_over_hpwl_returned,
      dense.pressure_over_hpwl_committed,
      dense.pressure_over_hpwl_rejected,
      dense.selected_hpwl_before,
      dense.selected_hpwl_after,
      dense.selected_pressure_before,
      dense.selected_pressure_after,
      dense.selected_distance_before,
      dense.selected_distance_after);

  logger_->info(
      DPL,
      1234,
      "DPL-Evolve Diamond dense trace: eligible/anchor/window/gain/legal "
      "{}/{}/{}/{}/{}, fixed legal {}, interval cells/legal/rows/checks "
      "{}/{}/{}/{}, selected/rejected {} / {}, rank "
      "scored/compared/selected/returned/committed/replaced/pattern "
      "{}/{}/{}/{}/{}/{}/{}, pattern exact/reject width/source/candidate/gap/exact "
      "{}/{}/{}/{}/{}/{}, shape eval/selected/committed/reject hpwl/gap "
      "{}/{}/{}/{}/{}, frag cache hit/miss/skip-band/skip-guard "
      "{}/{}/{}/{}, row ctx use/build {}/{}, HPWL {} -> {}, frag {} -> {}, "
      "gap {} -> {}, edge rows {} -> {}, split rows {} -> {}, minor gap {} -> "
      "{}, major gap {} -> {}, pressure {} -> {}, distance {} -> {}, width sel "
      "{}/{}/{}, row sel {}/{}.",
      dense.eligible_cells,
      dense.anchor_ready_cells,
      dense.window_gate_cells,
      dense.gain_gate_cells,
      dense.legal_probe_cells,
      dense.fixed_probe_candidates,
      dense.interval_probe_cells,
      dense.interval_probe_candidates,
      dense.interval_rows,
      dense.interval_site_checks,
      dense.hpwl_selected + dense.pressure_selected,
      dense.pressure_guard_rejects,
      dense.rank_scored,
      dense.rank_compared,
      dense.rank_selected,
      dense.rank_returned,
      dense.rank_committed,
      dense.rank_replaced,
      dense.rank_pattern_replaced,
      dense.rank_pattern_exact_evaluated,
      dense.rank_pattern_reject_width,
      dense.rank_pattern_reject_source,
      dense.rank_pattern_reject_candidate,
      dense.rank_pattern_reject_gap,
      dense.rank_pattern_reject_exact,
      dense.rank_shape_evaluated,
      dense.rank_shape_selected,
      dense.rank_shape_committed,
      dense.rank_shape_reject_hpwl,
      dense.rank_shape_reject_gap,
      dense.rank_fragment_hits,
      dense.rank_fragment_misses,
      dense.rank_fragment_avoided_band,
      dense.rank_fragment_avoided_guard,
      dense.row_context_uses,
      dense.row_context_builds,
      dense.rank_hpwl_before,
      dense.rank_hpwl_after,
      dense.rank_fragment_before,
      dense.rank_fragment_after,
      dense.rank_free_gap_before,
      dense.rank_free_gap_after,
      dense.rank_edge_rows_before,
      dense.rank_edge_rows_after,
      dense.rank_split_rows_before,
      dense.rank_split_rows_after,
      dense.rank_minor_gap_before,
      dense.rank_minor_gap_after,
      dense.rank_major_gap_before,
      dense.rank_major_gap_after,
      dense.rank_pressure_before,
      dense.rank_pressure_after,
      dense.rank_distance_before,
      dense.rank_distance_after,
      dense.rank_width_selected_small,
      dense.rank_width_selected_medium,
      dense.rank_width_selected_large,
      dense.rank_row_selected_single,
      dense.rank_row_selected_multi);

  const auto& order = diamondSourceState().order;
  logger_->info(
      DPL,
      1232,
      "DPL-Evolve Diamond order telemetry: active {}, candidates {}, "
      "queue bucket {}, active buckets {}/{}, bucket span avg/max {}/{}, earlier "
      "pool/selected/moved/capped {}/{}/{}/{}, later "
      "pool/selected/moved/capped {}/{}/{}/{}, slot shift sum/max {}/{} / "
      "{}/{}, pressure {}/{}, critical nets {}/{}, HPWL {} -> {} / {} -> {}.",
      order.active,
      order.candidate_cells,
      order.queue_bucket_size,
      order.active_earlier_buckets,
      order.active_later_buckets,
      order.earlier_selected + order.later_selected == 0
          ? 0
          : order.bucket_span_sum
                / (order.earlier_selected + order.later_selected),
      order.max_bucket_span,
      order.earlier_pool,
      order.earlier_selected,
      order.moved_earlier_cells,
      order.earlier_capped,
      order.later_pool,
      order.later_selected,
      order.moved_later_cells,
      order.later_capped,
      order.earlier_slot_shift,
      order.max_earlier_slot_shift,
      order.later_slot_shift,
      order.max_later_slot_shift,
      order.earlier_pressure,
      order.later_pressure,
      order.earlier_critical_edges,
      order.later_critical_edges,
      order.earlier_hpwl_before,
      order.earlier_hpwl_after,
      order.later_hpwl_before,
      order.later_hpwl_after);

  logger_->info(
      DPL,
      1233,
      "DPL-Evolve Diamond order trace: critical candidates/selected/rejected "
      "{}/{}/{}/{}, gain bands pool low/mid/high {}/{}/{}, selected {}/{}/{}, "
      "shift bands selected small/mid/large {}/{}/{}.",
      order.critical_candidates,
      order.critical_selected,
      order.critical_rejected_bucket,
      order.critical_rejected_quota,
      order.gain_pool_low,
      order.gain_pool_mid,
      order.gain_pool_high,
      order.gain_selected_low,
      order.gain_selected_mid,
      order.gain_selected_high,
      order.shift_selected_small,
      order.shift_selected_mid,
      order.shift_selected_large);
}

void Opendp::diamondDPL()
{
  if (debug_observer_) {
    debug_observer_->startPlacement(block_);
  }

  placement_failures_.clear();
  initGrid();
  // Paint fixed cells.
  setFixedGridCells();
  // Paint initially place2d cells (respecting already legalized ones).
  if (incremental_) {
    logger_->report("setInitialGridCells()");
    setInitialGridCells();
  }
  // group mapping & x_axis dummycell insertion
  groupInitPixels2();
  // y axis dummycell insertion
  groupInitPixels();

  if (!arch_->getRegions().empty()) {
    placeGroups();
  }

  const int64_t core_area
      = static_cast<int64_t>(core_.dx()) * static_cast<int64_t>(core_.dy());
  int64_t inst_area = 0;
  for (const auto& node : network_->getNodes()) {
    if (node->getType() == Node::CELL) {
      inst_area += static_cast<int64_t>(node->getWidth().v)
                   * static_cast<int64_t>(node->getHeight().v);
    }
  }
  diamond_design_utilization_
      = core_area > 0 ? (100.0 * static_cast<double>(inst_area)
                         / static_cast<double>(core_area))
                      : 0.0;
  logger_->metric("dpl_evolve__diamond__design_utilization_percent",
                  diamond_design_utilization_);
  resetDiamondSourceRuntime();

  if (debug_observer_) {
    logger_->report("Pause before detail placement.");
    debug_observer_->redrawAndPause();
  }

  place();
  diamondHpwlRepairPass();
  reportDiamondSourceTelemetry();

  if (debug_observer_) {
    logger_->report("Pause after detail placement.");
    debug_observer_->redrawAndPause();
  }
}

////////////////////////////////////////////////////////////////

void Opendp::placeGroups()
{
  groupAssignCellRegions();

  prePlaceGroups();
  prePlace();

  // naive placement method ( multi -> single )
  placeGroups2();
  for (auto& group : arch_->getRegions()) {
    // magic number alert
    for (int pass = 0; pass < 3; pass++) {
      int refine_count = groupRefine(group);
      int anneal_count = anneal(group);
      // magic number alert
      if (refine_count < 10 || anneal_count < 100) {
        break;
      }
    }
  }
}

void Opendp::prePlace()
{
  for (auto& cell : network_->getNodes()) {
    if (cell->getType() != Node::CELL) {
      continue;
    }
    const odb::Rect* group_rect = nullptr;
    if (!cell->inGroup() && !cell->isPlaced()) {
      for (auto& group : arch_->getRegions()) {
        for (const odb::Rect& rect : group->getRects()) {
          if (checkOverlap(cell.get(), rect)) {
            group_rect = &rect;
          }
        }
      }
      if (group_rect) {
        const DbuPt nearest = nearestPt(cell.get(), *group_rect);
        const GridPt legal = legalGridPt(cell.get(), nearest);
        if (diamondMove(cell.get(), legal)) {
          cell->setHold(true);
        }
      }
    }
  }
}

bool Opendp::checkOverlap(const Node* cell, const DbuRect& rect) const
{
  const DbuPt init = initialLocation(cell, false);
  const DbuX x = init.x;
  const DbuY y = init.y;
  return x + cell->getWidth() > rect.xl && x < rect.xl
         && y + cell->getHeight() > rect.yl && y < rect.yh;
}

DbuPt Opendp::nearestPt(const Node* cell, const DbuRect& rect) const
{
  const DbuPt init = initialLocation(cell, false);
  const DbuX x = init.x;
  const DbuY y = init.y;

  DbuX temp_x = x;
  DbuY temp_y = y;

  const DbuX cell_width = cell->getWidth();
  if (checkOverlap(cell, rect)) {
    DbuX dist_x;
    DbuY dist_y;
    if (abs(x + cell_width - rect.xl) > abs(rect.xh - x)) {
      dist_x = abs(rect.xh - x);
      temp_x = rect.xh;
    } else {
      dist_x = abs(x - rect.xl);
      temp_x = rect.xl - cell_width;
    }
    if (abs(y + cell->getHeight() - rect.yl) > abs(rect.yh - y)) {
      dist_y = abs(rect.yh - y);
      temp_y = rect.yh;
    } else {
      dist_y = abs(y - rect.yl);
      temp_y = rect.yl - cell->getHeight();
    }
    if (dist_x.v < dist_y.v) {
      return {temp_x, y};
    }
    return {x, temp_y};
  }

  if (x < rect.xl) {
    temp_x = rect.xl;
  } else if (x + cell_width > rect.xh) {
    temp_x = rect.xh - cell_width;
  }

  if (y < rect.yl) {
    temp_y = rect.yl;
  } else if (y + cell->getHeight() > rect.yh) {
    temp_y = rect.yh - cell->getHeight();
  }

  return {temp_x, temp_y};
}

void Opendp::prePlaceGroups()
{
  for (auto& group : arch_->getRegions()) {
    for (Node* cell : group->getCells()) {
      if (!cell->isFixed() && !cell->isPlaced()) {
        int dist = numeric_limits<int>::max();
        bool in_group = false;
        const odb::Rect* nearest_rect = nullptr;
        for (const odb::Rect& rect : group->getRects()) {
          if (isInside(cell, rect)) {
            in_group = true;
          }
          int rect_dist = distToRect(cell, rect);
          if (rect_dist < dist) {
            dist = rect_dist;
            nearest_rect = &rect;
          }
        }
        if (!nearest_rect) {
          continue;  // degenerate case of empty group.regions
        }
        if (!in_group) {
          const DbuPt nearest = nearestPt(cell, *nearest_rect);
          const GridPt legal = legalGridPt(cell, nearest);
          if (diamondMove(cell, legal)) {
            cell->setHold(true);
          }
        }
      }
    }
  }
}

bool Opendp::isInside(const Node* cell, const odb::Rect& rect) const
{
  const DbuPt init = initialLocation(cell, false);
  const DbuX x = init.x;
  const DbuY y = init.y;
  return x >= rect.xMin() && x + cell->getWidth() <= rect.xMax()
         && y >= rect.yMin() && y + cell->getHeight() <= rect.yMax();
}

int Opendp::distToRect(const Node* cell, const odb::Rect& rect) const
{
  const DbuPt init = initialLocation(cell, true);
  const DbuX x = init.x;
  const DbuY y = init.y;

  DbuX dist_x{0};
  DbuY dist_y{0};
  if (x < rect.xMin()) {
    dist_x = DbuX{rect.xMin()} - x;
  } else if (x + cell->getWidth() > rect.xMax()) {
    dist_x = x + cell->getWidth() - rect.xMax();
  }

  if (y < rect.yMin()) {
    dist_y = DbuY{rect.yMin()} - y;
  } else if (y + cell->getHeight() > rect.yMax()) {
    dist_y = y + cell->getHeight() - rect.yMax();
  }

  return sumXY(dist_x, dist_y);
}

class CellPlaceOrderLess
{
 public:
  explicit CellPlaceOrderLess(const odb::Rect& core, const Opendp* opendp);
  bool operator()(const Node* cell1, const Node* cell2) const;

 private:
  int centerDist(const Node* cell) const;

  const int center_x_;
  const int center_y_;
  const Opendp* opendp_;
};

CellPlaceOrderLess::CellPlaceOrderLess(const odb::Rect& core,
                                       const Opendp* opendp)
    : center_x_((core.xMin() + core.xMax()) / 2),
      center_y_((core.yMin() + core.yMax()) / 2),
      opendp_(opendp)
{
}

int CellPlaceOrderLess::centerDist(const Node* cell) const
{
  return sumXY(abs(cell->getLeft() - center_x_),
               abs(cell->getBottom() - center_y_));
}

bool CellPlaceOrderLess::operator()(const Node* cell1, const Node* cell2) const
{
  const bool is_multi_row1 = opendp_->isMultiRow(cell1);
  const bool is_multi_row2 = opendp_->isMultiRow(cell2);

  if (is_multi_row1 != is_multi_row2) {
    return is_multi_row1;
  }

  const int64_t area1 = cell1->area();
  const int64_t area2 = cell2->area();
  const int dist1 = centerDist(cell1);
  const int dist2 = centerDist(cell2);
  return area1 > area2
         || (area1 == area2
             && (dist1 < dist2
                 || (dist1 == dist2
                     && strcmp(cell1->getDbInst()->getConstName(),
                               cell2->getDbInst()->getConstName())
                            < 0)));
}

void Opendp::place()
{
  auto report_placement = [this](
                              Node* cell, bool diamond_move, bool rip_up_move) {
    if (debug_observer_) {
      const char* type = isMultiRow(cell) ? "multi-row" : "single-row";
      if (diamond_move) {
        logger_->report("Successful diamondMove(), {} cell {}, #moves: {}",
                        type,
                        cell->name(),
                        move_count_);
      } else {
        logger_->report(
            "Failed diamondMove(), {} cell {}, trying ripUpAndReplace(), "
            "#moves: {}",
            type,
            cell->name(),
            move_count_);
        if (rip_up_move) {
          logger_->report(
              "Successful ripUpAndReplace(), {} cell {}, #moves: {}",
              type,
              cell->name(),
              move_count_);
        } else {
          logger_->report("Unsuccessful placement, {} cell {}, #moves: {}",
                          type,
                          cell->name(),
                          move_count_);
        }
      }
      move_count_++;
      if (jump_moves_ > 0 && (move_count_ % jump_moves_ != 0)) {
        deep_iterative_debug_ = false;
        return;
      }
      deep_iterative_debug_ = true;
      debug_observer_->redrawAndPause();
    }
  };

  vector<Node*> sorted_cells;
  sorted_cells.reserve(network_->getNumCells());
  int failed_diamond_move = 0, failed_rip_up = 0, success_diamond_move = 0;

  for (auto& cell : network_->getNodes()) {
    if (cell->getType() != Node::CELL
        || !cell->getDbInst()->getMaster()->isCore()) {
      continue;
    }
    if (!(cell->isFixed() || cell->inGroup() || cell->isPlaced())) {
      sorted_cells.push_back(cell.get());
      if (!grid_->cellFitsInCore(cell.get())) {
        logger_->error(DPL,
                       15,
                       "instance {} does not fit inside the ROW core area.",
                       cell->name());
      }
    }
  }
  std::ranges::sort(sorted_cells, CellPlaceOrderLess(core_, this));

  DiamondSourceState& diamond_state = diamondSourceState();
  auto& order_telemetry = diamond_state.order;
  // Keep the bundle-14 queue-bucket path as diagnostic-only until it can
  // beat the iter06 quality line without reopening tail displacement.
  const bool enable_mid_util_queue_order = false;
  if (enable_mid_util_queue_order && diamond_design_utilization_ >= 80.0
      && diamond_design_utilization_ < 90.0
      && !sorted_cells.empty()) {
    struct OrderCandidate
    {
      Node* cell = nullptr;
      size_t base_index = 0;
      int pressure_dbu = 0;
      int critical_edges = 0;
      uint64_t source_hpwl = 0;
      uint64_t anchor_hpwl = 0;
      double anchor_gain_ratio = 0.0;
    };
    struct OrderEdgeStat
    {
      int static_min_x = numeric_limits<int>::max();
      int static_max_x = numeric_limits<int>::min();
      int static_min_y = numeric_limits<int>::max();
      int static_max_y = numeric_limits<int>::min();
      int64_t static_sum_x = 0;
      int64_t static_sum_y = 0;
      int static_pin_count = 0;
      std::vector<std::pair<int, int>> moved_offsets;
    };

    const int row_count = grid_->getRowCount().v;
    const int64_t row_capacity_dbu
        = static_cast<int64_t>(grid_->getRowSiteCount().v)
          * static_cast<int64_t>(grid_->getSiteWidth().v);
    std::vector<int64_t> row_demand_dbu(row_count, 0);
    std::vector<GridPt> source_grids;
    std::vector<GridY> cell_heights;
    source_grids.reserve(sorted_cells.size());
    cell_heights.reserve(sorted_cells.size());
    for (Node* cell : sorted_cells) {
      const GridPt source_grid = legalGridPt(cell, false);
      const GridY cell_height = grid_->gridHeight(cell);
      const int64_t cell_width_dbu
          = static_cast<int64_t>(grid_->gridWidth(cell).v)
            * static_cast<int64_t>(grid_->getSiteWidth().v);
      const int row_begin = std::max(0, source_grid.y.v);
      const int row_end = std::min(row_count, source_grid.y.v + cell_height.v);
      for (int row = row_begin; row < row_end; ++row) {
        row_demand_dbu[row] += cell_width_dbu;
      }
      source_grids.push_back(source_grid);
      cell_heights.push_back(cell_height);
    }

    std::vector<int64_t> row_overflow_dbu(row_count, 0);
    for (int row = 0; row < row_count; ++row) {
      row_overflow_dbu[row]
          = std::max<int64_t>(0, row_demand_dbu[row] - row_capacity_dbu);
    }

    const int pressure_guard = std::max(
        grid_->getSiteWidth().v * 24,
        static_cast<int>(std::max<int64_t>(1, row_capacity_dbu / 40)));
    const int low_pressure_guard = std::max(grid_->getSiteWidth().v * 8,
                                            pressure_guard / 4);
    const auto pressureAround = [&](const GridPt& source_grid,
                                    const GridY cell_height) {
      int64_t pressure = 0;
      const int row_begin = std::max(0, source_grid.y.v - 1);
      const int row_end
          = std::min(row_count, source_grid.y.v + cell_height.v + 1);
      for (int row = row_begin; row < row_end; ++row) {
        pressure += row_overflow_dbu[row];
      }
      return static_cast<int>(
          std::min<int64_t>(pressure, std::numeric_limits<int>::max()));
    };
    const auto gainBandIndex = [](const double anchor_gain_ratio) {
      if (anchor_gain_ratio < 0.03) {
        return 0;
      }
      if (anchor_gain_ratio < 0.05) {
        return 1;
      }
      return 2;
    };
    const auto recordGainBand
        = [](DiamondSourceState::OrderTelemetry& telemetry,
             const bool selected,
             const int band) {
      int* band_slot = nullptr;
      switch (band) {
        case 0:
          band_slot = selected ? &telemetry.gain_selected_low
                               : &telemetry.gain_pool_low;
          break;
        case 1:
          band_slot = selected ? &telemetry.gain_selected_mid
                               : &telemetry.gain_pool_mid;
          break;
        default:
          band_slot = selected ? &telemetry.gain_selected_high
                               : &telemetry.gain_pool_high;
          break;
      }
      ++(*band_slot);
    };
    const auto recordShiftBand
        = [](DiamondSourceState::OrderTelemetry& telemetry,
             const size_t shift) {
      if (shift <= 256) {
        ++telemetry.shift_selected_small;
      } else if (shift <= 512) {
        ++telemetry.shift_selected_mid;
      } else {
        ++telemetry.shift_selected_large;
      }
    };

    std::vector<OrderCandidate> earlier_pool;
    std::vector<OrderCandidate> later_pool;
    earlier_pool.reserve(sorted_cells.size() / 8 + 1);
    later_pool.reserve(sorted_cells.size() / 8 + 1);
    const auto orderEdgesFor = [&](const Node* candidate_cell) {
      std::vector<const Edge*> edges;
      edges.reserve(candidate_cell->getPins().size());
      std::unordered_set<const Edge*> seen;
      for (const Pin* pin : candidate_cell->getPins()) {
        if (pin == nullptr || pin->getEdge() == nullptr) {
          continue;
        }
        const Edge* edge = pin->getEdge();
        if (edge->getNumPins() <= 1 || edge->getNumPins() >= 100) {
          continue;
        }
        if (seen.insert(edge).second) {
          edges.push_back(edge);
        }
      }
      return edges;
    };
    const auto edgeHpwlAt = [&](const OrderEdgeStat& edge_stat,
                                const int center_x,
                                const int center_y) {
      int min_x = edge_stat.static_min_x;
      int max_x = edge_stat.static_max_x;
      int min_y = edge_stat.static_min_y;
      int max_y = edge_stat.static_max_y;
      for (const auto& [offset_x, offset_y] : edge_stat.moved_offsets) {
        const int pin_x = center_x + offset_x;
        const int pin_y = center_y + offset_y;
        min_x = std::min(min_x, pin_x);
        max_x = std::max(max_x, pin_x);
        min_y = std::min(min_y, pin_y);
        max_y = std::max(max_y, pin_y);
      }
      return static_cast<uint64_t>(max_x - min_x) + (max_y - min_y);
    };
    order_telemetry.candidate_cells = 0;
    for (size_t idx = 0; idx < sorted_cells.size(); ++idx) {
      Node* cell = sorted_cells[idx];
      const std::vector<const Edge*> order_edges = orderEdgesFor(cell);
      if (order_edges.empty()) {
        continue;
      }

      const GridPt source_grid = source_grids[idx];
      const DbuX source_left = gridToDbu(source_grid.x, grid_->getSiteWidth());
      const DbuY source_bottom = grid_->gridYToDbu(source_grid.y);
      const int source_center_x = (source_left + cell->getWidth() / DbuX{2}).v;
      const int source_center_y
          = (source_bottom + cell->getHeight() / DbuY{2}).v;
      std::vector<OrderEdgeStat> edge_stats;
      edge_stats.reserve(order_edges.size());
      int64_t anchor_sum_x = 0;
      int64_t anchor_sum_y = 0;
      int anchor_pin_count = 0;
      uint64_t source_hpwl = 0;
      for (const Edge* edge : order_edges) {
        OrderEdgeStat edge_stat;
        for (const Pin* pin : edge->getPins()) {
          const Node* node = pin->getNode();
          if (node == nullptr) {
            continue;
          }
          if (node == cell) {
            edge_stat.moved_offsets.push_back(
                {pin->getOffsetX().v, pin->getOffsetY().v});
            continue;
          }
          const int pin_x = (node->getCenterX() + pin->getOffsetX()).v;
          const int pin_y = (node->getCenterY() + pin->getOffsetY()).v;
          edge_stat.static_min_x = std::min(edge_stat.static_min_x, pin_x);
          edge_stat.static_max_x = std::max(edge_stat.static_max_x, pin_x);
          edge_stat.static_min_y = std::min(edge_stat.static_min_y, pin_y);
          edge_stat.static_max_y = std::max(edge_stat.static_max_y, pin_y);
          edge_stat.static_sum_x += pin_x;
          edge_stat.static_sum_y += pin_y;
          ++edge_stat.static_pin_count;
        }
        if (edge_stat.moved_offsets.empty() || edge_stat.static_pin_count == 0) {
          continue;
        }
        anchor_sum_x += edge_stat.static_sum_x;
        anchor_sum_y += edge_stat.static_sum_y;
        anchor_pin_count += edge_stat.static_pin_count;
        source_hpwl += edgeHpwlAt(edge_stat, source_center_x, source_center_y);
        edge_stats.push_back(std::move(edge_stat));
      }
      if (edge_stats.empty() || anchor_pin_count == 0 || source_hpwl == 0) {
        continue;
      }
      ++order_telemetry.candidate_cells;

      const int anchor_center_x = static_cast<int>(anchor_sum_x / anchor_pin_count);
      const int anchor_center_y = static_cast<int>(anchor_sum_y / anchor_pin_count);
      uint64_t anchor_hpwl = 0;
      for (const OrderEdgeStat& edge_stat : edge_stats) {
        anchor_hpwl += edgeHpwlAt(edge_stat, anchor_center_x, anchor_center_y);
      }
      if (anchor_hpwl >= source_hpwl || source_hpwl == 0) {
        continue;
      }

      const int pressure_dbu = pressureAround(source_grid, cell_heights[idx]);
      const int critical_edges = static_cast<int>(edge_stats.size());
      const double anchor_gain_ratio
          = static_cast<double>(source_hpwl - anchor_hpwl)
            / static_cast<double>(source_hpwl);
      OrderCandidate candidate{.cell = cell,
                               .base_index = idx,
                               .pressure_dbu = pressure_dbu,
                               .critical_edges = critical_edges,
                               .source_hpwl = source_hpwl,
                               .anchor_hpwl = anchor_hpwl,
                               .anchor_gain_ratio = anchor_gain_ratio};
      if (anchor_gain_ratio >= 0.018 && pressure_dbu >= pressure_guard
          && critical_edges >= 2) {
        earlier_pool.push_back(candidate);
      } else if (anchor_gain_ratio <= 0.004 && pressure_dbu <= low_pressure_guard
                 && critical_edges <= 2) {
        later_pool.push_back(candidate);
      }
    }

    order_telemetry.earlier_pool = static_cast<int>(earlier_pool.size());
    order_telemetry.later_pool = static_cast<int>(later_pool.size());
    order_telemetry.critical_candidates = order_telemetry.earlier_pool;
    for (const OrderCandidate& candidate : earlier_pool) {
      recordGainBand(order_telemetry,
                     false,
                     gainBandIndex(candidate.anchor_gain_ratio));
    }

    const size_t earlier_cap
        = std::min<size_t>(96, std::max<size_t>(24, sorted_cells.size() / 192));
    const size_t later_cap
        = std::min<size_t>(48, std::max<size_t>(8, sorted_cells.size() / 512));
    if (!earlier_pool.empty() || !later_pool.empty()) {
      std::ranges::sort(earlier_pool,
                        [](const OrderCandidate& left,
                           const OrderCandidate& right) {
                          return std::tuple(left.pressure_dbu,
                                            left.anchor_gain_ratio,
                                            left.critical_edges,
                                            -static_cast<int64_t>(
                                                left.base_index))
                                 > std::tuple(right.pressure_dbu,
                                              right.anchor_gain_ratio,
                                              right.critical_edges,
                                              -static_cast<int64_t>(
                                                  right.base_index));
                        });
      std::ranges::sort(later_pool,
                        [](const OrderCandidate& left,
                           const OrderCandidate& right) {
                          return std::tuple(left.anchor_gain_ratio,
                                            left.pressure_dbu,
                                            left.critical_edges,
                                            left.base_index)
                                 < std::tuple(right.anchor_gain_ratio,
                                              right.pressure_dbu,
                                              right.critical_edges,
                                              right.base_index);
                        });

      const size_t queue_bucket_size = std::clamp<size_t>(
          sorted_cells.size() / 12, 768, 1536);
      const size_t bucket_count
          = (sorted_cells.size() + queue_bucket_size - 1) / queue_bucket_size;
      order_telemetry.queue_bucket_size = static_cast<int>(queue_bucket_size);
      struct BucketStat
      {
        uint64_t gain_sum = 0;
        int64_t pressure_sum = 0;
        int count = 0;
        int high_gain_count = 0;
        int later_count = 0;
      };
      std::vector<BucketStat> bucket_stats(bucket_count);
      for (const OrderCandidate& candidate : earlier_pool) {
        BucketStat& bucket = bucket_stats[candidate.base_index / queue_bucket_size];
        bucket.gain_sum += candidate.source_hpwl - candidate.anchor_hpwl;
        bucket.pressure_sum += candidate.pressure_dbu;
        ++bucket.count;
        if (candidate.anchor_gain_ratio >= 0.05) {
          ++bucket.high_gain_count;
        }
      }
      for (const OrderCandidate& candidate : later_pool) {
        ++bucket_stats[candidate.base_index / queue_bucket_size].later_count;
      }

      std::vector<size_t> bucket_order(bucket_count);
      std::iota(bucket_order.begin(), bucket_order.end(), 0);
      std::ranges::sort(bucket_order,
                        [&](const size_t left, const size_t right) {
                          const BucketStat& left_bucket = bucket_stats[left];
                          const BucketStat& right_bucket = bucket_stats[right];
                          return std::tuple(left_bucket.high_gain_count,
                                            left_bucket.gain_sum,
                                            left_bucket.pressure_sum,
                                            left_bucket.count,
                                            -static_cast<int64_t>(left))
                                 > std::tuple(right_bucket.high_gain_count,
                                              right_bucket.gain_sum,
                                              right_bucket.pressure_sum,
                                              right_bucket.count,
                                              -static_cast<int64_t>(right));
                        });

      std::vector<bool> active_earlier_bucket(bucket_count, false);
      std::vector<bool> active_later_bucket(bucket_count, false);
      const size_t active_bucket_cap = std::min<size_t>(
          2, std::max<size_t>(1, sorted_cells.size() / 6144 + 1));
      const uint64_t top_bucket_gain = bucket_order.empty()
                                           ? 0
                                           : bucket_stats[bucket_order.front()].gain_sum;
      for (const size_t bucket_id : bucket_order) {
        if (order_telemetry.active_earlier_buckets >= active_bucket_cap) {
          break;
        }
        const BucketStat& bucket = bucket_stats[bucket_id];
        if (bucket.count == 0) {
          break;
        }
        const bool strong_bucket = bucket.high_gain_count > 0;
        const bool gainworthy_bucket
            = top_bucket_gain > 0 && bucket.gain_sum * 10 >= top_bucket_gain * 4;
        if (!strong_bucket && !gainworthy_bucket) {
          continue;
        }
        active_earlier_bucket[bucket_id] = true;
        ++order_telemetry.active_earlier_buckets;
      }
      if (order_telemetry.active_earlier_buckets == 0 && !bucket_order.empty()) {
        const size_t fallback_bucket = bucket_order.front();
        if (bucket_stats[fallback_bucket].count > 0) {
          active_earlier_bucket[fallback_bucket] = true;
          order_telemetry.active_earlier_buckets = 1;
        }
      }
      for (size_t bucket_id = 0; bucket_id < bucket_count; ++bucket_id) {
        if (bucket_stats[bucket_id].later_count == 0) {
          continue;
        }
        const bool near_active
            = active_earlier_bucket[bucket_id]
              || (bucket_id > 0 && active_earlier_bucket[bucket_id - 1])
              || (bucket_id + 1 < bucket_count
                  && active_earlier_bucket[bucket_id + 1]);
        if (near_active) {
          active_later_bucket[bucket_id] = true;
          ++order_telemetry.active_later_buckets;
        }
      }

      std::unordered_set<Node*> selected_earlier;
      std::unordered_set<Node*> selected_later;
      selected_earlier.reserve(earlier_cap * 2 + 1);
      selected_later.reserve(later_cap * 2 + 1);
      std::vector<size_t> earlier_bucket_selected(bucket_count, 0);
      std::vector<size_t> later_bucket_selected(bucket_count, 0);
      const size_t per_bucket_earlier_cap = std::clamp<size_t>(
          order_telemetry.active_earlier_buckets == 0
              ? earlier_cap
              : (earlier_cap + order_telemetry.active_earlier_buckets - 1)
                    / order_telemetry.active_earlier_buckets,
          8,
          24);
      const size_t per_bucket_later_cap = std::clamp<size_t>(
          order_telemetry.active_later_buckets == 0
              ? later_cap
              : (later_cap + order_telemetry.active_later_buckets - 1)
                    / order_telemetry.active_later_buckets,
          4,
          12);
      std::vector<OrderCandidate> earlier_selected;
      std::vector<OrderCandidate> later_selected;
      earlier_selected.reserve(earlier_cap);
      later_selected.reserve(later_cap);
      for (const OrderCandidate& candidate : earlier_pool) {
        const size_t bucket_id = candidate.base_index / queue_bucket_size;
        if (!active_earlier_bucket[bucket_id]) {
          ++order_telemetry.critical_rejected_bucket;
          continue;
        }
        if (earlier_selected.size() >= earlier_cap
            || earlier_bucket_selected[bucket_id] >= per_bucket_earlier_cap) {
          ++order_telemetry.critical_rejected_quota;
          continue;
        }
        if (!selected_earlier.insert(candidate.cell).second) {
          continue;
        }
        earlier_selected.push_back(candidate);
        ++earlier_bucket_selected[bucket_id];
        ++order_telemetry.earlier_selected;
        ++order_telemetry.critical_selected;
        order_telemetry.earlier_pressure += candidate.pressure_dbu;
        order_telemetry.earlier_critical_edges += candidate.critical_edges;
        order_telemetry.earlier_hpwl_before += candidate.source_hpwl;
        order_telemetry.earlier_hpwl_after += candidate.anchor_hpwl;
        recordGainBand(order_telemetry,
                       true,
                       gainBandIndex(candidate.anchor_gain_ratio));
      }
      for (const OrderCandidate& candidate : later_pool) {
        const size_t bucket_id = candidate.base_index / queue_bucket_size;
        if (!active_later_bucket[bucket_id]
            || later_selected.size() >= later_cap
            || later_bucket_selected[bucket_id] >= per_bucket_later_cap) {
          continue;
        }
        if (selected_earlier.contains(candidate.cell)
            || !selected_later.insert(candidate.cell).second) {
          continue;
        }
        later_selected.push_back(candidate);
        ++later_bucket_selected[bucket_id];
        ++order_telemetry.later_selected;
        order_telemetry.later_pressure += candidate.pressure_dbu;
        order_telemetry.later_critical_edges += candidate.critical_edges;
        order_telemetry.later_hpwl_before += candidate.source_hpwl;
        order_telemetry.later_hpwl_after += candidate.anchor_hpwl;
      }

      if (!earlier_selected.empty() || !later_selected.empty()) {
        const size_t earlier_shift_cap = std::clamp<size_t>(
            queue_bucket_size * 3 / 4, 384, 1024);
        const size_t later_shift_cap = std::clamp<size_t>(
            queue_bucket_size / 4, 96, 256);
        const size_t queue_guard = std::max<size_t>(64, queue_bucket_size / 6);

        struct QueueMoveRequest
        {
          Node* cell = nullptr;
          size_t base_index = 0;
          size_t target_index = 0;
          size_t band_begin = 0;
          size_t band_end = 0;
          bool move_earlier = false;
        };

        const auto makeBandRange = [&](const size_t base_index,
                                       const bool move_earlier) {
          const size_t bucket_id = base_index / queue_bucket_size;
          const size_t bucket_begin = bucket_id * queue_bucket_size;
          const size_t bucket_end
              = std::min(sorted_cells.size(), bucket_begin + queue_bucket_size);
          const size_t band_begin = move_earlier
                                        ? (bucket_begin > queue_guard
                                               ? bucket_begin - queue_guard
                                               : 0)
                                        : bucket_begin;
          const size_t band_end = move_earlier
                                      ? bucket_end
                                      : std::min(sorted_cells.size(),
                                                 bucket_end + queue_guard);
          return std::pair(band_begin, band_end);
        };

        const auto earlierShiftFor = [&](const OrderCandidate& candidate,
                                         const size_t rank) {
          const int pressure_band
              = std::clamp(candidate.pressure_dbu / std::max(1, pressure_guard),
                           0,
                           6);
          const int gain_band
              = std::clamp(static_cast<int>((candidate.anchor_gain_ratio - 0.018)
                                            / 0.004)
                               + 1,
                           1,
                           6);
          const int edge_band = std::clamp(candidate.critical_edges - 1, 1, 6);
          size_t raw_shift = 160 + pressure_band * 64 + gain_band * 96
                             + edge_band * 16;
          if (rank < 8) {
            raw_shift += 192;
          } else if (rank < 24) {
            raw_shift += 96;
          }
          const size_t effective_cap
              = candidate.anchor_gain_ratio >= 0.05
                    ? earlier_shift_cap
                    : candidate.anchor_gain_ratio >= 0.03
                          ? std::min<size_t>(640, earlier_shift_cap)
                          : std::min<size_t>(448, earlier_shift_cap);
          if (raw_shift > effective_cap) {
            ++order_telemetry.earlier_capped;
          }
          return std::min(raw_shift, effective_cap);
        };
        const auto laterShiftFor = [&](const OrderCandidate& candidate,
                                       const size_t rank) {
          const int weak_gain_band
              = candidate.anchor_gain_ratio <= 0.001
                    ? 4
                    : candidate.anchor_gain_ratio <= 0.002
                          ? 3
                          : candidate.anchor_gain_ratio <= 0.003 ? 2 : 1;
          const int sparse_band = candidate.pressure_dbu <= 0
                                      ? 3
                                      : candidate.pressure_dbu
                                                <= low_pressure_guard / 2
                                            ? 2
                                            : 1;
          size_t raw_shift = 48 + weak_gain_band * 32 + sparse_band * 24;
          if (rank < 6) {
            raw_shift += 32;
          }
          if (raw_shift > later_shift_cap) {
            ++order_telemetry.later_capped;
          }
          return std::min(raw_shift, later_shift_cap);
        };

        std::vector<QueueMoveRequest> earlier_requests;
        std::vector<QueueMoveRequest> later_requests;
        earlier_requests.reserve(earlier_selected.size());
        later_requests.reserve(later_selected.size());
        for (size_t rank = 0; rank < earlier_selected.size(); ++rank) {
          const OrderCandidate& candidate = earlier_selected[rank];
          const auto [band_begin, band_end]
              = makeBandRange(candidate.base_index, true);
          const size_t shift = earlierShiftFor(candidate, rank);
          const size_t target_index = std::max(
              band_begin,
              candidate.base_index > shift ? candidate.base_index - shift : 0);
          order_telemetry.bucket_span_sum += band_end - band_begin;
          order_telemetry.max_bucket_span
              = std::max<int64_t>(order_telemetry.max_bucket_span,
                                  static_cast<int64_t>(band_end - band_begin));
          recordShiftBand(order_telemetry, shift);
          earlier_requests.push_back(QueueMoveRequest{.cell = candidate.cell,
                                                      .base_index = candidate.base_index,
                                                      .target_index = target_index,
                                                      .band_begin = band_begin,
                                                      .band_end = band_end,
                                                      .move_earlier = true});
        }
        for (size_t rank = 0; rank < later_selected.size(); ++rank) {
          const OrderCandidate& candidate = later_selected[rank];
          const auto [band_begin, band_end]
              = makeBandRange(candidate.base_index, false);
          const size_t shift = laterShiftFor(candidate, rank);
          const size_t target_index = std::min(
              band_end - 1, candidate.base_index + shift);
          order_telemetry.bucket_span_sum += band_end - band_begin;
          order_telemetry.max_bucket_span
              = std::max<int64_t>(order_telemetry.max_bucket_span,
                                  static_cast<int64_t>(band_end - band_begin));
          later_requests.push_back(QueueMoveRequest{.cell = candidate.cell,
                                                    .base_index = candidate.base_index,
                                                    .target_index = target_index,
                                                    .band_begin = band_begin,
                                                    .band_end = band_end,
                                                    .move_earlier = false});
        }

        std::ranges::sort(earlier_requests,
                          [](const QueueMoveRequest& left,
                             const QueueMoveRequest& right) {
                            return std::tuple(left.target_index, left.base_index)
                                 < std::tuple(right.target_index, right.base_index);
                          });
        std::ranges::sort(later_requests,
                          [](const QueueMoveRequest& left,
                             const QueueMoveRequest& right) {
                            return std::tuple(left.target_index, left.base_index)
                                 > std::tuple(right.target_index, right.base_index);
                          });

        std::vector<Node*> reordered_cells = sorted_cells;
        std::unordered_map<Node*, size_t> current_index;
        current_index.reserve(sorted_cells.size() * 2);
        for (size_t idx = 0; idx < reordered_cells.size(); ++idx) {
          current_index.emplace(reordered_cells[idx], idx);
        }

        const auto moveCell = [&](const QueueMoveRequest& request) {
          const auto current_itr = current_index.find(request.cell);
          if (current_itr == current_index.end()) {
            return;
          }
          const size_t current = current_itr->second;
          if (request.move_earlier) {
            const size_t target = std::max(request.band_begin, request.target_index);
            if (target >= current) {
              return;
            }
            std::rotate(reordered_cells.begin() + target,
                        reordered_cells.begin() + current,
                        reordered_cells.begin() + current + 1);
            for (size_t idx = target; idx <= current; ++idx) {
              current_index[reordered_cells[idx]] = idx;
            }
          } else {
            const size_t target
                = std::min(request.band_end - 1, request.target_index);
            if (target <= current) {
              return;
            }
            std::rotate(reordered_cells.begin() + current,
                        reordered_cells.begin() + current + 1,
                        reordered_cells.begin() + target + 1);
            for (size_t idx = current; idx <= target; ++idx) {
              current_index[reordered_cells[idx]] = idx;
            }
          }
        };

        for (const QueueMoveRequest& request : earlier_requests) {
          moveCell(request);
        }
        for (const QueueMoveRequest& request : later_requests) {
          moveCell(request);
        }

        std::unordered_map<Node*, size_t> base_index;
        base_index.reserve(sorted_cells.size() * 2);
        for (size_t idx = 0; idx < sorted_cells.size(); ++idx) {
          base_index.emplace(sorted_cells[idx], idx);
        }
        for (const OrderCandidate& candidate : earlier_selected) {
          const size_t base = base_index[candidate.cell];
          const size_t current = current_index[candidate.cell];
          if (current < base) {
            ++order_telemetry.moved_earlier_cells;
            const int64_t shift = static_cast<int64_t>(base - current);
            order_telemetry.earlier_slot_shift += shift;
            order_telemetry.max_earlier_slot_shift
                = std::max(order_telemetry.max_earlier_slot_shift, shift);
          }
        }
        for (const OrderCandidate& candidate : later_selected) {
          const size_t base = base_index[candidate.cell];
          const size_t current = current_index[candidate.cell];
          if (current > base) {
            ++order_telemetry.moved_later_cells;
            const int64_t shift = static_cast<int64_t>(current - base);
            order_telemetry.later_slot_shift += shift;
            order_telemetry.max_later_slot_shift
                = std::max(order_telemetry.max_later_slot_shift, shift);
          }
        }

        order_telemetry.active
            = order_telemetry.moved_earlier_cells > 0
              || order_telemetry.moved_later_cells > 0;
        if (order_telemetry.active) {
          sorted_cells = std::move(reordered_cells);
        }
      }
    }
  }
  int count = 0;
  for (Node* cell : sorted_cells) {
    if (iterative_debug_) {
      count++;
      logger_->report("Placing cell {}, multi-row: {}, count {}, %: {:.2f}",
                      cell->name(),
                      isMultiRow(cell),
                      count,
                      100.0 * count / sorted_cells.size());
    }

    bool diamond_move = diamondMove(cell);
    bool rip_up_move = false;

    if (!diamond_move) {
      // TODO: this is non-deteministic due to std::set<Node*>,
      // and experiments show no legalization for failed diamond searches.
      rip_up_move = ripUpAndReplace(cell);
      if (!rip_up_move) {
        failed_rip_up++;
      }
    }
    diamond_move == 1 ? success_diamond_move++ : failed_diamond_move++;

    if (iterative_debug_) {
      odb::Point initial_location = getOdbLocation(cell);
      odb::Point final_location = getDplLocation(cell);
      float len = odb::Point::squaredDistance(initial_location, final_location);
      if (len > 0) {
        report_placement(cell, diamond_move, rip_up_move);
      }
    }
  }

  const size_t total_cells = sorted_cells.size();
  const int success_rip_up = failed_diamond_move - failed_rip_up;

  logger_->report("Movements Summary");
  logger_->report("---------------------------------------");
  logger_->report("Total cells:                {:8d}", total_cells);
  logger_->report(
      "Diamond Move Success:       {:8d} ({:6.2f}%)",
      success_diamond_move,
      total_cells > 0 ? 100.0 * success_diamond_move / total_cells : 0.0);
  logger_->report("Diamond Move Failure:       {:8d}", failed_diamond_move);
  logger_->report(
      "Rip-up and replace Success: {:8d} ({:6.2f}% of diamond failures)",
      success_rip_up,
      failed_diamond_move > 0 ? 100.0 * success_rip_up / failed_diamond_move
                              : 0.0);
  logger_->report("Rip-up and replace Failure: {:8d}", failed_rip_up);
  logger_->report("Total Placement Failures:   {:8d}",
                  (int) placement_failures_.size());
  logger_->report("---------------------------------------");
}

void Opendp::placeGroups2()
{
  for (auto& group : arch_->getRegions()) {
    vector<Node*> group_cells;
    group_cells.reserve(network_->getNumCells());
    for (Node* cell : group->getCells()) {
      if (!cell->isFixed() && !cell->isPlaced()) {
        group_cells.push_back(cell);
      }
    }
    std::ranges::sort(group_cells, CellPlaceOrderLess(core_, this));

    bool pass = true;
    for (Node* cell : group_cells) {
      if (!cell->isFixed() && !cell->isPlaced()) {
        assert(cell->inGroup());
        pass = diamondMove(cell);
        if (!pass) {
          break;
        }
      }
    }

    if (!pass) {
      // Erase group cells
      for (Node* cell : group->getCells()) {
        unplaceCell(cell);
      }

      // Determine brick placement by utilization.
      // magic number alert
      if (group->getUtil() > 0.95) {
        brickPlace1(group);
      } else {
        brickPlace2(group);
      }
    }
  }
}

// Place cells in group toward edges.
void Opendp::brickPlace1(const Group* group)
{
  const odb::Rect& boundary = group->getBBox();
  vector<Node*> sorted_cells(group->getCells());

  std::ranges::sort(sorted_cells, [&](Node* cell1, Node* cell2) {
    return rectDist(cell1, boundary) < rectDist(cell2, boundary);
  });

  for (Node* cell : sorted_cells) {
    DbuX x;
    DbuY y;
    rectDist(cell, boundary, &x.v, &y.v);
    const GridPt legal = legalGridPt(cell, {x, y});
    // This looks for a site starting at the nearest corner in rect,
    // which seems broken. It should start looking at the nearest point
    // on the rect boundary. -cherry
    if (!diamondMove(cell, legal)) {
      logger_->error(DPL, 16, "cannot place instance {}.", cell->name());
    }
  }
}

void Opendp::rectDist(const Node* cell,
                      const odb::Rect& rect,
                      // Return values.
                      int* x,
                      int* y) const
{
  const DbuPt init = initialLocation(cell, false);
  const DbuX init_x = init.x;
  const DbuY init_y = init.y;

  if (init_x > (rect.xMin() + rect.xMax()) / 2) {
    *x = rect.xMax();
  } else {
    *x = rect.xMin();
  }

  if (init_y > (rect.yMin() + rect.yMax()) / 2) {
    *y = rect.yMax();
  } else {
    *y = rect.yMin();
  }
}

int Opendp::rectDist(const Node* cell, const odb::Rect& rect) const
{
  int x, y;
  rectDist(cell, rect, &x, &y);
  const DbuPt init = initialLocation(cell, false);
  return sumXY(abs(init.x - x), abs(init.y - y));
}

// Place group cells toward region edges.
void Opendp::brickPlace2(const Group* group)
{
  vector<Node*> sorted_cells(group->getCells());

  std::ranges::sort(sorted_cells, [&](Node* cell1, Node* cell2) {
    return rectDist(cell1, *cell1->getRegion())
           < rectDist(cell2, *cell2->getRegion());
  });

  for (Node* cell : sorted_cells) {
    if (!cell->isHold()) {
      DbuX x;
      DbuY y;
      rectDist(cell, *cell->getRegion(), &x.v, &y.v);
      const GridPt legal = legalGridPt(cell, {x, y});
      // This looks for a site starting at the nearest corner in rect,
      // which seems broken. It should start looking at the nearest point
      // on the rect boundary. -cherry
      if (!diamondMove(cell, legal)) {
        logger_->error(DPL, 17, "cannot place instance {}.", cell->name());
      }
    }
  }
}

int Opendp::groupRefine(const Group* group)
{
  vector<Node*> sort_by_disp(group->getCells());

  std::ranges::sort(sort_by_disp, [&](Node* cell1, Node* cell2) {
    return (disp(cell1) > disp(cell2));
  });

  int count = 0;
  for (int i = 0; i < sort_by_disp.size() * group_refine_percent_; i++) {
    Node* cell = sort_by_disp[i];
    if (!cell->isHold() && !cell->isFixed()) {
      if (refineMove(cell)) {
        count++;
      }
    }
  }
  return count;
}

// This is NOT annealing. It is random swapping. -cherry
int Opendp::anneal(Group* group)
{
  std::mt19937 rand_gen(rand_seed_);
  int count = 0;

  // magic number alert
  using idx_range = boost::random::uniform_int_distribution<int>;
  const size_t num_cells = group->getCells().size();
  for (int i = 0; i < 100 * num_cells; i++) {
    const auto cell1_idx = idx_range(0, num_cells - 1)(rand_gen);
    const auto cell2_idx = idx_range(0, num_cells - 1)(rand_gen);
    Node* cell1 = group->getCells()[cell1_idx];
    Node* cell2 = group->getCells()[cell2_idx];
    if (swapCells(cell1, cell2)) {
      count++;
    }
  }
  return count;
}

// Not called -cherry.
int Opendp::refine()
{
  vector<Node*> sorted;
  sorted.reserve(network_->getNumCells());

  for (auto& cell : network_->getNodes()) {
    if (cell->getType() != Node::CELL) {
      continue;
    }
    if (!(cell->isFixed() || cell->isHold() || cell->inGroup())) {
      sorted.push_back(cell.get());
    }
  }
  std::ranges::sort(sorted, [&](Node* cell1, Node* cell2) {
    return disp(cell1) > disp(cell2);
  });

  int count = 0;
  for (int i = 0; i < sorted.size() * refine_percent_; i++) {
    Node* cell = sorted[i];
    if (!cell->isHold()) {
      if (refineMove(cell)) {
        count++;
      }
    }
  }
  return count;
}

////////////////////////////////////////////////////////////////

bool Opendp::diamondMove(Node* cell)
{
  const GridPt init = legalGridPt(cell, false);
  return diamondMove(cell, init);
}

bool Opendp::diamondMove(Node* cell, const GridPt& grid_pt)
{
  debugPrint(logger_,
             DPL,
             "place",
             1,
             "diamond move {} ({}, {}) to ({}, {})",
             cell->name(),
             cell->getLeft(),
             cell->getBottom(),
             grid_pt.x,
             grid_pt.y);
  const PixelPt pixel_pt = diamondSearch(cell, grid_pt.x, grid_pt.y);
  debugPrint(logger_,
             DPL,
             "place",
             1,
             "Diamond search {} ({}, {}) to ({}, {})",
             cell->name(),
             cell->getLeft(),
             cell->getBottom(),
             pixel_pt.x,
             pixel_pt.y);
  if (pixel_pt.pixel) {
    placeCell(cell, pixel_pt.x, pixel_pt.y);
    recordDiamondSourceSelection(diamondLastSourceCandidate());
    recordDiamondSourceOutcome(diamondLastSourceCandidate(), true);
    recordDenseCandidateCommit(diamondSourceState());
    diamondLastSourceCandidate() = {};
    if (debug_observer_) {
      debug_observer_->drawSelected(cell->getDbInst(), false);
    }
    return true;
  }
  recordDenseCandidateReject(diamondSourceState());
  diamondLastSourceCandidate() = {};
  return false;
}

void Opendp::deepIterativePause(const std::string& message, bool only_print)
{
  if (deep_iterative_debug_ && debug_observer_) {
    logger_->report(message);
    if (!only_print) {
      debug_observer_->redrawAndPause();
    }
  }
}

bool Opendp::ripUpAndReplace(Node* target_cell)
{
  const GridPt taget_cell_pixel = legalGridPt(target_cell, true);
  // magic number alert
  const GridY boundary_margin{3};
  const GridX margin_width{grid_->gridPaddedWidth(target_cell).v
                           * (1 + boundary_margin.v)};
  std::set<Node*> region_cells;
  for (GridX x = taget_cell_pixel.x - margin_width;
       x <= (taget_cell_pixel.x + margin_width);
       x++) {
    for (GridY y = taget_cell_pixel.y - boundary_margin;
         y <= (taget_cell_pixel.y + boundary_margin);
         y++) {
      Pixel* pixel = grid_->gridPixel(x, y);
      if (pixel) {
        Node* cell_in_pixel = pixel->cell;
        if (cell_in_pixel && !cell_in_pixel->isFixed()) {
          region_cells.insert(cell_in_pixel);
        }
      }
    }
  }

  deepIterativePause("pause after legalGridPt() inside ripUpAndReplace(), cell "
                     + target_cell->name());

  // erase region cells
  for (Node* around_cell : region_cells) {
    if (target_cell->inGroup() == around_cell->inGroup()) {
      unplaceCell(around_cell);
    }
  }

  deepIterativePause("pause after unplacing cells inside ripUpAndReplace()");

  // place target cell
  bool success = true;
  if (!diamondMove(target_cell)) {
    deepIterativePause(
        "failed diamondMove() inside ripUpAndReplace() for target cell "
            + target_cell->name(),
        /*only_print=*/true);
    placement_failures_.push_back(target_cell);
    success = false;
  }

  deepIterativePause(
      "pause after placing target cell inside ripUpAndReplace()");

  // re-place erased cells
  for (Node* around_cell : region_cells) {
    deepIterativePause(
        "pause before diamondMove() inside ripUpAndReplace() for surrounding "
        "cell "
        + around_cell->name());

    if (target_cell->inGroup() == around_cell->inGroup()
        && !diamondMove(around_cell)) {
      deepIterativePause(
          "failed diamondMove() inside ripUpAndReplace() for surrounding cell "
              + around_cell->name(),
          /*only_print=*/true);
      placement_failures_.push_back(around_cell);
      success = false;
    }
  }

  deepIterativePause(
      "pause after placing surrounding cells inside ripUpAndReplace()");

  return success;
}

bool Opendp::swapCells(Node* cell1, Node* cell2)
{
  if (cell1 != cell2 && !cell1->isHold() && !cell2->isHold()
      && cell1->getWidth() == cell2->getWidth()
      && cell1->getHeight() == cell2->getHeight() && !cell1->isFixed()
      && !cell2->isFixed()) {
    const int dist_change
        = distChange(cell1, cell2->getLeft(), cell2->getBottom())
          + distChange(cell2, cell1->getLeft(), cell1->getBottom());

    if (dist_change < 0) {
      Journal journal(grid_.get(), nullptr);
      MoveCellAction action1(cell1,
                             cell1->getLeft(),
                             cell1->getBottom(),
                             cell2->getLeft(),
                             cell2->getBottom(),
                             cell1->isPlaced());
      journal.addAction(action1);

      MoveCellAction action2(cell2,
                             cell2->getLeft(),
                             cell2->getBottom(),
                             cell1->getLeft(),
                             cell1->getBottom(),
                             cell2->isPlaced());
      journal.addAction(action2);

      const GridX grid_x1 = grid_->gridX(cell2);
      const GridY grid_y1 = grid_->gridSnapDownY(cell2);
      const GridX grid_x2 = grid_->gridX(cell1);
      const GridY grid_y2 = grid_->gridSnapDownY(cell1);

      unplaceCell(cell1);
      unplaceCell(cell2);
      placeCell(cell1, grid_x1, grid_y1);
      placeCell(cell2, grid_x2, grid_y2);
      // Check if placement is valid
      if (drc_engine_->checkDRC(cell1) && drc_engine_->checkDRC(cell2)) {
        return true;
      }
      journal.undo();
    }
  }
  return false;
}

bool Opendp::refineMove(Node* cell)
{
  const GridPt grid_pt = legalGridPt(cell, false);
  const PixelPt pixel_pt = diamondSearch(cell, grid_pt.x, grid_pt.y);

  if (pixel_pt.pixel) {
    if (abs(grid_pt.x - pixel_pt.x) > max_displacement_x_
        || abs(grid_pt.y - pixel_pt.y) > max_displacement_y_) {
      return false;
    }

    const int dist_change
        = distChange(cell,
                     gridToDbu(pixel_pt.x, grid_->getSiteWidth()),
                     grid_->gridYToDbu(pixel_pt.y));

    if (dist_change < 0) {
      unplaceCell(cell);
      placeCell(cell, pixel_pt.x, pixel_pt.y);
      recordDiamondSourceSelection(diamondLastSourceCandidate());
      recordDiamondSourceOutcome(diamondLastSourceCandidate(), true);
      recordDenseCandidateCommit(diamondSourceState());
      diamondLastSourceCandidate() = {};
      return true;
    }
  }
  recordDenseCandidateReject(diamondSourceState());
  diamondLastSourceCandidate() = {};
  return false;
}

int Opendp::distChange(const Node* cell, const DbuX x, const DbuY y) const
{
  const DbuPt init = initialLocation(cell, false);
  const int cell_dist
      = sumXY(abs(cell->getLeft() - init.x), abs(cell->getBottom() - init.y));
  const int pt_dist = sumXY(abs(init.x - x), abs(init.y - y));
  return pt_dist - cell_dist;
}

////////////////////////////////////////////////////////////////

namespace {

constexpr double kDiamondDisplacementWeight = 0.35;
constexpr double kRepairDisplacementWeight = 0.20;
constexpr double kDiamondRowPressureWeight = 0.00;
constexpr double kTailRepairRowPressureWeight = 0.10;
constexpr double kTailRepairPercentileWeight = 0.55;
constexpr double kExtremeTailRepairPercentileWeight = 1.10;

std::vector<const Edge*> uniqueCellEdges(const Node* cell)
{
  std::vector<const Edge*> edges;
  if (cell == nullptr) {
    return edges;
  }
  edges.reserve(cell->getPins().size());
  std::unordered_set<const Edge*> seen;
  for (const Pin* pin : cell->getPins()) {
    if (pin == nullptr || pin->getEdge() == nullptr) {
      continue;
    }
    const Edge* edge = pin->getEdge();
    if (edge->getNumPins() <= 1 || edge->getNumPins() >= 100) {
      continue;
    }
    if (seen.insert(edge).second) {
      edges.push_back(edge);
    }
  }
  return edges;
}

struct CachedAffectedEdge
{
  struct PinOffset
  {
    int x;
    int y;
  };

  bool has_static_pin = false;
  int static_min_x = numeric_limits<int>::max();
  int static_max_x = numeric_limits<int>::min();
  int static_min_y = numeric_limits<int>::max();
  int static_max_y = numeric_limits<int>::min();
  int64_t static_sum_x = 0;
  int64_t static_sum_y = 0;
  int static_pin_count = 0;
  int moved_min_offset_x = numeric_limits<int>::max();
  int moved_max_offset_x = numeric_limits<int>::min();
  int moved_min_offset_y = numeric_limits<int>::max();
  int moved_max_offset_y = numeric_limits<int>::min();
  std::vector<PinOffset> moved_pin_offsets;

  void mergeStaticPin(const int x, const int y)
  {
    has_static_pin = true;
    static_min_x = std::min(static_min_x, x);
    static_max_x = std::max(static_max_x, x);
    static_min_y = std::min(static_min_y, y);
    static_max_y = std::max(static_max_y, y);
    static_sum_x += x;
    static_sum_y += y;
    ++static_pin_count;
  }

  void addMovedPinOffset(const int x, const int y)
  {
    moved_pin_offsets.push_back({.x = x, .y = y});
    moved_min_offset_x = std::min(moved_min_offset_x, x);
    moved_max_offset_x = std::max(moved_max_offset_x, x);
    moved_min_offset_y = std::min(moved_min_offset_y, y);
    moved_max_offset_y = std::max(moved_max_offset_y, y);
  }

  int totalPinCount() const
  {
    return static_pin_count + static_cast<int>(moved_pin_offsets.size());
  }
};

std::vector<CachedAffectedEdge> buildAffectedHpwlCache(const Node* cell)
{
  std::vector<CachedAffectedEdge> cached_edges;
  const std::vector<const Edge*> edges = uniqueCellEdges(cell);
  cached_edges.reserve(edges.size());

  for (const Edge* edge : edges) {
    CachedAffectedEdge cached_edge;
    for (const Pin* pin : edge->getPins()) {
      const Node* node = pin->getNode();
      if (node == nullptr) {
        continue;
      }
      if (node == cell) {
        cached_edge.addMovedPinOffset(pin->getOffsetX().v, pin->getOffsetY().v);
        continue;
      }
      cached_edge.mergeStaticPin((node->getCenterX() + pin->getOffsetX()).v,
                                 (node->getCenterY() + pin->getOffsetY()).v);
    }
    if (!cached_edge.moved_pin_offsets.empty()) {
      cached_edges.push_back(std::move(cached_edge));
    }
  }
  return cached_edges;
}

uint64_t cachedHpwlAt(const CachedAffectedEdge& edge,
                      const Node* cell,
                      const DbuX left,
                      const DbuY bottom)
{
  int min_x = edge.static_min_x;
  int max_x = edge.static_max_x;
  int min_y = edge.static_min_y;
  int max_y = edge.static_max_y;
  if (!edge.has_static_pin) {
    min_x = numeric_limits<int>::max();
    max_x = numeric_limits<int>::min();
    min_y = numeric_limits<int>::max();
    max_y = numeric_limits<int>::min();
  }

  const int center_x = (left + cell->getWidth() / DbuX{2}).v;
  const int center_y = (bottom + cell->getHeight() / DbuY{2}).v;
  for (const CachedAffectedEdge::PinOffset& offset :
       edge.moved_pin_offsets) {
    const int pin_x = center_x + offset.x;
    const int pin_y = center_y + offset.y;
    min_x = std::min(min_x, pin_x);
    max_x = std::max(max_x, pin_x);
    min_y = std::min(min_y, pin_y);
    max_y = std::max(max_y, pin_y);
  }

  return static_cast<uint64_t>(max_x - min_x) + (max_y - min_y);
}

uint64_t affectedHpwlAt(const std::vector<CachedAffectedEdge>& cached_edges,
                        const Node* cell,
                        const DbuX left,
                        const DbuY bottom)
{
  uint64_t hpwl = 0;
  for (const CachedAffectedEdge& edge : cached_edges) {
    hpwl += cachedHpwlAt(edge, cell, left, bottom);
  }
  return hpwl;
}

bool affectedHpwlAnchor(const std::vector<CachedAffectedEdge>& cached_edges,
                        const Node* cell,
                        DbuPt& anchor)
{
  int64_t sum_x = 0;
  int64_t sum_y = 0;
  int pin_count = 0;
  for (const CachedAffectedEdge& edge : cached_edges) {
    sum_x += edge.static_sum_x;
    sum_y += edge.static_sum_y;
    pin_count += edge.static_pin_count;
  }
  if (pin_count == 0) {
    return false;
  }

  anchor.x = DbuX{static_cast<int>(sum_x / pin_count)}
             - cell->getWidth() / DbuX{2};
  anchor.y = DbuY{static_cast<int>(sum_y / pin_count)}
             - cell->getHeight() / DbuY{2};
  return true;
}

}  // namespace

PixelPt Opendp::diamondSearch(const Node* cell,
                              const GridX x,
                              const GridY y,
                              const bool tail_repair,
                              const int displacement_p90,
                              const int displacement_p99)
{
  diamondLastSourceCandidate() = {};
  diamondSourceState().last_dense_candidate = {};
  // Diamond search limits.
  GridX x_min = x - max_displacement_x_;
  GridX x_max = x + max_displacement_x_;
  GridY y_min = y - max_displacement_y_;
  GridY y_max = y + max_displacement_y_;

  // Restrict search to group boundary.
  Group* group = cell->getGroup();
  if (group) {
    // Boundary to grid staying inside.
    const GridRect grid_boundary = grid_->gridWithin(group->getBBox());
    const GridPt min = grid_boundary.closestPtInside({x_min, y_min});
    const GridPt max = grid_boundary.closestPtInside({x_max, y_max});
    x_min = min.x;
    y_min = min.y;
    x_max = max.x;
    y_max = max.y;
  }

  // Clip limits to grid bounds.
  x_min = max(GridX{0}, x_min);
  y_min = max(GridY{0}, y_min);
  x_max = min(grid_->getRowSiteCount(), x_max);
  y_max = min(grid_->getRowCount(), y_max);
  debugPrint(logger_,
             DPL,
             "place",
             1,
             "Diamond search {} ({}, {}) bounds ({}-{}, {}-{})",
             cell->name(),
             x,
             y,
             x_min,
             x_max - 1,
             y_min,
             y_max - 1);

  struct PQ_entry
  {
    int manhattan_distance;
    GridPt p;
    int sequence;
    bool operator>(const PQ_entry& other) const
    {
      return std::tie(manhattan_distance, sequence)
             > std::tie(other.manhattan_distance, other.sequence);
    }
  };
  std::priority_queue<PQ_entry, std::vector<PQ_entry>, std::greater<PQ_entry>>
      positionsHeap;
  std::unordered_set<GridPt> visited;
  std::unordered_set<GridPt> scored_legal_candidates;
  std::unordered_map<GridPt, bool> feasibility_cache;
  std::unordered_map<GridPt, int> row_pressure_cache;
  const std::vector<CachedAffectedEdge> affected_edges
      = buildAffectedHpwlCache(cell);
  PixelPt best_pixel;
  double best_score = std::numeric_limits<double>::infinity();
  uint64_t best_hpwl = std::numeric_limits<uint64_t>::max();
  uint64_t best_hpwl_band_idx = std::numeric_limits<uint64_t>::max();
  int best_distance = std::numeric_limits<int>::max();
  int best_row_pressure = std::numeric_limits<int>::max();
  DiamondSourceState::DenseFragmentMetric best_fragment_metric;
  bool best_fragmentation_known = false;
  DiamondSourceState::DenseFragmentMetric best_pattern_metric;
  bool best_pattern_metric_known = false;
  DiamondSourceState::DenseGapShape best_gap_shape;
  bool best_gap_shape_known = false;
  GridPt best_grid{GridX{-1}, GridY{-1}};
  bool best_from_preprobe = false;
  int first_legal_distance = -1;
  int legal_candidates = 0;
  const int search_slack
      = std::max(grid_->getSiteWidth().v * 4,
                 grid_->gridYToDbu(GridY{std::min(y.v + 1,
                                                  grid_->getRowCount().v - 1)})
                         .v
                     - grid_->gridYToDbu(y).v);
  const uint64_t hpwl_equivalence_band = std::max<uint64_t>(
      static_cast<uint64_t>(grid_->getSiteWidth().v * 4),
      static_cast<uint64_t>(
          std::max(1, static_cast<int>(affected_edges.size()))
          * std::max(grid_->getSiteWidth().v, search_slack / 2)));
  const int preprobe_shorter_guard
      = std::max(grid_->getSiteWidth().v, search_slack / 2);
  const double preprobe_score_guard = static_cast<double>(
      tail_repair ? hpwl_equivalence_band : hpwl_equivalence_band / 2);
  const uint64_t bfs_hpwl_margin_guard = std::max<uint64_t>(
      static_cast<uint64_t>(grid_->getSiteWidth().v * 4),
      std::max<uint64_t>(1, hpwl_equivalence_band / 4));
  const int bfs_distance_guard
      = std::max(grid_->getSiteWidth().v * 2, search_slack / 2);
  const int bfs_row_pressure_guard
      = std::max(grid_->getSiteWidth().v * 8, search_slack);
  const double bfs_score_guard = preprobe_score_guard / 2.0;
  std::unordered_set<GridPt> preprobe_seeded_candidates;
  DiamondSourceState& diamond_state = diamondSourceState();
  const auto gainBandFor = [&](const uint64_t reference_hpwl,
                               const uint64_t candidate_hpwl) {
    const double gain_ratio
        = reference_hpwl > candidate_hpwl && reference_hpwl > 0
              ? static_cast<double>(reference_hpwl - candidate_hpwl)
                    / static_cast<double>(reference_hpwl)
              : 0.0;
    if (gain_ratio >= 0.05) {
      return DiamondGainBand::kStrong;
    }
    if (gain_ratio >= 0.015) {
      return DiamondGainBand::kMedium;
    }
    return DiamondGainBand::kWeak;
  };
  const auto distanceBandFor = [&](const int source_distance) {
    if (source_distance <= search_slack * 2) {
      return DiamondDistanceBand::kNear;
    }
    if (source_distance <= search_slack * 6) {
      return DiamondDistanceBand::kMid;
    }
    return DiamondDistanceBand::kFar;
  };
  struct PreProbeCandidate
  {
    GridPt pt;
    DiamondSourceFamily family = DiamondSourceFamily::kEndpoint;
    DiamondGainBand gain_band = DiamondGainBand::kWeak;
    DiamondDistanceBand distance_band = DiamondDistanceBand::kNear;
    uint64_t reference_hpwl = 0;
    uint64_t candidate_hpwl = std::numeric_limits<uint64_t>::max();
    int64_t predicted_hpwl_delta = std::numeric_limits<int64_t>::min();
    int state_rank = 0;
    int source_distance = 0;
    int kind_rank = 0;
  };
  struct DenseProbeCandidate
  {
    GridPt pt;
    uint64_t hpwl = std::numeric_limits<uint64_t>::max();
    int row_pressure = std::numeric_limits<int>::max();
    int distance = std::numeric_limits<int>::max();
  };
  std::unordered_map<GridPt, PreProbeCandidate> preprobe_candidates;
  constexpr int kMaxLegalCandidates = 16;
  int sequence = 0;
  GridPt center{x, y};
  positionsHeap.push(
      {.manhattan_distance = 0, .p = center, .sequence = sequence++});
  visited.insert(center);

  const GridX cell_width = grid_->gridWidth(cell);
  const GridY cell_height = grid_->gridHeight(cell);
  const int cell_width_sites = std::max(1, cell_width.v);
  const int cell_height_rows = std::max(1, cell_height.v);
  const uint64_t dense_hpwl_tie_guard = std::max<uint64_t>(
      static_cast<uint64_t>(grid_->getSiteWidth().v * 8),
      std::max<uint64_t>(1, hpwl_equivalence_band / 2));
  const int dense_distance_tie_guard
      = std::max(grid_->getSiteWidth().v * 2, search_slack / 2);
  const int dense_row_pressure_tie_guard
      = std::max(grid_->getSiteWidth().v * 8, search_slack);
  const int dense_fragment_guard = std::max(
      grid_->getSiteWidth().v * 6,
      cell_width_sites * grid_->getSiteWidth().v);
  const int dense_shape_minor_gap_guard = grid_->getSiteWidth().v;
  const int dense_shape_major_gap_guard = grid_->getSiteWidth().v * 2;
  const double dense_score_tie_guard = preprobe_score_guard / 2.0;
  auto rowPressureDbu = [&](const GridPt& candidate) {
    if (const auto cached = row_pressure_cache.find(candidate);
        cached != row_pressure_cache.end()) {
      return cached->second;
    }

    const GridX x_begin = max(GridX{0}, candidate.x - GridX{8});
    const GridX x_end
        = min(grid_->getRowSiteCount(), candidate.x + cell_width + GridX{8});
    const GridY y_end = min(grid_->getRowCount(), candidate.y + cell_height);
    int pressure = 0;
    for (GridY row = candidate.y; row < y_end; ++row) {
      for (GridX col = x_begin; col < x_end; ++col) {
        const Pixel* pixel = grid_->gridPixel(col, row);
        if (pixel == nullptr || !pixel->is_valid || pixel->cell
            || (cell->inGroup() && pixel->group != cell->getGroup())
            || (!cell->inGroup() && pixel->group)) {
          ++pressure;
        }
      }
    }
    const int pressure_dbu = pressure * grid_->getSiteWidth().v;
    row_pressure_cache.emplace(candidate, pressure_dbu);
    return pressure_dbu;
  };

  auto isFeasible = [&](const GridPt& candidate) {
    if (const auto cached = feasibility_cache.find(candidate);
        cached != feasibility_cache.end()) {
      return cached->second;
    }
    const bool feasible = canBePlaced(cell, candidate.x, candidate.y);
    feasibility_cache.emplace(candidate, feasible);
    return feasible;
  };

  const auto isOpenSite = [&](const GridX col, const GridY row) {
    const Pixel* pixel = grid_->gridPixel(col, row);
    return pixel != nullptr && pixel->is_valid && pixel->cell == nullptr
           && ((cell->inGroup() && pixel->group == cell->getGroup())
               || (!cell->inGroup() && pixel->group == nullptr));
  };

  const bool dense_row_context_enabled = false && !cell->inGroup();
  const int row_site_count = grid_->getRowSiteCount().v;
  auto denseRowGapContext = [&](const GridY row)
      -> const DiamondSourceState::DenseRowGapContext* {
    if (!dense_row_context_enabled || row.v < 0
        || row.v >= static_cast<int>(diamond_state.dense_row_gap_contexts.size())) {
      return nullptr;
    }

    ++diamond_state.dense.row_context_uses;
    auto& context = diamond_state.dense_row_gap_contexts[row.v];
    if (context.valid) {
      ++diamond_state.dense.rank_fragment_hits;
      return &context;
    }

    ++diamond_state.dense.rank_fragment_misses;
    ++diamond_state.dense.row_context_builds;
    if (static_cast<int>(context.left_open_sites.size()) != row_site_count) {
      context.left_open_sites.assign(row_site_count, 0);
      context.right_open_sites.assign(row_site_count, 0);
    } else {
      std::fill(context.left_open_sites.begin(), context.left_open_sites.end(), 0);
      std::fill(context.right_open_sites.begin(),
                context.right_open_sites.end(),
                0);
    }

    uint8_t run_length = 0;
    for (int col = 0; col < row_site_count; ++col) {
      if (isOpenSite(GridX{col}, row)) {
        run_length = std::min<uint8_t>(run_length + 1, kDenseRowGapCapSites);
        context.left_open_sites[col] = run_length;
      } else {
        run_length = 0;
      }
    }
    run_length = 0;
    for (int col = row_site_count - 1; col >= 0; --col) {
      if (isOpenSite(GridX{col}, row)) {
        run_length = std::min<uint8_t>(run_length + 1, kDenseRowGapCapSites);
        context.right_open_sites[col] = run_length;
      } else {
        run_length = 0;
      }
    }
    context.valid = true;
    return &context;
  };

  auto gapSitesOnSide = [&](const GridPt& candidate,
                            const GridY row,
                            const bool left_side,
                            const int gap_cap_sites) {
    int gap_sites = 0;
    if (const auto* row_context = denseRowGapContext(row); row_context != nullptr) {
      if (left_side) {
        if (candidate.x > GridX{0}) {
          gap_sites = std::min(
              gap_cap_sites,
              static_cast<int>(row_context->left_open_sites[candidate.x.v - 1]));
        }
      } else {
        const int right_col = candidate.x.v + cell_width.v;
        if (right_col < row_site_count) {
          gap_sites = std::min(
              gap_cap_sites,
              static_cast<int>(row_context->right_open_sites[right_col]));
        }
      }
      return gap_sites;
    }

    if (left_side) {
      for (int step = 1; step <= gap_cap_sites; ++step) {
        const GridX col = candidate.x - GridX{step};
        if (col < GridX{0} || !isOpenSite(col, row)) {
          break;
        }
        ++gap_sites;
      }
    } else {
      for (int step = 0; step < gap_cap_sites; ++step) {
        const GridX col = candidate.x + cell_width + GridX{step};
        if (col >= grid_->getRowSiteCount() || !isOpenSite(col, row)) {
          break;
        }
        ++gap_sites;
      }
    }
    return gap_sites;
  };

  auto denseFragmentMetric = [&](const GridPt& candidate,
                                 const int gap_cap_sites,
                                 const bool include_free_gap) {
    DiamondSourceState::DenseFragmentMetric metric;
    const int sliver_guard_sites = std::clamp(cell_width.v, 3, 10);
    int sliver_penalty_sites = 0;
    int split_penalty_sites = 0;
    const GridY y_end = min(grid_->getRowCount(), candidate.y + cell_height);
    for (GridY row = candidate.y; row < y_end; ++row) {
      const int left_gap_sites
          = gapSitesOnSide(candidate, row, true, gap_cap_sites);
      const int right_gap_sites
          = gapSitesOnSide(candidate, row, false, gap_cap_sites);

      if (include_free_gap) {
        metric.free_gap_dbu += (left_gap_sites + right_gap_sites)
                               * grid_->getSiteWidth().v;
      }
      if (left_gap_sites == 0 || right_gap_sites == 0) {
        ++metric.edge_rows;
      }
      if (left_gap_sites > 0 && right_gap_sites > 0) {
        ++metric.split_rows;
      }
      const auto sliverPenaltySites = [&](const int gap_sites) {
        return gap_sites > 0 && gap_sites < sliver_guard_sites
                   ? sliver_guard_sites - gap_sites
                   : 0;
      };
      sliver_penalty_sites += sliverPenaltySites(left_gap_sites)
                              + sliverPenaltySites(right_gap_sites);
      if (left_gap_sites > 0 && right_gap_sites > 0) {
        split_penalty_sites += std::min({left_gap_sites,
                                         right_gap_sites,
                                         sliver_guard_sites});
      }
    }

    metric.fragmentation_dbu
        = (sliver_penalty_sites * 2 + split_penalty_sites)
          * grid_->getSiteWidth().v;
    return metric;
  };

  auto denseCoreFragmentMetric = [&](const GridPt& candidate) {
    return denseFragmentMetric(
        candidate, std::clamp(cell_width.v, 3, 10), false);
  };

  auto denseFullFragmentMetric = [&](const GridPt& candidate) {
    return denseFragmentMetric(
        candidate, std::clamp(cell_width.v * 2, 8, 24), true);
  };

  auto denseSingleRowEdgeSignature = [&](const GridPt& candidate) {
    DiamondSourceState::DenseEdgeSignature signature;
    if (cell_height_rows != 1) {
      return signature;
    }
    const GridY row = candidate.y;
    if (candidate.x > GridX{0}) {
      signature.left_open = isOpenSite(candidate.x - GridX{1}, row);
    }
    const GridX right_col = candidate.x + cell_width;
    if (right_col < grid_->getRowSiteCount()) {
      signature.right_open = isOpenSite(right_col, row);
    }
    return signature;
  };

  auto denseSingleRowGapShape = [&](const GridPt& candidate) {
    DiamondSourceState::DenseGapShape shape;
    if (cell_height_rows != 1) {
      return shape;
    }
    const int gap_cap_sites = std::clamp(cell_width.v * 2, 8, 24);
    const int left_gap_sites
        = gapSitesOnSide(candidate, candidate.y, true, gap_cap_sites);
    const int right_gap_sites
        = gapSitesOnSide(candidate, candidate.y, false, gap_cap_sites);
    shape.left_gap_dbu = left_gap_sites * grid_->getSiteWidth().v;
    shape.right_gap_dbu = right_gap_sites * grid_->getSiteWidth().v;
    shape.min_gap_dbu = std::min(shape.left_gap_dbu, shape.right_gap_dbu);
    shape.max_gap_dbu = std::max(shape.left_gap_dbu, shape.right_gap_dbu);
    return shape;
  };

  auto denseSingleRowPatternMetric = [&](
                                         const DiamondSourceState::DenseGapShape&
                                             shape) {
    DiamondSourceState::DenseFragmentMetric metric;
    if (cell_height_rows != 1) {
      return metric;
    }
    const int site_width = grid_->getSiteWidth().v;
    const int left_gap_sites = shape.left_gap_dbu / site_width;
    const int right_gap_sites = shape.right_gap_dbu / site_width;
    metric.free_gap_dbu = shape.left_gap_dbu + shape.right_gap_dbu;
    metric.edge_rows = shape.isEdge() ? 1 : 0;
    metric.split_rows = shape.isSplit() ? 1 : 0;
    const int sliver_guard_sites = std::clamp(cell_width.v, 3, 10);
    const auto sliverPenaltySites = [&](const int gap_sites) {
      return gap_sites > 0 && gap_sites < sliver_guard_sites
                 ? sliver_guard_sites - gap_sites
                 : 0;
    };
    int split_penalty_sites = 0;
    if (shape.isSplit()) {
      split_penalty_sites = std::min({left_gap_sites,
                                      right_gap_sites,
                                      sliver_guard_sites});
    }
    metric.fragmentation_dbu
        = (sliverPenaltySites(left_gap_sites)
               + sliverPenaltySites(right_gap_sites))
              * 2 * site_width
          + split_penalty_sites * site_width;
    return metric;
  };

  auto scoreCandidate = [&](const GridPt& candidate) {
    if (candidate.x < x_min || candidate.x > x_max || candidate.y < y_min
        || candidate.y > y_max) {
      return false;
    }
    if (scored_legal_candidates.contains(candidate)) {
      return true;
    }
    if (!isFeasible(candidate)) {
      return false;
    }

    scored_legal_candidates.insert(candidate);
    const DbuX candidate_x = gridToDbu(candidate.x, grid_->getSiteWidth());
    const DbuY candidate_y = grid_->gridYToDbu(candidate.y);
    const uint64_t hpwl = affected_edges.empty()
                              ? 0
                              : affectedHpwlAt(
                                  affected_edges, cell, candidate_x, candidate_y);
    const int candidate_distance = calcDist(center, candidate);
    const bool from_preprobe = preprobe_seeded_candidates.contains(candidate);
    const bool high_util_dense_candidate
        = !tail_repair && !from_preprobe && diamond_design_utilization_ >= 90.0;
    DiamondSourceCandidateInfo candidate_source_info;
    if (from_preprobe) {
      const auto preprobe_it = preprobe_candidates.find(candidate);
      if (preprobe_it != preprobe_candidates.end()) {
        candidate_source_info.active = true;
        candidate_source_info.tail_repair = tail_repair;
        candidate_source_info.family = preprobe_it->second.family;
        candidate_source_info.gain_band = preprobe_it->second.gain_band;
        candidate_source_info.distance_band = preprobe_it->second.distance_band;
        candidate_source_info.reference_hpwl = preprobe_it->second.reference_hpwl;
        candidate_source_info.candidate_hpwl = preprobe_it->second.candidate_hpwl;
        candidate_source_info.source_distance = preprobe_it->second.source_distance;
      }
    }
    const double displacement_weight
        = tail_repair ? 1.25 : kDiamondDisplacementWeight;
    const int tail_surplus = tail_repair && displacement_p90 > 0
                                 ? std::max(0,
                                            candidate_distance
                                                - displacement_p90)
                                 : 0;
    const int extreme_tail_surplus = tail_repair && displacement_p99 > 0
                                         ? std::max(0,
                                                    candidate_distance
                                                        - displacement_p99)
                                         : 0;
    const double row_pressure_weight = tail_repair
                                           ? kTailRepairRowPressureWeight
                                           : kDiamondRowPressureWeight;
    const int candidate_row_pressure = rowPressureDbu(candidate);
    double score = static_cast<double>(hpwl)
                   + displacement_weight
                         * static_cast<double>(candidate_distance)
                   + kTailRepairPercentileWeight
                         * static_cast<double>(tail_surplus)
                   + kExtremeTailRepairPercentileWeight
                         * static_cast<double>(extreme_tail_surplus);
    if (row_pressure_weight > 0.0) {
      score += row_pressure_weight
               * static_cast<double>(candidate_row_pressure);
    }
    if (candidate_source_info.active
        && candidate_source_info.family == DiamondSourceFamily::kSpanContraction
        && !diamondSourceRuntime()[static_cast<int>(candidate_source_info.family)]
                .positive_evidence) {
      score += static_cast<double>(hpwl_equivalence_band / 2);
    }
    const bool mid_util_bfs_candidate
        = !tail_repair && !from_preprobe && !best_from_preprobe
          && diamond_design_utilization_ >= 80.0
          && diamond_design_utilization_ < 90.0
          && !diamondSourceRuntime()[static_cast<int>(
                 DiamondSourceFamily::kSpanContraction)]
                  .enabled;
    const bool probationary_span_candidate
        = !tail_repair && from_preprobe
          && candidate_source_info.active
          && candidate_source_info.family == DiamondSourceFamily::kSpanContraction
          && !diamondSourceRuntime()[static_cast<int>(
                 DiamondSourceFamily::kSpanContraction)]
                  .enabled;
    if (!tail_repair && !from_preprobe
        && diamond_design_utilization_ >= 80.0
        && diamond_design_utilization_ < 90.0) {
      ++diamond_state.comparator.bfs_scored;
    }
    if (high_util_dense_candidate) {
      ++diamond_state.dense.rank_scored;
      if (best_pixel.pixel != nullptr && !best_from_preprobe) {
        ++diamond_state.dense.rank_compared;
      }
    }
    if (probationary_span_candidate) {
      ++diamond_state.comparator.span_trial_legal;
    }
    const uint64_t hpwl_band_idx = hpwl / hpwl_equivalence_band;
    const bool preprobe_shorter_equivalent = tail_repair && from_preprobe
                                             && best_from_preprobe
                                             && hpwl_band_idx == best_hpwl_band_idx
                                             && candidate_distance
                                                    + preprobe_shorter_guard
                                                < best_distance
                                             && score
                                                    <= best_score
                                                           + preprobe_score_guard;
    const bool bfs_shorter_equivalent
        = !tail_repair && !from_preprobe && !best_from_preprobe
          && diamond_design_utilization_ >= 80.0
          && diamond_design_utilization_ < 90.0
          && !diamondSourceRuntime()[static_cast<int>(
                 DiamondSourceFamily::kSpanContraction)]
                  .enabled
          && hpwl_band_idx == best_hpwl_band_idx
          && candidate_distance + preprobe_shorter_guard < best_distance
          && score <= best_score + preprobe_score_guard / 2.0;
    if (mid_util_bfs_candidate && best_pixel.pixel != nullptr) {
      ++diamond_state.comparator.bfs_compared;
    }
    const bool bfs_hpwl_margin_replacement
        = mid_util_bfs_candidate && best_pixel.pixel != nullptr
          && hpwl + bfs_hpwl_margin_guard < best_hpwl
          && candidate_distance <= best_distance + bfs_distance_guard
          && candidate_row_pressure <= best_row_pressure + bfs_row_pressure_guard
          && score <= best_score + bfs_score_guard;
    DiamondSourceState::DenseFragmentMetric candidate_fragment_metric;
    bool candidate_fragmentation_known = false;
    DiamondSourceState::DenseFragmentMetric candidate_pattern_metric;
    bool candidate_pattern_metric_known = false;
    DiamondSourceState::DenseGapShape candidate_gap_shape;
    bool candidate_gap_shape_known = false;
    const bool dense_same_band_candidate
        = high_util_dense_candidate && best_pixel.pixel != nullptr
          && !best_from_preprobe && hpwl_band_idx == best_hpwl_band_idx
          && hpwl <= best_hpwl + dense_hpwl_tie_guard;
    if (high_util_dense_candidate && best_pixel.pixel != nullptr
        && !best_from_preprobe && !dense_same_band_candidate) {
      ++diamond_state.dense.rank_fragment_avoided_band;
    }
    const bool dense_guard_candidate
        = dense_same_band_candidate
          && candidate_distance <= best_distance + dense_distance_tie_guard
          && candidate_row_pressure
                 <= best_row_pressure + dense_row_pressure_tie_guard
          && score <= best_score + dense_score_tie_guard;
    if (dense_same_band_candidate && !dense_guard_candidate) {
      ++diamond_state.dense.rank_fragment_avoided_guard;
    }
    const auto& getCandidateFragmentMetric = [&]()
        -> const DiamondSourceState::DenseFragmentMetric& {
      if (!candidate_fragmentation_known) {
        candidate_fragment_metric = denseCoreFragmentMetric(candidate);
        candidate_fragmentation_known = true;
      }
      return candidate_fragment_metric;
    };
    const auto& getBestFragmentMetric = [&]()
        -> const DiamondSourceState::DenseFragmentMetric& {
      if (!best_fragmentation_known && best_grid.x.v >= 0 && best_grid.y.v >= 0
          && best_pixel.pixel != nullptr && !best_from_preprobe
          && diamond_design_utilization_ >= 90.0) {
        best_fragment_metric = denseCoreFragmentMetric(best_grid);
        best_fragmentation_known = true;
      }
      return best_fragment_metric;
    };
    const auto& getCandidateGapShape = [&]()
        -> const DiamondSourceState::DenseGapShape& {
      if (!candidate_gap_shape_known) {
        candidate_gap_shape = denseSingleRowGapShape(candidate);
        candidate_gap_shape_known = true;
      }
      return candidate_gap_shape;
    };
    const auto& getBestGapShape = [&]()
        -> const DiamondSourceState::DenseGapShape& {
      if (!best_gap_shape_known && best_grid.x.v >= 0 && best_grid.y.v >= 0
          && best_pixel.pixel != nullptr && !best_from_preprobe
          && diamond_design_utilization_ >= 90.0) {
        best_gap_shape = denseSingleRowGapShape(best_grid);
        best_gap_shape_known = true;
      }
      return best_gap_shape;
    };
    const auto& getCandidatePatternMetric = [&]()
        -> const DiamondSourceState::DenseFragmentMetric& {
      if (!candidate_pattern_metric_known) {
        if (cell_height_rows == 1) {
          candidate_pattern_metric
              = denseSingleRowPatternMetric(getCandidateGapShape());
        } else {
          candidate_pattern_metric = denseFullFragmentMetric(candidate);
        }
        candidate_pattern_metric_known = true;
      }
      return candidate_pattern_metric;
    };
    const auto& getBestPatternMetric = [&]()
        -> const DiamondSourceState::DenseFragmentMetric& {
      if (!best_pattern_metric_known && best_grid.x.v >= 0 && best_grid.y.v >= 0
          && best_pixel.pixel != nullptr && !best_from_preprobe
          && diamond_design_utilization_ >= 90.0) {
        if (cell_height_rows == 1) {
          best_pattern_metric = denseSingleRowPatternMetric(getBestGapShape());
        } else {
          best_pattern_metric = denseFullFragmentMetric(best_grid);
        }
        best_pattern_metric_known = true;
      }
      return best_pattern_metric;
    };
    const int dense_pattern_free_gap_guard = grid_->getSiteWidth().v * 4;
    const int dense_pattern_fragment_guard = grid_->getSiteWidth().v * 2;
    const int dense_pattern_major_slack = grid_->getSiteWidth().v;
    const bool dense_fragment_equivalent_replacement
        = dense_guard_candidate
          && getCandidateFragmentMetric().fragmentation_dbu + dense_fragment_guard
                 < getBestFragmentMetric().fragmentation_dbu;
    bool dense_shape_transition_replacement = false;
    bool dense_pattern_transition_replacement = false;
    if (dense_guard_candidate && !dense_fragment_equivalent_replacement) {
      if (cell_height_rows != 1 || cell_width_sites < 3) {
        ++diamond_state.dense.rank_pattern_reject_width;
      } else {
        const auto best_edge_signature = denseSingleRowEdgeSignature(best_grid);
        if (!best_edge_signature.isSplit()) {
          ++diamond_state.dense.rank_pattern_reject_source;
        } else {
          const auto candidate_edge_signature
              = denseSingleRowEdgeSignature(candidate);
          if (!candidate_edge_signature.isEdge()) {
            ++diamond_state.dense.rank_pattern_reject_candidate;
          } else {
            const auto& best_gap_shape = getBestGapShape();
            const auto& candidate_gap_shape = getCandidateGapShape();
            ++diamond_state.dense.rank_shape_evaluated;
            if (hpwl > best_hpwl) {
              ++diamond_state.dense.rank_shape_reject_hpwl;
            } else {
              dense_shape_transition_replacement
                  = candidate_gap_shape.isEdge() && best_gap_shape.isSplit()
                    && getCandidateFragmentMetric().fragmentation_dbu
                           + dense_pattern_fragment_guard
                           < getBestFragmentMetric().fragmentation_dbu
                    && candidate_gap_shape.min_gap_dbu
                           + dense_shape_minor_gap_guard
                           < best_gap_shape.min_gap_dbu
                    && candidate_gap_shape.max_gap_dbu
                           > best_gap_shape.max_gap_dbu
                                 + dense_shape_major_gap_guard;
              if (!dense_shape_transition_replacement) {
                ++diamond_state.dense.rank_shape_reject_gap;
              }
            }
            const bool dense_pattern_gap_candidate
                = candidate_gap_shape.min_gap_dbu
                       + dense_shape_minor_gap_guard
                       < best_gap_shape.min_gap_dbu
                  && candidate_gap_shape.max_gap_dbu
                         + dense_pattern_major_slack
                         >= best_gap_shape.max_gap_dbu;
            if (!dense_pattern_gap_candidate) {
              ++diamond_state.dense.rank_pattern_reject_gap;
            } else {
              ++diamond_state.dense.rank_pattern_exact_evaluated;
              dense_pattern_transition_replacement
                  = getBestPatternMetric().split_rows > 0
                    && getCandidatePatternMetric().split_rows == 0
                    && getCandidatePatternMetric().edge_rows
                           >= getBestPatternMetric().edge_rows
                    && getCandidatePatternMetric().fragmentation_dbu
                           + dense_pattern_fragment_guard
                           < getBestPatternMetric().fragmentation_dbu
                    && getCandidatePatternMetric().free_gap_dbu
                           + dense_pattern_free_gap_guard
                           < getBestPatternMetric().free_gap_dbu;
              if (!dense_pattern_transition_replacement) {
                ++diamond_state.dense.rank_pattern_reject_exact;
              }
            }
          }
        }
      }
    }
    const bool dense_shape_novel_replacement
        = dense_shape_transition_replacement
          && !dense_pattern_transition_replacement;
    if (score < best_score
        || preprobe_shorter_equivalent
        || bfs_shorter_equivalent
        || bfs_hpwl_margin_replacement
        || dense_fragment_equivalent_replacement
        || dense_shape_transition_replacement
        || dense_pattern_transition_replacement
        || (score == best_score
            && (best_pixel.pixel == nullptr
                || candidate_distance < best_distance))) {
      if (bfs_hpwl_margin_replacement) {
        ++diamond_state.comparator.bfs_hpwl_replaced;
        diamond_state.comparator.bfs_hpwl_before += best_hpwl;
        diamond_state.comparator.bfs_hpwl_after += hpwl;
      }
      if (bfs_shorter_equivalent) {
        ++diamond_state.comparator.bfs_band_replaced;
      }
      if (probationary_span_candidate) {
        ++diamond_state.comparator.span_trial_replaced;
      }
      if (dense_fragment_equivalent_replacement) {
        ++diamond_state.dense.rank_selected;
        ++diamond_state.dense.rank_replaced;
      }
      if (dense_pattern_transition_replacement) {
        ++diamond_state.dense.rank_selected;
        ++diamond_state.dense.rank_replaced;
        ++diamond_state.dense.rank_pattern_replaced;
      }
      if (dense_shape_novel_replacement) {
        ++diamond_state.dense.rank_selected;
        ++diamond_state.dense.rank_replaced;
        ++diamond_state.dense.rank_shape_selected;
      }
      if (dense_fragment_equivalent_replacement || dense_shape_transition_replacement
          || dense_pattern_transition_replacement) {
        const auto& best_report_metric = getBestPatternMetric();
        const auto& candidate_report_metric = getCandidatePatternMetric();
        const auto& best_gap_shape = getBestGapShape();
        const auto& candidate_gap_shape = getCandidateGapShape();
        diamond_state.last_dense_candidate = {
            .active = true,
            .pressure_selected = false,
            .pressure_over_hpwl = false,
            .rank_selected = true,
            .shape_selected = dense_shape_novel_replacement,
            .candidate_grid_x = candidate.x.v,
            .candidate_grid_y = candidate.y.v,
            .source_hpwl = best_hpwl,
            .candidate_hpwl = hpwl,
            .source_pressure = best_row_pressure,
            .candidate_pressure = candidate_row_pressure,
            .source_distance = best_distance,
            .candidate_distance = candidate_distance,
            .source_fragmentation = getBestFragmentMetric().fragmentation_dbu,
            .candidate_fragmentation
                = getCandidateFragmentMetric().fragmentation_dbu,
            .source_free_gap = best_report_metric.free_gap_dbu,
            .candidate_free_gap = candidate_report_metric.free_gap_dbu,
            .source_edge_rows = best_report_metric.edge_rows,
            .candidate_edge_rows = candidate_report_metric.edge_rows,
            .source_split_rows = best_report_metric.split_rows,
            .candidate_split_rows = candidate_report_metric.split_rows,
            .source_minor_gap = best_gap_shape.min_gap_dbu,
            .candidate_minor_gap = candidate_gap_shape.min_gap_dbu,
            .source_major_gap = best_gap_shape.max_gap_dbu,
            .candidate_major_gap = candidate_gap_shape.max_gap_dbu,
            .cell_width_sites = cell_width_sites,
            .cell_height_rows = cell_height_rows};
      }
      best_score = score;
      best_hpwl = hpwl;
      best_hpwl_band_idx = hpwl_band_idx;
      best_distance = candidate_distance;
      best_row_pressure = candidate_row_pressure;
      best_fragment_metric = candidate_fragmentation_known
                                 ? candidate_fragment_metric
                                 : DiamondSourceState::DenseFragmentMetric{};
      best_fragmentation_known = candidate_fragmentation_known;
      best_pattern_metric = candidate_pattern_metric_known
                                ? candidate_pattern_metric
                                : DiamondSourceState::DenseFragmentMetric{};
      best_pattern_metric_known = candidate_pattern_metric_known;
      best_gap_shape = candidate_gap_shape_known
                           ? candidate_gap_shape
                           : DiamondSourceState::DenseGapShape{};
      best_gap_shape_known = candidate_gap_shape_known;
      best_grid = candidate;
      best_from_preprobe = from_preprobe;
      diamondLastSourceCandidate()
          = from_preprobe ? candidate_source_info : DiamondSourceCandidateInfo{};
      best_pixel = PixelPt(grid_->gridPixel(candidate.x, candidate.y),
                           candidate.x,
                           candidate.y);
    }
    return true;
  };

  const bool enable_wide_two_sided_net_anchors
      = diamond_design_utilization_ < 80.0;
  const bool enable_mid_util_source_recovery
      = diamond_design_utilization_ >= 80.0
        && diamond_design_utilization_ < 90.0;
  const bool enable_dense_strong_preprobes = false;
  const int wide_preprobe_distance_limit
      = std::max(grid_->getSiteWidth().v * 512, search_slack * 48);
  const int mid_preprobe_distance_limit
      = std::max(grid_->getSiteWidth().v * 48, search_slack * 6);
  const int dense_preprobe_distance_limit
      = std::max(grid_->getSiteWidth().v * 8, search_slack * 2);
  const DbuX source_left = gridToDbu(center.x, grid_->getSiteWidth());
  const DbuY source_bottom = grid_->gridYToDbu(center.y);
  const DbuX current_left = cell->getLeft();
  const DbuY current_bottom = cell->getBottom();
  const uint64_t source_hpwl = affected_edges.empty()
                                   ? 0
                                   : affectedHpwlAt(affected_edges,
                                                    cell,
                                                    source_left,
                                                    source_bottom);
  const uint64_t current_hpwl = affected_edges.empty()
                                    ? 0
                                    : affectedHpwlAt(affected_edges,
                                                     cell,
                                                     current_left,
                                                     current_bottom);
  const uint64_t mid_preprobe_min_delta = std::max<uint64_t>(
      static_cast<uint64_t>(grid_->getSiteWidth().v * 4),
      std::max<uint64_t>(1, std::max(source_hpwl, current_hpwl) / 256));
  const uint64_t dense_preprobe_min_delta = std::max<uint64_t>(
      static_cast<uint64_t>(grid_->getSiteWidth().v * 32),
      std::max<uint64_t>(1, std::max(source_hpwl, current_hpwl) / 48));
  const int cell_half_width = cell->getWidth().v / 2;
  const int cell_half_height = cell->getHeight().v / 2;
  auto addPreProbeCandidate = [&](const DbuPt& candidate_pt,
                                  const DiamondSourceFamily family,
                                  const uint64_t reference_hpwl,
                                  const int state_rank,
                                  const int kind_rank,
                                  const int distance_limit,
                                  const uint64_t min_predicted_hpwl_delta) {
    if (!isDiamondSourceFamilyEnabled(family)) {
      return;
    }
    const GridPt candidate = legalGridPt(cell, candidate_pt);
    if (candidate.x < x_min || candidate.x > x_max || candidate.y < y_min
        || candidate.y > y_max) {
      return;
    }
    const DbuX candidate_x = gridToDbu(candidate.x, grid_->getSiteWidth());
    const DbuY candidate_y = grid_->gridYToDbu(candidate.y);
    const uint64_t candidate_hpwl
        = affectedHpwlAt(affected_edges, cell, candidate_x, candidate_y);
    if (candidate_hpwl >= reference_hpwl) {
      return;
    }
    const uint64_t predicted_hpwl_delta = reference_hpwl - candidate_hpwl;
    if (predicted_hpwl_delta < min_predicted_hpwl_delta) {
      return;
    }
    const int source_distance = calcDist(center, candidate);
    if (source_distance > distance_limit) {
      return;
    }
    const DiamondGainBand gain_band = gainBandFor(reference_hpwl, candidate_hpwl);
    const DiamondDistanceBand distance_band = distanceBandFor(source_distance);
    if (!diamondSourceRuntime()[static_cast<int>(family)].positive_evidence
        && (gain_band == DiamondGainBand::kWeak
            || distance_band == DiamondDistanceBand::kFar)) {
      return;
    }
    auto [it, inserted] = preprobe_candidates.emplace(
        candidate,
        PreProbeCandidate{.pt = candidate,
                          .family = family,
                          .gain_band = gain_band,
                          .distance_band = distance_band,
                          .reference_hpwl = reference_hpwl,
                          .candidate_hpwl = candidate_hpwl,
                          .predicted_hpwl_delta
                          = static_cast<int64_t>(predicted_hpwl_delta),
                          .state_rank = state_rank,
                          .source_distance = source_distance,
                          .kind_rank = kind_rank});
    if (!inserted) {
      const auto incumbent_key = std::tuple(
          it->second.candidate_hpwl / hpwl_equivalence_band,
          it->second.source_distance,
          diamondSourceRuntime()[static_cast<int>(it->second.family)]
                  .positive_evidence
              ? 0
              : 1,
          it->second.kind_rank,
          it->second.state_rank,
          it->second.candidate_hpwl,
          -it->second.predicted_hpwl_delta);
      const auto challenger_key = std::tuple(candidate_hpwl / hpwl_equivalence_band,
                                             source_distance,
                                             diamondSourceRuntime()[static_cast<int>(
                                                 family)]
                                                     .positive_evidence
                                                 ? 0
                                                 : 1,
                                             kind_rank,
                                             state_rank,
                                             candidate_hpwl,
                                             -static_cast<int64_t>(
                                                 predicted_hpwl_delta));
      if (challenger_key < incumbent_key) {
        it->second.family = family;
        it->second.gain_band = gain_band;
        it->second.distance_band = distance_band;
        it->second.reference_hpwl = reference_hpwl;
        it->second.candidate_hpwl = candidate_hpwl;
        it->second.predicted_hpwl_delta = predicted_hpwl_delta;
        it->second.state_rank = state_rank;
        it->second.source_distance = source_distance;
        it->second.kind_rank = kind_rank;
      }
    }
  };
  auto seedTwoSidedNetAnchors = [&](const DbuX reference_left,
                                    const DbuY reference_bottom,
                                    const DiamondSourceFamily family,
                                    const uint64_t reference_hpwl,
                                    const int state_rank,
                                    const int distance_limit,
                                    const uint64_t min_predicted_hpwl_delta) {
    const int reference_center_x = (reference_left + cell->getWidth() / DbuX{2}).v;
    const int reference_center_y
        = (reference_bottom + cell->getHeight() / DbuY{2}).v;
    for (const CachedAffectedEdge& edge : affected_edges) {
      if (!edge.has_static_pin) {
        continue;
      }

      const int moved_min_x = reference_center_x + edge.moved_min_offset_x;
      const int moved_max_x = reference_center_x + edge.moved_max_offset_x;
      const int moved_min_y = reference_center_y + edge.moved_min_offset_y;
      const int moved_max_y = reference_center_y + edge.moved_max_offset_y;

      std::vector<DbuX> x_targets;
      std::vector<DbuY> y_targets;
      if (moved_min_x < edge.static_min_x) {
        x_targets.push_back(
            DbuX{edge.static_min_x - edge.moved_min_offset_x - cell_half_width});
      }
      if (moved_max_x > edge.static_max_x) {
        x_targets.push_back(
            DbuX{edge.static_max_x - edge.moved_max_offset_x - cell_half_width});
      }
      if (moved_min_y < edge.static_min_y) {
        y_targets.push_back(DbuY{edge.static_min_y - edge.moved_min_offset_y
                                 - cell_half_height});
      }
      if (moved_max_y > edge.static_max_y) {
        y_targets.push_back(DbuY{edge.static_max_y - edge.moved_max_offset_y
                                 - cell_half_height});
      }

      for (const DbuX target_x : x_targets) {
        addPreProbeCandidate({target_x, reference_bottom},
                             family,
                             reference_hpwl,
                             state_rank,
                             0,
                             distance_limit,
                             min_predicted_hpwl_delta);
      }
      for (const DbuY target_y : y_targets) {
        addPreProbeCandidate({reference_left, target_y},
                             family,
                             reference_hpwl,
                             state_rank,
                             0,
                             distance_limit,
                             min_predicted_hpwl_delta);
      }
      for (const DbuX target_x : x_targets) {
        for (const DbuY target_y : y_targets) {
          addPreProbeCandidate({target_x, target_y},
                               family,
                               reference_hpwl,
                               state_rank,
                               0,
                               distance_limit,
                               min_predicted_hpwl_delta);
        }
      }
    }
  };
  auto seedSpanContractionProbes = [&](const DbuX reference_left,
                                       const DbuY reference_bottom,
                                       const DiamondSourceFamily family,
                                       const uint64_t reference_hpwl,
                                       const int state_rank,
                                       const int distance_limit,
                                       const uint64_t min_predicted_hpwl_delta,
                                       const bool require_dual_axis) {
    const int reference_center_x = (reference_left + cell->getWidth() / DbuX{2}).v;
    const int reference_center_y
        = (reference_bottom + cell->getHeight() / DbuY{2}).v;
    for (const CachedAffectedEdge& edge : affected_edges) {
      if (!edge.has_static_pin || edge.static_pin_count < 2
          || edge.totalPinCount() < 3) {
        continue;
      }

      const int static_span_x = std::max(0, edge.static_max_x - edge.static_min_x);
      const int static_span_y = std::max(0, edge.static_max_y - edge.static_min_y);
      const int moved_min_x = reference_center_x + edge.moved_min_offset_x;
      const int moved_max_x = reference_center_x + edge.moved_max_offset_x;
      const int moved_min_y = reference_center_y + edge.moved_min_offset_y;
      const int moved_max_y = reference_center_y + edge.moved_max_offset_y;
      const int x_near_window = std::max(grid_->getSiteWidth().v * 4,
                                         static_span_x / 6);
      const int y_near_window = std::max(search_slack * 2, static_span_y / 6);
      const int x_interior_step
          = std::max(grid_->getSiteWidth().v,
                     std::min(grid_->getSiteWidth().v * 6,
                              std::max(grid_->getSiteWidth().v,
                                       static_span_x / 12)));
      const int y_interior_step
          = std::max(search_slack / 2,
                     std::min(search_slack * 4,
                              std::max(search_slack / 2, static_span_y / 10)));
      std::vector<DbuX> x_targets;
      std::vector<DbuY> y_targets;
      auto pushUniqueX = [&](const DbuX target_x) {
        if (std::find(x_targets.begin(), x_targets.end(), target_x)
            == x_targets.end()) {
          x_targets.push_back(target_x);
        }
      };
      auto pushUniqueY = [&](const DbuY target_y) {
        if (std::find(y_targets.begin(), y_targets.end(), target_y)
            == y_targets.end()) {
          y_targets.push_back(target_y);
        }
      };
      if (moved_min_x <= edge.static_min_x + x_near_window) {
        pushUniqueX(
            DbuX{edge.static_min_x + x_interior_step - edge.moved_min_offset_x
                 - cell_half_width});
      }
      if (moved_max_x >= edge.static_max_x - x_near_window) {
        pushUniqueX(
            DbuX{edge.static_max_x - x_interior_step - edge.moved_max_offset_x
                 - cell_half_width});
      }
      if (moved_min_y <= edge.static_min_y + y_near_window) {
        pushUniqueY(
            DbuY{edge.static_min_y + y_interior_step - edge.moved_min_offset_y
                 - cell_half_height});
      }
      if (moved_max_y >= edge.static_max_y - y_near_window) {
        pushUniqueY(
            DbuY{edge.static_max_y - y_interior_step - edge.moved_max_offset_y
                 - cell_half_height});
      }
      if (require_dual_axis && (x_targets.empty() || y_targets.empty())) {
        continue;
      }

      for (const DbuX target_x : x_targets) {
        addPreProbeCandidate({target_x, reference_bottom},
                             family,
                             reference_hpwl,
                             state_rank,
                             1,
                             distance_limit,
                             min_predicted_hpwl_delta);
      }
      for (const DbuY target_y : y_targets) {
        addPreProbeCandidate({reference_left, target_y},
                             family,
                             reference_hpwl,
                             state_rank,
                             1,
                             distance_limit,
                             min_predicted_hpwl_delta);
      }
      for (const DbuX target_x : x_targets) {
        for (const DbuY target_y : y_targets) {
          addPreProbeCandidate({target_x, target_y},
                               family,
                               reference_hpwl,
                               state_rank,
                               1,
                               distance_limit,
                               min_predicted_hpwl_delta);
        }
      }
    }
  };

  // Bundle 15's dense anchor scan proved useful as trace evidence, but it did
  // not produce parent-worthy committed moves. Keep bundle 16 on the iter06
  // quality path and use dense consistency ranking on existing candidates
  // instead of widening dense probes again.
  const bool enable_dense_pressure_stream = false;
  if (enable_dense_pressure_stream) {
    auto& dense = diamond_state.dense;
    ++dense.eligible_cells;
    DbuPt dense_anchor_pt;
    if (affectedHpwlAnchor(affected_edges, cell, dense_anchor_pt)) {
      ++dense.anchor_ready_cells;
      GridPt dense_anchor = legalGridPt(cell, dense_anchor_pt);
      dense_anchor.x = max(x_min, min(x_max, dense_anchor.x));
      dense_anchor.y = max(y_min, min(y_max, dense_anchor.y));
      const uint64_t dense_anchor_hpwl = affectedHpwlAt(
          affected_edges,
          cell,
          gridToDbu(dense_anchor.x, grid_->getSiteWidth()),
          grid_->gridYToDbu(dense_anchor.y));
      const double dense_anchor_gain_ratio
          = source_hpwl > dense_anchor_hpwl && source_hpwl > 0
                ? static_cast<double>(source_hpwl - dense_anchor_hpwl)
                      / static_cast<double>(source_hpwl)
                : 0.0;
      const int source_pressure = rowPressureDbu(center);
      const int anchor_pressure = rowPressureDbu(dense_anchor);
      const int pressure_context = std::max(source_pressure, anchor_pressure);
      const int anchor_distance = calcDist(center, dense_anchor);
      const int dense_window_guard
          = grid_->getSiteWidth().v
            * (dense_anchor_gain_ratio >= 0.05 ? 32
               : dense_anchor_gain_ratio >= 0.03 ? 24
                                                 : 16);
      const bool high_pressure_window
          = anchor_distance <= dense_window_guard + search_slack * 2;
      const bool meaningful_anchor_gain = dense_anchor_gain_ratio >= 0.012;
      if (high_pressure_window) {
        ++dense.window_gate_cells;
      }
      if (meaningful_anchor_gain) {
        ++dense.gain_gate_cells;
      }
      if (high_pressure_window && meaningful_anchor_gain) {
        std::vector<GridY> dense_probe_rows;
        const int dense_row_budget = dense_anchor_gain_ratio >= 0.04 ? 3 : 2;
        auto addDenseProbeRow = [&](const GridY row) {
          if (row < y_min || row > y_max) {
            return;
          }
          if (abs(row - center.y).v > dense_row_budget
              && abs(row - dense_anchor.y).v > dense_row_budget) {
            return;
          }
          if (std::find(dense_probe_rows.begin(), dense_probe_rows.end(), row)
              == dense_probe_rows.end()) {
            dense_probe_rows.push_back(row);
          }
        };
        for (const int row_offset : {0, -1, 1, -2, 2}) {
          addDenseProbeRow(GridY{center.y.v + row_offset});
          addDenseProbeRow(GridY{dense_anchor.y.v + row_offset});
        }
        std::ranges::stable_sort(dense_probe_rows,
                                 [&](const GridY left, const GridY right) {
                                   const GridPt left_probe{dense_anchor.x, left};
                                   const GridPt right_probe{dense_anchor.x, right};
                                   const auto left_key
                                       = std::tuple(rowPressureDbu(left_probe),
                                                    abs(left - dense_anchor.y).v,
                                                    abs(left - center.y).v);
                                   const auto right_key
                                       = std::tuple(rowPressureDbu(right_probe),
                                                    abs(right - dense_anchor.y).v,
                                                    abs(right - center.y).v);
                                   return left_key < right_key;
                                 });
        const int dense_probe_distance_limit
            = dense_window_guard + search_slack * 2;
        const uint64_t dense_probe_hpwl_guard = std::max<uint64_t>(
            static_cast<uint64_t>(grid_->getSiteWidth().v * 24),
            std::max<uint64_t>(1, hpwl_equivalence_band / 2));
        DenseProbeCandidate hpwl_best;
        DenseProbeCandidate pressure_best;
        bool have_hpwl_best = false;
        bool have_pressure_best = false;
        int dense_legal_candidates = 0;
        auto considerDenseProbe = [&](const GridPt& probe,
                                     const bool from_interval) {
          if (probe.x < x_min || probe.x > x_max || probe.y < y_min
              || probe.y > y_max) {
            return false;
          }
          if (calcDist(center, probe) > dense_probe_distance_limit
              || !isFeasible(probe)) {
            return false;
          }

          const DbuX probe_x = gridToDbu(probe.x, grid_->getSiteWidth());
          const DbuY probe_y = grid_->gridYToDbu(probe.y);
          const uint64_t probe_hpwl
              = affectedHpwlAt(affected_edges, cell, probe_x, probe_y);
          const int probe_pressure = rowPressureDbu(probe);
          const int probe_distance = calcDist(center, probe);
          const DenseProbeCandidate candidate{.pt = probe,
                                              .hpwl = probe_hpwl,
                                              .row_pressure = probe_pressure,
                                              .distance = probe_distance};
          if (from_interval) {
            ++dense.interval_probe_candidates;
          } else {
            ++dense.fixed_probe_candidates;
          }
          if (!have_hpwl_best
              || std::tuple(candidate.hpwl,
                            candidate.distance,
                            candidate.row_pressure)
                     < std::tuple(hpwl_best.hpwl,
                                  hpwl_best.distance,
                                  hpwl_best.row_pressure)) {
            hpwl_best = candidate;
            have_hpwl_best = true;
          }
          if (!have_pressure_best
              || std::tuple(candidate.row_pressure,
                            candidate.hpwl / dense_probe_hpwl_guard,
                            candidate.distance,
                            candidate.hpwl)
                     < std::tuple(pressure_best.row_pressure,
                                  pressure_best.hpwl / dense_probe_hpwl_guard,
                                  pressure_best.distance,
                                  pressure_best.hpwl)) {
            pressure_best = candidate;
            have_pressure_best = true;
          }
          ++dense_legal_candidates;
          return true;
        };
        for (const GridY row : dense_probe_rows) {
          for (const int site_offset : {0, -1, 1, -2, 2, -4, 4, -8, 8}) {
            const GridPt probe{GridX{dense_anchor.x.v + site_offset}, row};
            considerDenseProbe(probe, false);
            if (dense_legal_candidates >= 12) {
              break;
            }
          }
          if (dense_legal_candidates >= 12) {
            break;
          }
        }

        if (dense_legal_candidates < 2) {
          ++dense.interval_probe_cells;
          const int interval_site_budget = dense_anchor_gain_ratio >= 0.05 ? 24
                                              : dense_anchor_gain_ratio >= 0.03 ? 20
                                                                                : 16;
          for (const GridY row : dense_probe_rows) {
            ++dense.interval_rows;
            bool left_found = false;
            bool right_found = false;
            for (int site_step = 9; site_step <= interval_site_budget;
                 ++site_step) {
              if (!left_found) {
                ++dense.interval_site_checks;
                left_found = considerDenseProbe(
                    {GridX{dense_anchor.x.v - site_step}, row},
                    true);
                if (dense_legal_candidates >= 12) {
                  break;
                }
              }
              if (!right_found) {
                ++dense.interval_site_checks;
                right_found = considerDenseProbe(
                    {GridX{dense_anchor.x.v + site_step}, row},
                    true);
                if (dense_legal_candidates >= 12) {
                  break;
                }
              }
              if (left_found && right_found) {
                break;
              }
            }
            if (dense_legal_candidates >= 12) {
              break;
            }
          }
        }

        if (have_hpwl_best && have_pressure_best) {
          ++dense.legal_probe_cells;
          ++dense.active_cells;
          bool pressure_selected = false;
          bool pressure_over_hpwl = false;
          bool promote_dense_candidate = false;
          DenseProbeCandidate dense_selected = hpwl_best;
          const int pressure_relief
              = hpwl_best.row_pressure - pressure_best.row_pressure;
          const uint64_t hpwl_penalty
              = pressure_best.hpwl > hpwl_best.hpwl
                    ? pressure_best.hpwl - hpwl_best.hpwl
                    : 0;
          const int distance_penalty
              = pressure_best.distance - hpwl_best.distance;
          const int source_pressure_relief
              = pressure_context - pressure_best.row_pressure;
          const int pressure_relief_guard
              = std::max(grid_->getSiteWidth().v * 8, search_slack * 2);
          const uint64_t hpwl_penalty_guard = std::max<uint64_t>(
              static_cast<uint64_t>(grid_->getSiteWidth().v * 16),
              dense_probe_hpwl_guard);
          const int distance_penalty_guard
              = std::max(grid_->getSiteWidth().v * 6, search_slack * 2);
          if (pressure_best.pt != hpwl_best.pt
              && pressure_relief >= pressure_relief_guard
              && source_pressure_relief > 0
              && hpwl_penalty <= hpwl_penalty_guard
              && distance_penalty <= distance_penalty_guard
              && pressure_best.hpwl <= source_hpwl + hpwl_penalty_guard) {
            dense_selected = pressure_best;
            pressure_selected = true;
            pressure_over_hpwl = true;
            promote_dense_candidate = true;
          } else {
            const uint64_t hpwl_gain = source_hpwl > hpwl_best.hpwl
                                           ? source_hpwl - hpwl_best.hpwl
                                           : 0;
            const uint64_t hpwl_gain_guard = std::max<uint64_t>(
                static_cast<uint64_t>(grid_->getSiteWidth().v * 48),
                std::max<uint64_t>(1, hpwl_equivalence_band * 2));
            const int hpwl_distance_guard
                = std::max(grid_->getSiteWidth().v * 4, search_slack);
            const int row_pressure_slack
                = std::max(grid_->getSiteWidth().v * 4, search_slack);
            if (hpwl_gain >= hpwl_gain_guard
                && dense_anchor_gain_ratio >= 0.04
                && hpwl_best.distance <= dense_probe_distance_limit
                && hpwl_best.distance <= anchor_distance + hpwl_distance_guard
                && hpwl_best.row_pressure <= pressure_context + row_pressure_slack) {
              promote_dense_candidate = true;
            }
          }

          if (promote_dense_candidate) {
            if (pressure_selected) {
              ++dense.pressure_selected;
            } else {
              ++dense.hpwl_selected;
            }
            if (pressure_over_hpwl) {
              ++dense.pressure_over_hpwl_selected;
            }
            const double dense_selected_score
                = static_cast<double>(dense_selected.hpwl)
                  + kDiamondDisplacementWeight
                        * static_cast<double>(dense_selected.distance)
                  + kDiamondRowPressureWeight
                        * static_cast<double>(dense_selected.row_pressure);
            best_score = dense_selected_score;
            best_hpwl = dense_selected.hpwl;
            best_hpwl_band_idx = dense_selected.hpwl / hpwl_equivalence_band;
            best_distance = dense_selected.distance;
            best_row_pressure = dense_selected.row_pressure;
            best_from_preprobe = false;
            best_pixel = PixelPt(grid_->gridPixel(dense_selected.pt.x,
                                                  dense_selected.pt.y),
                                 dense_selected.pt.x,
                                 dense_selected.pt.y);
            scored_legal_candidates.insert(dense_selected.pt);
            feasibility_cache.emplace(dense_selected.pt, true);
            diamond_state.last_dense_candidate = {
                .active = true,
                .pressure_selected = pressure_selected,
                .pressure_over_hpwl = pressure_over_hpwl,
                .candidate_grid_x = dense_selected.pt.x.v,
                .candidate_grid_y = dense_selected.pt.y.v,
                .source_hpwl = source_hpwl,
                .candidate_hpwl = dense_selected.hpwl,
                .source_pressure = source_pressure,
                .candidate_pressure = dense_selected.row_pressure,
                .source_distance = 0,
                .candidate_distance = dense_selected.distance};
          } else {
            ++dense.pressure_guard_rejects;
          }
        }
      }
    }
  }

  if (enable_wide_two_sided_net_anchors) {
    seedTwoSidedNetAnchors(source_left,
                           source_bottom,
                           DiamondSourceFamily::kEndpoint,
                           source_hpwl,
                           0,
                           wide_preprobe_distance_limit,
                           1);
    if (current_left != source_left || current_bottom != source_bottom) {
      seedTwoSidedNetAnchors(current_left,
                             current_bottom,
                             DiamondSourceFamily::kEndpoint,
                             current_hpwl,
                             1,
                             wide_preprobe_distance_limit,
                             1);
    }
  } else if (enable_dense_strong_preprobes) {
    seedTwoSidedNetAnchors(source_left,
                           source_bottom,
                           DiamondSourceFamily::kEndpoint,
                           source_hpwl,
                           0,
                           dense_preprobe_distance_limit,
                           dense_preprobe_min_delta);
    if (current_left != source_left || current_bottom != source_bottom) {
      seedTwoSidedNetAnchors(current_left,
                             current_bottom,
                             DiamondSourceFamily::kEndpoint,
                             current_hpwl,
                             1,
                             dense_preprobe_distance_limit,
                             dense_preprobe_min_delta);
    }
  }
  if (enable_mid_util_source_recovery) {
    seedSpanContractionProbes(source_left,
                              source_bottom,
                              DiamondSourceFamily::kSpanContraction,
                              source_hpwl,
                              0,
                              mid_preprobe_distance_limit,
                              mid_preprobe_min_delta,
                              true);
    if (current_left != source_left || current_bottom != source_bottom) {
      seedSpanContractionProbes(current_left,
                                current_bottom,
                                DiamondSourceFamily::kSpanContraction,
                                current_hpwl,
                                1,
                                mid_preprobe_distance_limit,
                                mid_preprobe_min_delta,
                                true);
    }
  } else if (enable_dense_strong_preprobes) {
    seedSpanContractionProbes(source_left,
                              source_bottom,
                              DiamondSourceFamily::kSpanContraction,
                              source_hpwl,
                              0,
                              dense_preprobe_distance_limit,
                              dense_preprobe_min_delta,
                              true);
    if (current_left != source_left || current_bottom != source_bottom) {
      seedSpanContractionProbes(current_left,
                                current_bottom,
                                DiamondSourceFamily::kSpanContraction,
                                current_hpwl,
                                1,
                                dense_preprobe_distance_limit,
                                dense_preprobe_min_delta,
                                true);
    }
  }

  std::vector<PreProbeCandidate> ordered_preprobes;
  ordered_preprobes.reserve(preprobe_candidates.size());
  for (const auto& [_, candidate] : preprobe_candidates) {
    ordered_preprobes.push_back(candidate);
  }
  std::ranges::sort(ordered_preprobes,
                    [&](const PreProbeCandidate& left,
                        const PreProbeCandidate& right) {
                      return std::tuple(
                                 diamondSourceRuntime()[static_cast<int>(
                                     left.family)]
                                         .positive_evidence
                                     ? 0
                                     : 1,
                                 left.candidate_hpwl / hpwl_equivalence_band,
                                 left.source_distance,
                                 left.kind_rank,
                                 left.state_rank,
                                 left.candidate_hpwl,
                                 -left.predicted_hpwl_delta)
                             < std::tuple(
                                 diamondSourceRuntime()[static_cast<int>(
                                     right.family)]
                                         .positive_evidence
                                     ? 0
                                     : 1,
                                 right.candidate_hpwl / hpwl_equivalence_band,
                                 right.source_distance,
                                 right.kind_rank,
                                 right.state_rank,
                                 right.candidate_hpwl,
                                 -right.predicted_hpwl_delta);
                    });
  preprobe_seeded_candidates.reserve(ordered_preprobes.size());
  for (const PreProbeCandidate& candidate : ordered_preprobes) {
    preprobe_seeded_candidates.insert(candidate.pt);
  }
  const int net_anchor_probe_legal_limit = enable_dense_strong_preprobes
                                               ? 2
                                           : enable_mid_util_source_recovery
                                               ? (diamondSourceRuntime()[static_cast<int>(
                                                      DiamondSourceFamily::
                                                          kSpanContraction)]
                                                          .positive_evidence
                                                      ? 2
                                                      : 1)
                                                                             : 12;
  int net_anchor_legal_candidates = 0;
  for (const PreProbeCandidate& candidate : ordered_preprobes) {
    if (scoreCandidate(candidate.pt)
        && ++net_anchor_legal_candidates >= net_anchor_probe_legal_limit) {
      break;
    }
  }

  DbuPt anchor_pt;
  if (!tail_repair && diamond_design_utilization_ < 90.0
      && affectedHpwlAnchor(affected_edges, cell, anchor_pt)) {
    GridPt anchor = legalGridPt(cell, anchor_pt);
    anchor.x = max(x_min, min(x_max, anchor.x));
    anchor.y = max(y_min, min(y_max, anchor.y));
    constexpr int kAnchorProbeLegalLimit = 8;
    const DbuX anchor_x_dbu = gridToDbu(anchor.x, grid_->getSiteWidth());
    const DbuY anchor_y_dbu = grid_->gridYToDbu(anchor.y);
    const uint64_t center_hpwl
        = affectedHpwlAt(affected_edges,
                         cell,
                         gridToDbu(center.x, grid_->getSiteWidth()),
                         grid_->gridYToDbu(center.y));
    const uint64_t anchor_hpwl
        = affectedHpwlAt(affected_edges, cell, anchor_x_dbu, anchor_y_dbu);
    const double anchor_gain_ratio
        = center_hpwl > anchor_hpwl && center_hpwl > 0
              ? static_cast<double>(center_hpwl - anchor_hpwl)
                    / static_cast<double>(center_hpwl)
              : 0.0;
    int row_budget = diamond_design_utilization_ < 80.0 ? 3 : 2;
    if (anchor_gain_ratio > 0.08) {
      row_budget = diamond_design_utilization_ < 80.0 ? 24 : 8;
    } else if (anchor_gain_ratio > 0.04) {
      row_budget = diamond_design_utilization_ < 80.0 ? 14 : 5;
    } else if (anchor_gain_ratio > 0.015) {
      row_budget = diamond_design_utilization_ < 80.0 ? 8 : 3;
    }
    const int anchor_probe_distance_limit
        = std::max(grid_->getSiteWidth().v
                       * (anchor_gain_ratio > 0.08 ? 384 : 192),
                   search_slack * (row_budget + 6));
    int anchor_legal_candidates = 0;
    std::vector<GridY> probe_rows;
    auto addProbeRow = [&](const GridY row) {
      if (row < y_min || row > y_max) {
        return;
      }
      if (abs(row - center.y).v > row_budget) {
        return;
      }
      if (std::find(probe_rows.begin(), probe_rows.end(), row)
          == probe_rows.end()) {
        probe_rows.push_back(row);
      }
    };
    for (const int row_offset : {0, -1, 1, -2, 2}) {
      addProbeRow(GridY{center.y.v + row_offset});
    }
    for (const int row_offset : {0, -1, 1, -2, 2, -4, 4}) {
      addProbeRow(GridY{anchor.y.v + row_offset});
    }
    std::ranges::stable_sort(probe_rows, [&](const GridY left,
                                             const GridY right) {
      const GridPt left_probe{anchor.x, left};
      const GridPt right_probe{anchor.x, right};
      const auto left_key
          = std::tuple(rowPressureDbu(left_probe),
                       abs(left - center.y).v,
                       abs(left - anchor.y).v);
      const auto right_key
          = std::tuple(rowPressureDbu(right_probe),
                       abs(right - center.y).v,
                       abs(right - anchor.y).v);
      return left_key < right_key;
    });
    for (const GridY row : probe_rows) {
      for (const int site_offset : {0, -1, 1, -2, 2, -4, 4, -8, 8, -16, 16}) {
        const GridPt probe{GridX{anchor.x.v + site_offset}, row};
        if (calcDist(center, probe) > anchor_probe_distance_limit) {
          continue;
        }
        if (scoreCandidate(probe)
            && ++anchor_legal_candidates >= kAnchorProbeLegalLimit) {
          break;
        }
      }
      if (anchor_legal_candidates >= kAnchorProbeLegalLimit) {
        break;
      }
    }
  }

  const vector<GridPt> neighbors = {{GridX(-1), GridY(0)},
                                    {GridX(1), GridY(0)},
                                    {GridX(0), GridY(-1)},
                                    {GridX(0), GridY(1)}};
  while (!positionsHeap.empty()) {
    const PQ_entry current = positionsHeap.top();
    const GridPt nearest = current.p;
    positionsHeap.pop();
    if (first_legal_distance >= 0
        && current.manhattan_distance > first_legal_distance + search_slack) {
      break;
    }

    if (scoreCandidate(nearest)) {
      if (first_legal_distance < 0) {
        first_legal_distance = current.manhattan_distance;
      }
      ++legal_candidates;
      if (legal_candidates >= kMaxLegalCandidates) {
        break;
      }
    }

    // Put neighbors in the queue
    for (GridPt offset : neighbors) {
      GridPt neighbor = {nearest.x + offset.x, nearest.y + offset.y};
      // Check if it was already put in the queue
      if (visited.contains(neighbor)) {
        continue;
      }
      // Check limits
      if (neighbor.x < x_min || neighbor.x > x_max || neighbor.y < y_min
          || neighbor.y > y_max) {
        continue;
      }
      if (first_legal_distance >= 0
          && calcDist(center, neighbor) > first_legal_distance + search_slack) {
        continue;
      }

      visited.insert(neighbor);
      positionsHeap.push({.manhattan_distance = calcDist(center, neighbor),
                          .p = neighbor,
                          .sequence = sequence++});
    }
  }
  if (best_pixel.pixel) {
    const auto& dense_candidate = diamondSourceState().last_dense_candidate;
    if (dense_candidate.active
        && (best_pixel.x.v != dense_candidate.candidate_grid_x
            || best_pixel.y.v != dense_candidate.candidate_grid_y)) {
      diamondSourceState().last_dense_candidate = {};
    } else if (dense_candidate.active) {
      auto& dense = diamondSourceState().dense;
      if (dense_candidate.rank_selected) {
        ++dense.rank_returned;
      } else if (dense_candidate.pressure_selected) {
        ++dense.pressure_returned;
      } else {
        ++dense.hpwl_returned;
      }
      if (dense_candidate.pressure_over_hpwl) {
        ++dense.pressure_over_hpwl_returned;
      }
    }
    return best_pixel;
  }
  return PixelPt();
}

int Opendp::calcDist(GridPt p0, GridPt p1) const
{
  DbuY y_dist = abs(grid_->gridYToDbu(p0.y) - grid_->gridYToDbu(p1.y));
  DbuX x_dist = gridToDbu(abs(p0.x - p1.x), grid_->getSiteWidth());
  return sumXY(x_dist, y_dist);
}

int Opendp::diamondHpwlRepairPass()
{
  struct RepairCandidate
  {
    Node* cell = nullptr;
    uint64_t affected_hpwl = 0;
    int displacement = 0;
    int cell_id = 0;
  };

  vector<RepairCandidate> candidates;
  candidates.reserve(network_->getNumCells());
  std::vector<int> displacements;
  displacements.reserve(network_->getNumCells());
  for (auto& cell : network_->getNodes()) {
    if (cell->getType() != Node::CELL || cell->isFixed() || cell->isHold()
        || !cell->isPlaced() || cell->getDbInst() == nullptr
        || !cell->getDbInst()->getMaster()->isCore()) {
      continue;
    }
    const std::vector<CachedAffectedEdge> affected_edges
        = buildAffectedHpwlCache(cell.get());
    if (affected_edges.empty()) {
      continue;
    }
    const uint64_t affected_hpwl = affectedHpwlAt(
        affected_edges, cell.get(), cell->getLeft(), cell->getBottom());
    const int displacement = disp(cell.get());
    candidates.push_back({.cell = cell.get(),
                          .affected_hpwl = affected_hpwl,
                          .displacement = displacement,
                          .cell_id = cell->getId()});
    displacements.push_back(displacement);
  }

  std::ranges::sort(displacements);
  const int p90_displacement
      = displacements.empty()
            ? 0
            : displacements[std::min<size_t>(
                displacements.size() - 1, displacements.size() * 90 / 100)];
  const int p99_displacement
      = displacements.empty()
            ? 0
            : displacements[std::min<size_t>(
                displacements.size() - 1, displacements.size() * 99 / 100)];

  std::ranges::sort(candidates, [p90_displacement, p99_displacement](
                                    const RepairCandidate& left,
                                    const RepairCandidate& right) {
    const int left_percentile_band = left.displacement >= p99_displacement ? 2
                                     : left.displacement >= p90_displacement ? 1
                                                                             : 0;
    const int right_percentile_band = right.displacement >= p99_displacement
                                          ? 2
                                      : right.displacement >= p90_displacement ? 1
                                                                               : 0;
    if (left_percentile_band != right_percentile_band) {
      return left_percentile_band > right_percentile_band;
    }
    if (left_percentile_band == 2 && left.displacement != right.displacement) {
      return left.displacement > right.displacement;
    }
    if (left.affected_hpwl != right.affected_hpwl) {
      return left.affected_hpwl > right.affected_hpwl;
    }
    if (left.displacement != right.displacement) {
      return left.displacement > right.displacement;
    }
    return left.cell_id < right.cell_id;
  });

  const int inspect_limit
      = std::min<int>(candidates.size(),
                      std::max(128, std::min<int>(4096, candidates.size() / 20)));
  const int move_limit
      = std::min<int>(512, std::max(32, static_cast<int>(candidates.size() / 200)));
  int inspected = 0;
  int accepted = 0;
  int tail_inspected = 0;
  int tail_accepted = 0;
  bool tail_repair_enabled = true;
  uint64_t accepted_hpwl_before = 0;
  uint64_t accepted_hpwl_after = 0;
  uint64_t tail_hpwl_before = 0;
  uint64_t tail_hpwl_after = 0;

  for (int i = 0; i < inspect_limit && accepted < move_limit; ++i) {
    Node* cell = candidates[i].cell;
    if (cell == nullptr || !cell->isPlaced() || cell->isFixed()
        || cell->isHold()) {
      continue;
    }
    const std::vector<CachedAffectedEdge> affected_edges
        = buildAffectedHpwlCache(cell);
    if (affected_edges.empty()) {
      continue;
    }

    const GridPt original_grid = legalGridPt(cell, false);
    const GridPt old_grid{grid_->gridX(cell->getLeft()),
                          grid_->gridSnapDownY(cell->getBottom())};
    const uint64_t before_hpwl = affectedHpwlAt(
        affected_edges, cell, cell->getLeft(), cell->getBottom());
    const int before_distance = calcDist(original_grid, old_grid);
    const double before_score = static_cast<double>(before_hpwl)
                                + kRepairDisplacementWeight
                                      * static_cast<double>(before_distance);

    unplaceCell(cell);
    const bool tail_candidate = tail_repair_enabled
                                && before_distance >= p99_displacement;
    if (tail_candidate) {
      ++tail_inspected;
    }
    const PixelPt repair_site
        = diamondSearch(cell,
                        original_grid.x,
                        original_grid.y,
                        tail_candidate,
                        p90_displacement,
                        p99_displacement);
    recordDiamondSourceSelection(diamondLastSourceCandidate());
    bool keep_repair = false;
    uint64_t after_hpwl = before_hpwl;
    if (repair_site.pixel) {
      const DbuX repair_x = gridToDbu(repair_site.x, grid_->getSiteWidth());
      const DbuY repair_y = grid_->gridYToDbu(repair_site.y);
      after_hpwl = affectedHpwlAt(affected_edges, cell, repair_x, repair_y);
      const int after_distance
          = calcDist(original_grid, {repair_site.x, repair_site.y});
      const double tail_weight = before_distance >= p99_displacement ? 0.85
                                                                     : 0.35;
      const int before_tail = std::max(0, before_distance - p90_displacement);
      const int after_tail = std::max(0, after_distance - p90_displacement);
      const int distance_relief = before_distance - after_distance;
      const int hpwl_gain
          = before_hpwl > after_hpwl
                ? static_cast<int>(before_hpwl - after_hpwl)
                : 0;
      const double before_pareto_score
          = before_score + tail_weight * static_cast<double>(before_tail);
      const double after_score = static_cast<double>(after_hpwl)
                                 + kRepairDisplacementWeight
                                       * static_cast<double>(after_distance);
      const double after_pareto_score
          = after_score + tail_weight * static_cast<double>(after_tail);
      const bool moved = repair_site.x != old_grid.x || repair_site.y != old_grid.y;
      if (tail_candidate) {
        const int hpwl_guard = std::max(grid_->getSiteWidth().v / 4,
                                        std::max(before_tail / 16,
                                                 grid_->getSiteWidth().v / 8));
        const int distance_guard
            = std::max(grid_->getSiteWidth().v, before_tail / 6);
        const int dominance_guard
            = std::max(hpwl_guard, distance_relief / 2);
        keep_repair = moved
                      && distance_relief >= distance_guard
                      && after_tail < before_tail
                      && after_distance < before_distance
                      && hpwl_gain >= dominance_guard
                      && after_hpwl + hpwl_guard <= before_hpwl
                      && after_pareto_score
                             + std::max(grid_->getSiteWidth().v / 2,
                                        before_tail / 12)
                             < before_pareto_score;
      } else {
        keep_repair = moved && after_distance <= before_distance
                      && after_hpwl < before_hpwl
                      && after_score + grid_->getSiteWidth().v < before_score;
      }
    }

    if (keep_repair) {
      placeCell(cell, repair_site.x, repair_site.y);
      recordDiamondSourceOutcome(diamondLastSourceCandidate(), true);
      recordDenseCandidateCommit(diamondSourceState());
      ++accepted;
      accepted_hpwl_before += before_hpwl;
      accepted_hpwl_after += after_hpwl;
      if (tail_candidate) {
        ++tail_accepted;
        tail_hpwl_before += before_hpwl;
        tail_hpwl_after += after_hpwl;
        if (tail_hpwl_after >= tail_hpwl_before) {
          tail_repair_enabled = false;
        }
      }
    } else {
      recordDiamondSourceOutcome(diamondLastSourceCandidate(), false);
      recordDenseCandidateReject(diamondSourceState());
      placeCell(cell, old_grid.x, old_grid.y);
    }
    diamondLastSourceCandidate() = {};
    ++inspected;
  }

  logger_->metric("dpl_evolve__pipeline__diamond_repair_inspected", inspected);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_accepted", accepted);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_p90_disp_dbu",
                  p90_displacement);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_p99_disp_dbu",
                  p99_displacement);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_tail_inspected",
                  tail_inspected);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_tail_accepted",
                  tail_accepted);
  logger_->metric("dpl_evolve__pipeline__diamond_repair_tail_enabled_final",
                  tail_repair_enabled ? 1.0 : 0.0);
  logger_->info(DPL,
                1222,
                "DPL-Evolve bounded Diamond HPWL repair: inspected {}, "
                "accepted {}, affected HPWL {} -> {}, tail accepted {} "
                "({} -> {}), tail enabled {}, p90/p99 displacement {} / {} dbu.",
                inspected,
                accepted,
                accepted_hpwl_before,
                accepted_hpwl_after,
                tail_accepted,
                tail_hpwl_before,
                tail_hpwl_after,
                tail_repair_enabled,
                p90_displacement,
                p99_displacement);
  return accepted;
}

bool Opendp::canBePlaced(const Node* cell, GridX bin_x, GridY bin_y) const
{
  debugPrint(logger_,
             DPL,
             "place",
             3,
             " canBePlaced {} ({:4},{:4})",
             cell->name(),
             bin_x,
             bin_y);

  if (bin_y >= grid_->getRowCount()) {
    return false;
  }

  const GridX x_end = bin_x + grid_->gridWidth(cell);
  const GridY y_end
      = grid_->gridEndY(grid_->gridYToDbu(bin_y) + cell->getHeight());

  if (debug_observer_) {
    debug_observer_->binSearch(cell, bin_x, bin_y, x_end, y_end);
  }
  return checkPixels(cell, bin_x, bin_y, x_end, y_end);
}

bool Opendp::checkRegionOverlap(const Node* cell,
                                const GridX x,
                                const GridY y,
                                const GridX x_end,
                                const GridY y_end) const
{
  // TODO: Investigate the caching of this function
  // it is called with the same cell and x,y,x_end,y_end multiple times
  debugPrint(logger_,
             DPL,
             "region",
             1,
             "Checking region overlap for cell {} at x[{} {}] and y[{} {}]",
             cell->name(),
             x,
             x_end,
             y,
             y_end);
  const DbuX site_width = grid_->getSiteWidth();
  const bgBox queryBox(
      {gridToDbu(x, site_width).v, grid_->gridYToDbu(y).v},
      {gridToDbu(x_end, site_width).v - 1, grid_->gridYToDbu(y_end).v - 1});

  std::vector<bgBox> result;
  findOverlapInRtree(queryBox, result);

  if (cell->getRegion()) {
    if (result.size() == 1) {
      // the queryBox must be fully contained in the region or else there
      // might be a part of the cell outside of any region
      return boost::geometry::covered_by(queryBox, result[0]);
    }
    // if we are here, then the overlap size is either 0 or > 1
    // both are invalid. The overlap size should be 1
    return false;
  }
  // If the cell has a region, then the region's bounding box must
  // be fully contained by the cell's bounding box.
  return result.empty();
}

// Check all pixels are empty.
bool Opendp::checkPixels(const Node* cell,
                         const GridX x,
                         const GridY y,
                         const GridX x_end,
                         const GridY y_end) const
{
  if (x_end > grid_->getRowSiteCount()) {
    return false;
  }
  if (!checkRegionOverlap(cell, x, y, x_end, y_end)) {
    return false;
  }

  odb::dbSite* site = cell->getSite();
  for (GridY y1 = y; y1 < y_end; y1++) {
    const bool first_row = (y1 == y);
    for (GridX x1 = x; x1 < x_end; x1++) {
      const Pixel* pixel = grid_->gridPixel(x1, y1);
      if (pixel == nullptr || pixel->cell || !pixel->is_valid
          || (cell->inGroup() && pixel->group != cell->getGroup())
          || (!cell->inGroup() && pixel->group)
          || (first_row && !grid_->getSiteOrientation(x1, y1, site))) {
        return false;
      }
    }
  }

  if (disallow_one_site_gaps_) {
    // here we need to check for abutting first, if there is an abutting
    // cell then we continue as there is nothing wrong with it if there is
    // no abutting cell, we will then check cells at 1+ distances we only
    // need to check on the left and right sides
    const GridX x_begin = max(GridX{0}, x - 1);
    const GridY y_begin = max(GridY{0}, y);
    // inclusive search, so we don't add 1 to the end
    const GridX x_finish = min(x_end, grid_->getRowSiteCount() - 1);
    const GridY y_finish = min(y_end, grid_->getRowCount() - 1);

    auto isAbutted = [this](const GridX x, const GridY y) {
      const Pixel* pixel = grid_->gridPixel(x, y);
      return (pixel == nullptr || pixel->cell);
    };

    auto cellAtSite = [this](const GridX x, const GridY y) {
      const Pixel* pixel = grid_->gridPixel(x, y);
      return (pixel != nullptr && pixel->cell);
    };
    for (GridY y = y_begin; y < y_finish; ++y) {
      // left side
      if (!isAbutted(x_begin, y) && cellAtSite(x_begin - 1, y)) {
        debugPrint(logger_,
                   DPL,
                   "one_site_gap",
                   1,
                   "One site gap left of {}  at ({}, {})",
                   cell->name(),
                   x,
                   y);
        return false;
      }
      // right side
      if (!isAbutted(x_finish, y) && cellAtSite(x_finish + 1, y)) {
        debugPrint(logger_,
                   DPL,
                   "one_site_gap",
                   1,
                   "One site gap right of {} at ({}, {})",
                   cell->name(),
                   x,
                   y);
        return false;
      }
    }
  }

  const auto orient = grid_->getSiteOrientation(x, y, site).value();

  // Check for symmetry
  auto* dbMaster = cell->getDbInst()->getMaster();
  unsigned masterSym = dpl_evolve::DetailedOrient::getMasterSymmetry(dbMaster);
  if (!checkMasterSym(masterSym, orient)) {
    return false;
  }

  // For multi-row cells, the bottom-row site/orient check above only covers
  // the bottom row; it doesn't ensure the master's power pin stack lines up
  // with the PDN rail stack across the span.  Reject wrong-parity landings.
  if (cell->getMaster()->isMultiRow() && !checkRowPowerCompatible(cell, y)) {
    return false;
  }

  return drc_engine_->checkDRC(cell, x, y, orient);
}

bool Opendp::checkRowPowerCompatible(const Node* cell, const GridY y) const
{
  const int row_idx = arch_->find_closest_row(grid_->gridYToDbu(y));
  if (row_idx >= arch_->getNumRows()) {
    return false;
  }
  bool flip = false;
  return arch_->powerCompatible(cell, arch_->getRow(row_idx), flip);
}

bool Opendp::checkMasterSym(unsigned masterSym, unsigned cellOri) const
{
  using odb::dbOrientType;
  switch (cellOri) {
    case dbOrientType::R0:
      return true;
    case dbOrientType::MX:
      return (masterSym & Symmetry_X) != 0;
    case dbOrientType::MY:
      return (masterSym & Symmetry_Y) != 0;
    case dbOrientType::R180:
      return (masterSym & Symmetry_X) && (masterSym & Symmetry_Y);
    case dbOrientType::R90:
    case dbOrientType::R270:
      return (masterSym & Symmetry_ROT90) != 0;
    case dbOrientType::MXR90:
    case dbOrientType::MYR90:
      return (masterSym & Symmetry_ROT90) && (masterSym & Symmetry_X)
             && (masterSym & Symmetry_Y);
    default:
      return false;
  }
}

////////////////////////////////////////////////////////////////

// Legalize cell origin
//  inside the core
//  row site
DbuPt Opendp::legalPt(const Node* cell, const DbuPt& pt) const
{
  // Move inside core.
  const DbuX site_width = grid_->getSiteWidth();
  const DbuX core_x = std::clamp(
      pt.x,
      DbuX{0},
      gridToDbu(grid_->getRowSiteCount(), site_width) - cell->getWidth());
  // Align with row site.
  const GridX grid_x{divRound(core_x.v, site_width.v)};
  const DbuX legal_x{gridToDbu(grid_x, site_width)};
  // Align to row
  const DbuY core_y
      = std::clamp(pt.y, DbuY{0}, DbuY{core_.yMax()} - cell->getHeight());
  const GridY grid_y = grid_->gridRoundY(core_y);
  DbuY legal_y = grid_->gridYToDbu(grid_y);

  return {legal_x, legal_y};
}

GridPt Opendp::legalGridPt(const Node* cell, const DbuPt& pt) const
{
  const DbuPt legal = legalPt(cell, pt);
  return GridPt(grid_->gridX(legal.x), grid_->gridSnapDownY(legal.y));
}

DbuPt Opendp::nearestBlockEdge(const Node* cell,
                               const DbuPt& legal_pt,
                               const odb::Rect& block_bbox) const
{
  const DbuX legal_x = legal_pt.x;
  const DbuY legal_y = legal_pt.y;

  const DbuX x_min_dist = abs(legal_x - block_bbox.xMin());
  const DbuX x_max_dist
      = abs(DbuX{block_bbox.xMax()} - (legal_x + cell->getWidth()));
  const DbuY y_min_dist = abs(legal_y - block_bbox.yMin());
  const DbuY y_max_dist
      = abs(DbuY{block_bbox.yMax()} - (legal_y + cell->getHeight()));

  const int min_dist
      = std::min({x_min_dist.v, x_max_dist.v, y_min_dist.v, y_max_dist.v});

  if (min_dist == x_min_dist) {  // left of block
    return legalPt(cell,
                   {DbuX{block_bbox.xMin()} - cell->getWidth(), legal_pt.y});
  }
  if (min_dist == x_max_dist) {  // right of block
    return legalPt(cell, {DbuX{block_bbox.xMax()}, legal_pt.y});
  }
  if (min_dist == y_min_dist) {  // below block
    return legalPt(cell,
                   {legal_pt.x, DbuY{block_bbox.yMin() - cell->getHeight().v}});
  }
  // above block
  return legalPt(cell, {legal_pt.x, DbuY{block_bbox.yMax()}});
}

// Find the nearest valid site left/right/above/below, if any.
// The site doesn't need to be empty but mearly valid.  That should
// be a reasonable place to start the search.  Returns true if any
// site can be found.
bool Opendp::moveHopeless(const Node* cell, GridX& grid_x, GridY& grid_y) const
{
  GridX best_x = grid_x;
  GridY best_y = grid_y;
  int best_dist = std::numeric_limits<int>::max();
  const GridX site_count = grid_->getRowSiteCount();
  const GridY row_count = grid_->getRowCount();
  const DbuX site_width = grid_->getSiteWidth();

  for (GridX x = grid_x - 1; x >= 0; --x) {  // left
    if (grid_->pixel(grid_y, x).is_valid) {
      best_dist = gridToDbu(grid_x - x - 1, site_width).v;
      best_x = x;
      best_y = grid_y;
      break;
    }
  }
  for (GridX x = grid_x + 1; x < site_count; ++x) {  // right
    if (grid_->pixel(grid_y, x).is_valid) {
      const int dist = gridToDbu(x - grid_x, site_width).v - cell->getWidth().v;
      if (dist < best_dist) {
        best_dist = dist;
        best_x = x;
        best_y = grid_y;
      }
      break;
    }
  }
  for (GridY y = grid_y - 1; y >= 0; --y) {  // below
    if (grid_->pixel(y, grid_x).is_valid) {
      const int dist = (grid_->gridYToDbu(grid_y) - grid_->gridYToDbu(y)).v;
      if (dist < best_dist) {
        best_dist = dist;
        best_x = grid_x;
        best_y = y;
      }
      break;
    }
  }
  for (GridY y = grid_y + 1; y < row_count; ++y) {  // above
    if (grid_->pixel(y, grid_x).is_valid) {
      const int dist = (grid_->gridYToDbu(y) - grid_->gridYToDbu(grid_y)).v;
      if (dist < best_dist) {
        best_dist = dist;
        best_x = grid_x;
        best_y = y;
      }
      break;
    }
  }
  if (best_dist != std::numeric_limits<int>::max()) {
    grid_x = best_x;
    grid_y = best_y;
    return true;
  }
  return false;
}

void Opendp::initMacrosAndGrid()
{
  importDb();
  adjustNodesOrient();
  initGrid();
  setFixedGridCells();
}

void Opendp::convertDbToCell(odb::dbInst* db_inst, Node& cell)
{
  cell.setType(Node::CELL);
  cell.setDbInst(db_inst);
  odb::Rect bbox = getBbox(db_inst);
  cell.setWidth(DbuX{bbox.dx()});
  cell.setHeight(DbuY{bbox.dy()});
  cell.setLeft(DbuX{bbox.xMin()});
  cell.setBottom(DbuY{bbox.yMin()});
  cell.setOrient(db_inst->getOrient());
}

DbuPt Opendp::pointOffMacro(const Node& cell)
{
  // Get cell position
  const DbuPt init = initialLocation(&cell, false);
  const odb::Rect bbox(init.x.v,
                       init.y.v,
                       init.x.v + cell.getWidth().v,
                       init.y.v + cell.getHeight().v);

  const GridRect grid_box = grid_->gridCovering(bbox);

  Pixel* pixel1 = grid_->gridPixel(grid_box.xlo, grid_box.ylo);
  Pixel* pixel2 = grid_->gridPixel(grid_box.xhi, grid_box.ylo);
  Pixel* pixel3 = grid_->gridPixel(grid_box.xlo, grid_box.yhi);
  Pixel* pixel4 = grid_->gridPixel(grid_box.xhi, grid_box.yhi);

  Node* block = nullptr;
  if (pixel1 && pixel1->cell && pixel1->cell->isBlock()) {
    block = pixel1->cell;
  } else if (pixel2 && pixel2->cell && pixel2->cell->isBlock()) {
    block = pixel2->cell;
  } else if (pixel3 && pixel3->cell && pixel3->cell->isBlock()) {
    block = pixel3->cell;
  } else if (pixel4 && pixel4->cell && pixel4->cell->isBlock()) {
    block = pixel4->cell;
  }

  if (block && block->isBlock()) {
    // Get new legal position
    const odb::Rect block_bbox(block->getLeft().v,
                               block->getBottom().v,
                               block->getLeft().v + block->getWidth().v,
                               block->getBottom().v + block->getHeight().v);
    return nearestBlockEdge(&cell, init, block_bbox);
  }
  return init;
}

void Opendp::legalCellPos(odb::dbInst* db_inst)
{
  Node cell;
  convertDbToCell(db_inst, cell);
  // returns the initial position of the cell
  const DbuPt init_pos = initialLocation(&cell, false);
  // returns the modified position if the cell is in a macro
  const DbuPt legal_pt = pointOffMacro(cell);
  // return the modified position if the cell is outside the die
  const DbuPt new_pos = legalPt(&cell, legal_pt);

  if (init_pos == new_pos) {
    return;
  }

  // transform to grid Pos for align
  const GridPt legal_grid_pt{grid_->gridX(DbuX{new_pos.x}),
                             grid_->gridSnapDownY(DbuY{new_pos.y})};
  // Transform position on real position
  setGridLoc(&cell, legal_grid_pt.x, legal_grid_pt.y);
  // Set position of cell on db
  db_inst->setLocation(core_.xMin() + cell.getLeft().v,
                       core_.yMin() + cell.getBottom().v);
}

DbuPt Opendp::initialLocation(const Node* cell, const bool padded) const
{
  if (!padded && use_guided_initial_locations_
      && hasGuidedInitialLocation(cell)) {
    const auto& guided = guided_initial_locations_[cell->getId()];
    return {DbuX{guided.first}, DbuY{guided.second}};
  }

  DbuPt loc;
  cell->getDbInst()->getLocation(loc.x.v, loc.y.v);
  loc.x -= core_.xMin();
  if (padded) {
    loc.x -= gridToDbu(padding_->padLeft(cell), grid_->getSiteWidth());
  }
  loc.y -= core_.yMin();
  return loc;
}

// Legalize pt origin for cell
//  inside the core
//  row site
//  not on top of a macro
//  not in a hopeless site
DbuPt Opendp::legalPt(const Node* cell, const bool padded) const
{
  if (cell->isFixed()) {
    logger_->critical(
        DPL, 26, "legalPt called on fixed cell {}.", cell->name());
  }

  const DbuPt init = initialLocation(cell, padded);
  DbuPt legal_pt = legalPt(cell, init);
  GridX grid_x = grid_->gridX(legal_pt.x);
  GridY grid_y = grid_->gridSnapDownY(legal_pt.y);

  Pixel* pixel = grid_->gridPixel(grid_x, grid_y);
  if (pixel) {
    // Move std cells off of macros.  First try the is_hopeless strategy
    if (pixel->is_hopeless && moveHopeless(cell, grid_x, grid_y)) {
      legal_pt = DbuPt(gridToDbu(grid_x, grid_->getSiteWidth()),
                       grid_->gridYToDbu(grid_y));
      pixel = grid_->gridPixel(grid_x, grid_y);
    }

    const Node* block = pixel->cell;

    // If that didn't do the job fall back on the old move to nearest
    // edge strategy.  This doesn't consider site availability at the
    // end used so it is secondary.
    if (block && block->isBlock()) {
      const odb::Rect block_bbox(block->getLeft().v,
                                 block->getBottom().v,
                                 block->getLeft().v + block->getWidth().v,
                                 block->getBottom().v + block->getHeight().v);
      if ((legal_pt.x + cell->getWidth()) >= block_bbox.xMin()
          && legal_pt.x <= block_bbox.xMax()
          && (legal_pt.y + cell->getHeight()) >= block_bbox.yMin()
          && legal_pt.y <= block_bbox.yMax()) {
        legal_pt = nearestBlockEdge(cell, legal_pt, block_bbox);
      }
    }
  }

  return legal_pt;
}

GridPt Opendp::legalGridPt(const Node* cell, const bool padded) const
{
  const DbuPt pt = legalPt(cell, padded);
  return GridPt(grid_->gridX(pt.x), grid_->gridSnapDownY(pt.y));
}

void Opendp::setGridLoc(Node* cell, const GridX x, const GridY y)
{
  cell->setLeft(gridToDbu(x, grid_->getSiteWidth()));
  cell->setBottom(grid_->gridYToDbu(y));
}
void Opendp::placeCell(Node* cell, const GridX x, const GridY y)
{
  const DbuX original_x = cell->getLeft();
  const DbuY original_y = cell->getBottom();
  const bool was_placed = cell->isPlaced();
  DiamondSourceState& diamond_state = diamondSourceState();
  const int cell_height_rows = std::max(1, grid_->gridHeight(cell).v);
  if (was_placed) {
    const GridY old_y = grid_->gridSnapDownY(original_y);
    invalidateDenseRowGapRange(
        diamond_state, old_y.v, old_y.v + cell_height_rows);
  }
  setGridLoc(cell, x, y);
  grid_->paintPixel(cell);
  cell->setPlaced(true);
  invalidateDenseRowGapRange(diamond_state, y.v, y.v + cell_height_rows);
  odb::dbSite* site = cell->getDbInst()->getMaster()->getSite();
  cell->setOrient(grid_->getSiteOrientation(x, y, site).value());
  if (journal_) {
    MoveCellAction action(cell,
                          original_x,
                          original_y,
                          cell->getLeft(),
                          cell->getBottom(),
                          was_placed);
    journal_->addAction(action);
  }
}

void Opendp::unplaceCell(Node* cell)
{
  if (cell->isFixed() || !cell->isPlaced()) {
    return;
  }
  const GridY old_y = grid_->gridSnapDownY(cell->getBottom());
  const int cell_height_rows = std::max(1, grid_->gridHeight(cell).v);
  if (journal_) {
    UnplaceCellAction action(cell, cell->isHold());
    journal_->addAction(action);
  }
  grid_->erasePixel(cell);
  invalidateDenseRowGapRange(
      diamondSourceState(), old_y.v, old_y.v + cell_height_rows);
  cell->setPlaced(false);
  cell->setHold(false);
}

}  // namespace dpl_evolve
