// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include <chrono>

#include "LegalmCommon.h"
#include "LegalmTechPenalty.h"
#include "PlacementDRC.h"

#ifdef _OPENMP
#include <omp.h>
#endif

namespace dpl_evolve {

namespace {

constexpr LegalmPaperParams kPaper = legalmPaperParams();
constexpr int kTargetThreads = legalmCpuCaps().target_threads;
constexpr int kLegalmKpart = kPaper.kpart;
constexpr int kLegalmKely = kPaper.kely;
constexpr int kLegalmKthre = kPaper.kthre;
constexpr int kLegalmKh = kPaper.kh;
constexpr int kLegalmLocalOptimumWindow = kPaper.local_optimum_window;
// Default for the paper's Algorithm 1 input T when the caller does not set it:
// use the documented reset point plus the documented local-optimum detection
// window so the escape branch can actually run before termination.
constexpr int kDefaultAlmOuterIterations
    = kLegalmKthre + kLegalmLocalOptimumWindow;

template <typename Func>
void parallelFor(const int count, const int threads, Func&& func)
{
  if (count <= 0) {
    return;
  }

  const int worker_count = std::max(1, std::min(threads, count));
  if (worker_count == 1) {
    func(0, count, 0);
    return;
  }

#ifdef _OPENMP
#pragma omp parallel num_threads(worker_count)
  {
    const int worker = omp_get_thread_num();
    const int begin = (count * worker) / worker_count;
    const int end = (count * (worker + 1)) / worker_count;
    func(begin, end, worker);
  }
#else
  std::vector<std::thread> workers;
  workers.reserve(worker_count);
  for (int worker = 0; worker < worker_count; ++worker) {
    const int begin = (count * worker) / worker_count;
    const int end = (count * (worker + 1)) / worker_count;
    workers.emplace_back(
        [&, begin, end, worker]() { func(begin, end, worker); });
  }
  for (auto& worker : workers) {
    worker.join();
  }
#endif
}

int chooseThreadCount(const int requested_threads)
{
  const int thread_budget
      = requested_threads > 0 ? requested_threads : kTargetThreads;
  const unsigned hw_threads = std::thread::hardware_concurrency();
  if (hw_threads == 0) {
    return 1;
  }
  return std::max(1, std::min(thread_budget, static_cast<int>(hw_threads)));
}

int chooseBinCount(const int row_count, const int site_count)
{
  if (row_count <= 0 || site_count <= 0) {
    return 0;
  }
  return site_count;
}

int binCapacitySites(const int bin, const int bin_sites, const int site_count)
{
  const int x0 = bin * bin_sites;
  const int x1 = std::min(site_count, x0 + bin_sites);
  return std::max(1, x1 - x0);
}

int binOverlapSites(const int x,
                    const int width,
                    const int bin,
                    const int bin_sites,
                    const int site_count)
{
  const int bin_x0 = bin * bin_sites;
  const int bin_x1 = std::min(site_count, bin_x0 + bin_sites);
  return std::max(0, std::min(x + width, bin_x1) - std::max(x, bin_x0));
}

double almFootprintCost(const std::vector<int64_t>& occupancy,
                        const std::vector<int64_t>* occupancy_delta,
                        const std::vector<double>& lambda,
                        const std::vector<double>& bin_inv_capacities,
                        const LegalmFootprint& current,
                        const LegalmFootprint& candidate,
                        const int bin_count,
                        const int bin_sites,
                        const int site_count,
                        const double target_density,
                        const double sigma)
{
  // Eq. (32)/(34): candidate cost contribution from the ALM penalty is
  // lambda_j * T_j, where T_j = max(1 + sigma * g_j(x), 0).
  // The displacement term w_i,t,j is added by the caller so height-class
  // weighting and alpha_max tail weighting stay explicit.
  double cost = 0.0;
  int covered_sites = 0;

  const int first_candidate_bin
      = std::clamp(candidate.site / bin_sites, 0, bin_count - 1);
  const int last_candidate_bin = std::clamp(
      (std::max(candidate.site, candidate.site + candidate.width_sites - 1))
          / bin_sites,
      0,
      bin_count - 1);

  for (int dy = 0; dy < candidate.height_rows; ++dy) {
    const int row = candidate.row + dy;
    for (int bin = first_candidate_bin; bin <= last_candidate_bin; ++bin) {
      const int candidate_overlap = binOverlapSites(
          candidate.site, candidate.width_sites, bin, bin_sites, site_count);
      if (candidate_overlap == 0) {
        continue;
      }

      const int idx = (row * bin_count) + bin;
      int64_t adjusted_occ = occupancy[idx];
      if (occupancy_delta != nullptr) {
        adjusted_occ += (*occupancy_delta)[idx];
      }
      if (row >= current.row && row < current.row + current.height_rows) {
        adjusted_occ -= binOverlapSites(
            current.site, current.width_sites, bin, bin_sites, site_count);
      }
      adjusted_occ = std::max<int64_t>(0, adjusted_occ);

      const double density
          = static_cast<double>(adjusted_occ) * bin_inv_capacities[bin];
      const double signed_violation = density - target_density;
      const double gradient_scale
          = std::max(1.0 + sigma * signed_violation, 0.0);
      cost += static_cast<double>(candidate_overlap) * lambda[idx]
              * gradient_scale;
      covered_sites += candidate_overlap;
    }
  }

  return covered_sites == 0 ? 0.0 : cost;
}

struct GuidanceCell
{
  Node* cell = nullptr;
  DbuPt init;
  int row = 0;
  int x_site = 0;
  int width_sites = 1;
  int height_rows = 1;
  int vertical_step_rows = 1;
  double height_class_weight = 1.0;
  unsigned master_sym = 0;
  bool master_multi_row = false;
  int site_sym_class = -1;
  std::vector<LegalmHpwlTerm> hpwl_terms;
  Group* group = nullptr;
};

struct GuidanceTarget
{
  int x = 0;
  int y = 0;
  int shift_sites = 0;
  int shift_rows = 0;
  double score = 0.0;
  bool active = false;
};

struct SiteSymClass
{
  odb::dbSite* site = nullptr;
  unsigned master_sym = 0;
  std::vector<unsigned char> compatible;
  std::vector<odb::dbOrientType::Value> orientations;
};

struct OverflowStats
{
  int overflow_bins = 0;
  double max_density = 0.0;
  double total_overflow_sites = 0.0;
};

struct RelaxedProjection
{
  int row = 0;
  int site = 0;
  bool direct = false;
  bool projected = false;
};

struct StaticInterval
{
  int x0 = 0;
  int x1 = 0;
  Group* group = nullptr;
  int node = -1;
};

struct PlateSegment
{
  int row = 0;
  int x0 = 0;
  int x1 = 0;
  Group* group = nullptr;
  int component = -1;
};

struct AlmAcceptedMove
{
  int cell_idx = -1;
  int old_row = 0;
  int old_site = 0;
  int new_row = 0;
  int new_site = 0;
};

}  // namespace

// LEGALM Stage 1 and Stage 2 guidance.
//
// Paper mapping:
// - Stage 1 relaxes movable-cell overlap and projects each cell to the nearest
//   statically valid row/site/fence/blockage position.
// - Stage 2 runs a linearized ALM loop with bounded BGD candidate search.
// - Triplefold partitioning keeps updates parallel while limiting mutual
//   influence; cells are sorted by cached demand d_i(x) once per ALM round.
// - The escape pass is explicit so local-optimum behavior remains measurable.
void Opendp::runDifferentialGuidance(const EvolveContext& context)
{
  using Clock = std::chrono::steady_clock;
  const auto guidance_start = Clock::now();
  auto elapsed_ms
      = [](const Clock::time_point begin, const Clock::time_point end) {
          return std::chrono::duration<double, std::milli>(end - begin).count();
        };

  clearGuidedInitialLocations();

  if (network_ == nullptr || grid_ == nullptr) {
    logger_->metric("dpl_evolve__diff_guidance__status", 0);
    return;
  }

  // Build a lightweight legal-site view for snapping only.  The full LEGALM
  // legalization stage rebuilds the true grid before committing placement.
  initGrid();
  setFixedGridCells();

  const int row_count = grid_->getRowCount().v;
  const int site_count = grid_->getRowSiteCount().v;
  const DbuX site_width = grid_->getSiteWidth();
  if (row_count <= 0 || site_count <= 0 || site_width.v <= 0) {
    logger_->metric("dpl_evolve__diff_guidance__status", 0);
    return;
  }

  const LegalmCpuCaps cpu_caps = resolveLegalmCpuCaps(context);
  const int thread_count = chooseThreadCount(cpu_caps.target_threads);
  const int alm_iteration_limit = context.legalm_iteration_limit > 0
                                      ? context.legalm_iteration_limit
                                      : kDefaultAlmOuterIterations;
  const int row_height_dbu
      = arch_ != nullptr && arch_->getNumRows() > 0
            ? std::max(1, arch_->getRow(0)->getHeight().v)
            : std::max(1,
                       (row_count > 1 ? (grid_->gridYToDbu(GridY{1})
                                         - grid_->gridYToDbu(GridY{0}))
                                            .v
                                      : site_width.v));
  const int row_equiv_sites = std::max(
      1,
      static_cast<int>(std::llround(static_cast<double>(row_height_dbu)
                                    / static_cast<double>(site_width.v))));
  const int bin_count = chooseBinCount(row_count, site_count);
  const int bin_sites
      = std::max(1,
                 static_cast<int>(std::ceil(static_cast<double>(site_count)
                                            / std::max(1, bin_count))));
  const int row_bin_count = row_count * bin_count;
  std::vector<int> bin_capacities(bin_count, 1);
  std::vector<double> bin_inv_capacities(bin_count, 1.0);
  for (int bin = 0; bin < bin_count; ++bin) {
    bin_capacities[bin] = binCapacitySites(bin, bin_sites, site_count);
    bin_inv_capacities[bin]
        = 1.0 / static_cast<double>(std::max(1, bin_capacities[bin]));
  }

  // LEGALM Stage One is a relaxed/static legalization.  The static legal area
  // is decomposed as scanline row segments: each contiguous run of valid,
  // unblocked, same-group sites becomes a PlateSegment, and vertically
  // overlapping same-group segments form a plate connected component.  Movable
  // cell overlap is intentionally ignored until Stage 2.
  std::vector<std::vector<StaticInterval>> static_free_intervals(row_count);
  std::vector<PlateSegment> plate_segments;
  int static_interval_count = 0;
  int64_t static_free_sites = 0;
  for (int row = 0; row < row_count; ++row) {
    int begin = -1;
    Group* active_group = nullptr;
    auto close_interval = [&](const int end) {
      if (begin < 0 || begin >= end) {
        begin = -1;
        active_group = nullptr;
        return;
      }
      const int node = static_cast<int>(plate_segments.size());
      static_free_intervals[row].push_back({begin, end, active_group, node});
      plate_segments.push_back({row, begin, end, active_group, -1});
      static_free_sites += end - begin;
      ++static_interval_count;
      begin = -1;
      active_group = nullptr;
    };
    for (int site = 0; site < site_count; ++site) {
      const Pixel* pixel = grid_->gridPixel(GridX{site}, GridY{row});
      const bool free = pixel != nullptr && pixel->is_valid
                        && pixel->cell == nullptr
                        && pixel->padding_reserved_by == nullptr;
      Group* pixel_group = free ? pixel->group : nullptr;
      if (free && begin < 0) {
        begin = site;
        active_group = pixel_group;
      } else if ((!free || pixel_group != active_group) && begin >= 0) {
        close_interval(site);
        if (free) {
          begin = site;
          active_group = pixel_group;
        }
      }
    }
    if (begin >= 0) {
      close_interval(site_count);
    }
  }

  std::vector<int> plate_parent(plate_segments.size());
  std::iota(plate_parent.begin(), plate_parent.end(), 0);
  auto find_plate_root = [&](int node) {
    int root = node;
    while (plate_parent[root] != root) {
      root = plate_parent[root];
    }
    while (plate_parent[node] != node) {
      const int next = plate_parent[node];
      plate_parent[node] = root;
      node = next;
    }
    return root;
  };
  auto union_plate_roots = [&](const int lhs, const int rhs) {
    const int lhs_root = find_plate_root(lhs);
    const int rhs_root = find_plate_root(rhs);
    if (lhs_root != rhs_root) {
      plate_parent[rhs_root] = lhs_root;
    }
  };
  for (int row = 1; row < row_count; ++row) {
    const auto& prev = static_free_intervals[row - 1];
    const auto& curr = static_free_intervals[row];
    int prev_idx = 0;
    int curr_idx = 0;
    while (prev_idx < static_cast<int>(prev.size())
           && curr_idx < static_cast<int>(curr.size())) {
      const StaticInterval& a = prev[prev_idx];
      const StaticInterval& b = curr[curr_idx];
      if (a.group == b.group && std::max(a.x0, b.x0) < std::min(a.x1, b.x1)) {
        union_plate_roots(a.node, b.node);
      }
      if (a.x1 < b.x1) {
        ++prev_idx;
      } else {
        ++curr_idx;
      }
    }
  }
  std::vector<int64_t> component_area(plate_segments.size(), 0);
  int plate_component_count = 0;
  for (int node = 0; node < static_cast<int>(plate_segments.size()); ++node) {
    const int root = find_plate_root(node);
    if (plate_segments[root].component < 0) {
      plate_segments[root].component = plate_component_count++;
    }
    const int component = plate_segments[root].component;
    plate_segments[node].component = component;
    component_area[component]
        += plate_segments[node].x1 - plate_segments[node].x0;
  }
  component_area.resize(plate_component_count);
  std::vector<std::vector<int>> component_segment_indices(
      plate_component_count);
  std::vector<Group*> component_group(plate_component_count, nullptr);
  for (int segment_idx = 0;
       segment_idx < static_cast<int>(plate_segments.size());
       ++segment_idx) {
    const int component = plate_segments[segment_idx].component;
    if (component >= 0 && component < plate_component_count) {
      component_segment_indices[component].push_back(segment_idx);
      component_group[component] = plate_segments[segment_idx].group;
    }
  }
  const int64_t max_plate_component_area
      = component_area.empty()
            ? 0
            : *std::max_element(component_area.begin(), component_area.end());

  auto interval_index_containing
      = [&](const int row, const int x, const int width, const Group* group) {
          if (row < 0 || row >= row_count || x < 0 || x + width > site_count) {
            return -1;
          }
          const auto& intervals = static_free_intervals[row];
          int lo = 0;
          int hi = static_cast<int>(intervals.size());
          while (lo < hi) {
            const int mid = (lo + hi) / 2;
            if (intervals[mid].x1 <= x) {
              lo = mid + 1;
            } else {
              hi = mid;
            }
          }
          if (lo < static_cast<int>(intervals.size())
              && intervals[lo].group == group && intervals[lo].x0 <= x
              && x + width <= intervals[lo].x1) {
            return lo;
          }
          return -1;
        };

  auto footprint_component = [&](const int row,
                                 const int x,
                                 const int width,
                                 const int height_rows,
                                 const Group* group) {
    int component = -1;
    for (int dy = 0; dy < height_rows; ++dy) {
      const int interval_idx
          = interval_index_containing(row + dy, x, width, group);
      if (interval_idx < 0) {
        return -1;
      }
      const int node = static_free_intervals[row + dy][interval_idx].node;
      const int row_component = plate_segments[node].component;
      if (component < 0) {
        component = row_component;
      } else if (component != row_component) {
        return -1;
      }
    }
    return component;
  };

  std::vector<SiteSymClass> site_sym_classes;
  auto row_site_compatible =
      [&](const GuidanceCell& gc, const int row, const int x) {
        Node* cell = gc.cell;
        if (cell == nullptr || cell->getSite() == nullptr || row < 0
            || row >= row_count || x < 0 || x >= site_count) {
          return false;
        }
        if (gc.site_sym_class < 0
            || gc.site_sym_class >= static_cast<int>(site_sym_classes.size())
            || site_sym_classes[gc.site_sym_class]
                       .compatible[(static_cast<size_t>(row)
                                    * static_cast<size_t>(site_count))
                                   + static_cast<size_t>(x)]
                   == 0) {
          return false;
        }
        if (gc.master_multi_row && !checkRowPowerCompatible(cell, GridY{row})) {
          return false;
        }
        return true;
      };

  auto interval_contains_static
      = [](const std::vector<StaticInterval>& intervals,
           const int x,
           const int width,
           const Group* group) {
          for (const StaticInterval& interval : intervals) {
            if (interval.group == group && interval.x0 <= x
                && x + width <= interval.x1) {
              return true;
            }
            if (interval.x0 > x) {
              break;
            }
          }
          return false;
        };

  auto static_fits_all_rows = [&](const int row,
                                  const int x,
                                  const int width,
                                  const int height_rows,
                                  const Group* group) {
    if (row < 0 || row + height_rows > row_count || x < 0
        || x + width > site_count) {
      return false;
    }
    for (int dy = 0; dy < height_rows; ++dy) {
      if (!interval_contains_static(
              static_free_intervals[row + dy], x, width, group)) {
        return false;
      }
    }
    return true;
  };

  auto relaxed_static_project = [&](const Node* cell,
                                    const int desired_row,
                                    const int desired_site,
                                    const int width_sites,
                                    const int height_rows,
                                    const int row_equiv_sites) {
    RelaxedProjection result;
    const Group* group = cell->getGroup();
    result.row
        = std::clamp(desired_row, 0, std::max(0, row_count - height_rows));
    result.site
        = std::clamp(desired_site, 0, std::max(0, site_count - width_sites));
    int best_cost = std::numeric_limits<int>::max();

    auto consider = [&](const int row,
                        const int site,
                        const bool direct_candidate) {
      const int clamped_row
          = std::clamp(row, 0, std::max(0, row_count - height_rows));
      const int clamped_site
          = std::clamp(site, 0, std::max(0, site_count - width_sites));
      if (!static_fits_all_rows(
              clamped_row, clamped_site, width_sites, height_rows, group)) {
        return false;
      }
      if (!canBePlaced(cell, GridX{clamped_site}, GridY{clamped_row})) {
        return false;
      }
      const int cost = std::abs(clamped_site - desired_site)
                       + row_equiv_sites * std::abs(clamped_row - desired_row);
      if (cost < best_cost) {
        best_cost = cost;
        result.row = clamped_row;
        result.site = clamped_site;
        result.direct = direct_candidate && cost == 0;
        result.projected = !result.direct;
      }
      return true;
    };

    if (consider(result.row, result.site, true)) {
      return result;
    }

    for (int row_delta = 0; row_delta < row_count; ++row_delta) {
      const std::array<int, 2> candidate_rows{desired_row - row_delta,
                                              desired_row + row_delta};
      for (const int row : candidate_rows) {
        if (row < 0 || row + height_rows > row_count) {
          continue;
        }
        if (row_delta != 0 && row == desired_row - row_delta
            && row == desired_row + row_delta) {
          continue;
        }
        for (const StaticInterval& interval : static_free_intervals[row]) {
          if (interval.group != group
              || interval.x1 - interval.x0 < width_sites) {
            continue;
          }
          const int projected_site = std::clamp(
              desired_site, interval.x0, interval.x1 - width_sites);
          consider(row, projected_site, false);
          if (projected_site > interval.x0) {
            consider(row, interval.x0, false);
          }
          if (projected_site + width_sites < interval.x1) {
            consider(row, interval.x1 - width_sites, false);
          }
        }
      }
      if (best_cost != std::numeric_limits<int>::max()
          && (row_delta + 1) * row_equiv_sites > best_cost) {
        break;
      }
    }

    return result;
  };

  std::vector<GuidanceCell> cells;
  cells.reserve(network_->getNumCells());
  auto site_sym_class_index
      = [&](odb::dbSite* site, const unsigned master_sym) {
          for (int i = 0; i < static_cast<int>(site_sym_classes.size()); ++i) {
            if (site_sym_classes[i].site == site
                && site_sym_classes[i].master_sym == master_sym) {
              return i;
            }
          }
          site_sym_classes.push_back({site, master_sym, {}, {}});
          return static_cast<int>(site_sym_classes.size()) - 1;
        };
  int skipped_cells = 0;
  int64_t movable_width_sites = 0;
  int stage1_direct_cells = 0;
  int stage1_projected_cells = 0;
  int stage1_failed_projection_cells = 0;
  int stage1_row_projected_cells = 0;
  int stage1_max_site_shift = 0;
  int stage1_max_row_shift = 0;
  int64_t stage1_total_abs_shift = 0;

  for (const auto& node_ptr : network_->getNodes()) {
    Node* cell = node_ptr.get();
    if (cell == nullptr || cell->getType() != Node::CELL || cell->isFixed()
        || !cell->isStdCell()) {
      ++skipped_cells;
      continue;
    }

    const DbuPt init = initialLocation(cell, false);
    const GridPt legal = legalGridPt(cell, init);
    const int row = std::clamp(legal.y.v, 0, row_count - 1);
    const int x_site = std::clamp(legal.x.v, 0, site_count - 1);
    const int width_sites = std::max(
        1,
        static_cast<int>(std::ceil(static_cast<double>(cell->getWidth().v)
                                   / static_cast<double>(site_width.v))));
    const int height_rows = std::max(1, grid_->gridHeight(cell).v);
    const RelaxedProjection projection = relaxed_static_project(
        cell, row, x_site, width_sites, height_rows, row_equiv_sites);
    if (projection.direct) {
      ++stage1_direct_cells;
    } else if (projection.projected) {
      ++stage1_projected_cells;
    } else {
      ++stage1_failed_projection_cells;
    }
    if (projection.projected && projection.row != row) {
      ++stage1_row_projected_cells;
    }
    const int stage1_site_shift = std::abs(projection.site - x_site);
    const int stage1_row_shift = std::abs(projection.row - row);
    stage1_max_site_shift = std::max(stage1_max_site_shift, stage1_site_shift);
    stage1_max_row_shift = std::max(stage1_max_row_shift, stage1_row_shift);
    stage1_total_abs_shift
        += stage1_site_shift + row_equiv_sites * stage1_row_shift;
    const unsigned master_sym = dpl_evolve::DetailedOrient::getMasterSymmetry(
        cell->getDbInst()->getMaster());
    cells.push_back({cell,
                     init,
                     projection.row,
                     projection.site,
                     width_sites,
                     height_rows,
                     height_rows % 2 == 0 ? 2 : 1,
                     1.0,
                     master_sym,
                     cell->getMaster()->isMultiRow(),
                     site_sym_class_index(cell->getSite(), master_sym),
                     legalmBuildHpwlTerms(cell),
                     cell->getGroup()});
    movable_width_sites += width_sites;
  }

  if (cells.empty() || bin_count <= 0 || row_bin_count <= 0) {
    logger_->metric("dpl_evolve__diff_guidance__eligible_cells", 0);
    return;
  }

  for (SiteSymClass& cls : site_sym_classes) {
    cls.compatible.assign(
        static_cast<size_t>(row_count) * static_cast<size_t>(site_count), 0);
    cls.orientations.assign(
        static_cast<size_t>(row_count) * static_cast<size_t>(site_count),
        odb::dbOrientType::R0);
    parallelFor(row_count, thread_count, [&](int begin, int end, int) {
      for (int row = begin; row < end; ++row) {
        for (int site = 0; site < site_count; ++site) {
          const auto orient
              = grid_->getSiteOrientation(GridX{site}, GridY{row}, cls.site);
          const size_t idx
              = (static_cast<size_t>(row) * static_cast<size_t>(site_count))
                + static_cast<size_t>(site);
          if (orient.has_value() && checkMasterSym(cls.master_sym, *orient)) {
            cls.compatible[idx] = 1;
            cls.orientations[idx] = orient->getValue();
          }
        }
      }
    });
  }

  std::vector<int> target_sites(cells.size());
  std::vector<int> target_rows(cells.size());
  for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
    target_sites[i] = cells[i].x_site;
    target_rows[i] = cells[i].row;
  }

  int max_height_rows = 1;
  for (const GuidanceCell& gc : cells) {
    max_height_rows = std::max(max_height_rows, gc.height_rows);
  }
  std::vector<int> height_class_counts(max_height_rows + 1, 0);
  for (const GuidanceCell& gc : cells) {
    ++height_class_counts[std::clamp(gc.height_rows, 1, max_height_rows)];
  }
  for (GuidanceCell& gc : cells) {
    const int class_count = std::max(
        1, height_class_counts[std::clamp(gc.height_rows, 1, max_height_rows)]);
    gc.height_class_weight = static_cast<double>(cells.size()) / class_count;
  }
  int64_t total_hpwl_proxy_terms = 0;
  for (const GuidanceCell& gc : cells) {
    total_hpwl_proxy_terms += static_cast<int64_t>(gc.hpwl_terms.size());
  }

  auto build_occupancy = [&]() {
    std::vector<std::vector<int64_t>> local_occupancy(
        thread_count, std::vector<int64_t>(row_bin_count, 0));
    std::vector<std::vector<int>> local_touched(thread_count);

    parallelFor(
        static_cast<int>(cells.size()),
        thread_count,
        [&](int begin, int end, int worker) {
          auto& occ = local_occupancy[worker];
          auto& touched = local_touched[worker];
          for (int i = begin; i < end; ++i) {
            const GuidanceCell& gc = cells[i];
            const int x0 = target_sites[i];
            const int x1 = std::min(site_count, x0 + gc.width_sites);
            const int y0 = std::clamp(
                target_rows[i], 0, std::max(0, row_count - gc.height_rows));
            const int first_bin = std::clamp(x0 / bin_sites, 0, bin_count - 1);
            const int last_bin = std::clamp(
                (std::max(x0, x1 - 1)) / bin_sites, 0, bin_count - 1);
            for (int dy = 0; dy < gc.height_rows && y0 + dy < row_count; ++dy) {
              for (int bin = first_bin; bin <= last_bin; ++bin) {
                const int overlap = binOverlapSites(
                    x0, gc.width_sites, bin, bin_sites, site_count);
                const int idx = ((y0 + dy) * bin_count) + bin;
                if (occ[idx] == 0) {
                  touched.push_back(idx);
                }
                occ[idx] += overlap;
              }
            }
          }
        });

    std::vector<int64_t> occupancy(row_bin_count, 0);
    for (int worker = 0; worker < thread_count; ++worker) {
      const auto& local = local_occupancy[worker];
      for (const int idx : local_touched[worker]) {
        occupancy[idx] += local[idx];
      }
    }
    return occupancy;
  };

  constexpr double kAlphaSigma = kPaper.alpha_sigma;
  constexpr double kAlphaLambda = kPaper.alpha_lambda;
  constexpr double kAlphaHf = kPaper.alpha_hf;
  constexpr double kAlphaMax = kPaper.alpha_max;
  constexpr double kPtech = kPaper.ptech;
  constexpr double kHpwlRegressionPenaltyWeight = 0.10;
  const bool paper_tech_penalty_enabled
      = drc_engine_ != nullptr && drc_engine_->hasCellEdgeSpacingTable();
  const double target_density = 1.0;
  const int max_displacement_threshold_sites = static_cast<int>(
      std::llround(kPaper.delta_threshold_rows * row_equiv_sites));
  const double max_hpwl_regression_penalty_sites
      = 4.0 * static_cast<double>(std::max(1, max_displacement_threshold_sites));

  std::vector<double> lambda(row_bin_count, 0.0);

  auto collect_stats = [&](const std::vector<int64_t>& occupancy) {
    std::vector<OverflowStats> local_stats(thread_count);
    parallelFor(row_count, thread_count, [&](int begin, int end, int worker) {
      OverflowStats stats;
      for (int row = begin; row < end; ++row) {
        for (int bin = 0; bin < bin_count; ++bin) {
          const double density
              = static_cast<double>(occupancy[(row * bin_count) + bin])
                * bin_inv_capacities[bin];
          stats.max_density = std::max(stats.max_density, density);
          const double overflow = std::max(0.0, density - target_density);
          if (overflow > 0.0) {
            ++stats.overflow_bins;
            stats.total_overflow_sites += overflow * bin_capacities[bin];
          }
        }
      }
      local_stats[worker] = stats;
    });
    OverflowStats stats;
    for (const OverflowStats& local : local_stats) {
      stats.overflow_bins += local.overflow_bins;
      stats.max_density = std::max(stats.max_density, local.max_density);
      stats.total_overflow_sites += local.total_overflow_sites;
    }
    return stats;
  };

  std::vector<int64_t> occupancy = build_occupancy();
  // Eq. (36): d_i(x) is the total demand over all sites covered by cell i.
  auto average_cell_demand =
      [&](const std::vector<int64_t>& current_occupancy) {
        std::vector<double> local_sums(thread_count, 0.0);
        std::vector<int> local_counts(thread_count, 0);
        parallelFor(
            static_cast<int>(cells.size()),
            thread_count,
            [&](int begin, int end, int worker) {
              double sum = 0.0;
              int count = 0;
              for (int i = begin; i < end; ++i) {
                const GuidanceCell& gc = cells[i];
                const int site = target_sites[i];
                const int row = target_rows[i];
                const int first_bin
                    = std::clamp(site / bin_sites, 0, bin_count - 1);
                const int last_bin = std::clamp(
                    (std::max(site, site + gc.width_sites - 1)) / bin_sites,
                    0,
                    bin_count - 1);
                double demand = 0.0;
                int covered_sites = 0;
                for (int dy = 0; dy < gc.height_rows && row + dy < row_count;
                     ++dy) {
                  for (int bin = first_bin; bin <= last_bin; ++bin) {
                    const int overlap = binOverlapSites(
                        site, gc.width_sites, bin, bin_sites, site_count);
                    if (overlap == 0) {
                      continue;
                    }
                    const double density
                        = static_cast<double>(
                              current_occupancy[((row + dy) * bin_count) + bin])
                          * bin_inv_capacities[bin];
                    demand += static_cast<double>(overlap) * density;
                    covered_sites += overlap;
                  }
                }
                if (covered_sites > 0) {
                  sum += demand;
                  ++count;
                }
              }
              local_sums[worker] = sum;
              local_counts[worker] = count;
            });
        const double sum
            = std::accumulate(local_sums.begin(), local_sums.end(), 0.0);
        const int count
            = std::accumulate(local_counts.begin(), local_counts.end(), 0);
        return count == 0 ? 1.0 : sum / static_cast<double>(count);
      };

  // Eq. (38)-(39): initialize lambda from expected row-height displacement
  // divided by each cell's augmented demand.
  auto average_lambda_seed = [&](const std::vector<int64_t>& current_occupancy,
                                 const double sigma) {
    std::vector<double> local_sums(thread_count, 0.0);
    std::vector<int> local_counts(thread_count, 0);
    parallelFor(
        static_cast<int>(cells.size()),
        thread_count,
        [&](int begin, int end, int worker) {
          double sum = 0.0;
          int count = 0;
          for (int i = begin; i < end; ++i) {
            const GuidanceCell& gc = cells[i];
            const int site = target_sites[i];
            const int row = target_rows[i];
            const int first_bin
                = std::clamp(site / bin_sites, 0, bin_count - 1);
            const int last_bin = std::clamp(
                (std::max(site, site + gc.width_sites - 1)) / bin_sites,
                0,
                bin_count - 1);
            double augmented = 0.0;
            int covered_sites = 0;
            for (int dy = 0; dy < gc.height_rows && row + dy < row_count;
                 ++dy) {
              for (int bin = first_bin; bin <= last_bin; ++bin) {
                const int overlap = binOverlapSites(
                    site, gc.width_sites, bin, bin_sites, site_count);
                if (overlap == 0) {
                  continue;
                }
                const double density
                    = static_cast<double>(
                          current_occupancy[((row + dy) * bin_count) + bin])
                      * bin_inv_capacities[bin];
                augmented
                    += static_cast<double>(overlap)
                       * (1.0 + density + 0.5 * sigma * density * density);
                covered_sites += overlap;
              }
            }
            if (covered_sites > 0) {
              sum += static_cast<double>(row_equiv_sites)
                     / std::max(1.0e-6, augmented);
              ++count;
            }
          }
          local_sums[worker] = sum;
          local_counts[worker] = count;
        });
    const double sum
        = std::accumulate(local_sums.begin(), local_sums.end(), 0.0);
    const int count
        = std::accumulate(local_counts.begin(), local_counts.end(), 0);
    return count == 0 ? 1.0 : kAlphaLambda * sum / static_cast<double>(count);
  };

  const double average_initial_demand = average_cell_demand(occupancy);
  const double alm_sigma = std::max(1.0, kAlphaSigma * average_initial_demand);
  const double lambda_seed
      = std::max(1.0e-6, average_lambda_seed(occupancy, alm_sigma));
  std::fill(lambda.begin(), lambda.end(), lambda_seed);
  double hf = lambda_seed;
  const OverflowStats initial_stats = collect_stats(occupancy);
  OverflowStats final_stats = initial_stats;
  OverflowStats best_stats = initial_stats;
  std::vector<int> best_target_sites = target_sites;
  std::vector<int> best_target_rows = target_rows;
  int best_stats_iter = -1;
  int best_stats_updates = 0;
  auto overflow_state_better = [](const OverflowStats& candidate,
                                  const OverflowStats& incumbent) {
    constexpr double kEpsilon = 1.0e-6;
    if (candidate.total_overflow_sites
        < incumbent.total_overflow_sites - kEpsilon) {
      return true;
    }
    if (candidate.total_overflow_sites
        > incumbent.total_overflow_sites + kEpsilon) {
      return false;
    }
    if (candidate.overflow_bins != incumbent.overflow_bins) {
      return candidate.overflow_bins < incumbent.overflow_bins;
    }
    return candidate.max_density < incumbent.max_density;
  };
  auto remember_best_targets = [&](const OverflowStats& stats, const int iter) {
    if (!overflow_state_better(stats, best_stats)) {
      return;
    }
    best_stats = stats;
    best_target_sites = target_sites;
    best_target_rows = target_rows;
    best_stats_iter = iter;
    ++best_stats_updates;
  };
  int64_t total_candidate_evals = 0;
  int64_t total_static_candidate_rejects = 0;
  int64_t total_site_compat_candidate_rejects = 0;
  int64_t total_tech_candidate_evals = 0;
  int64_t total_edge_spacing_terms = 0;
  int64_t total_pin_short_terms = 0;
  int64_t total_pin_access_terms = 0;
  int64_t total_accepted_moves = 0;
  int last_iter_moves = 0;
  int vertical_guided_moves = 0;
  std::vector<double> overflow_history;
  const std::vector<LegalmCandidateOffset> candidate_stencil
      = legalmCandidateStencil(cpu_caps);

  const int dbu_per_micron
      = db_ != nullptr && db_->getTech() != nullptr
            ? std::max(1, db_->getTech()->getDbUnitsPerMicron())
            : 1000;
  const int xhint_sites = std::max(
      1,
      static_cast<int>(std::llround(kPaper.xhint_microns * dbu_per_micron
                                    / static_cast<double>(site_width.v))));
  const int yhint_rows = std::max(1, std::min(kPaper.yhint_rows, row_count));
  const int partition_cols
      = std::max(1, (site_count + xhint_sites - 1) / xhint_sites + 1);
  const int partition_rows
      = std::max(1, (row_count + yhint_rows - 1) / yhint_rows + 1);
  const int partition_count = partition_cols * partition_rows;
  int last_tp_scheme = 0;
  int last_partitioned_cells = 0;
  int last_boundary_excluded_cells = 0;

  auto shifted_partition
      = [](const int coord, const int shift, const int stride) {
          return (coord + shift) / std::max(1, stride);
        };

  auto inside_partition = [&](const int row,
                              const int site,
                              const int width,
                              const int height,
                              const int partition_col,
                              const int partition_row,
                              const int site_shift,
                              const int row_shift) {
    if (row < 0 || row + height > row_count || site < 0
        || site + width > site_count) {
      return false;
    }
    return shifted_partition(site, site_shift, xhint_sites) == partition_col
           && shifted_partition(site + width - 1, site_shift, xhint_sites)
                  == partition_col
           && shifted_partition(row, row_shift, yhint_rows) == partition_row
           && shifted_partition(row + height - 1, row_shift, yhint_rows)
                  == partition_row;
  };

  // Algorithm 3 sorts cells by descending d_i(x) from Eq. (36).
  auto cell_demand = [&](const std::vector<int64_t>& current_occupancy,
                         const int cell_idx) {
    const GuidanceCell& gc = cells[cell_idx];
    const int site = target_sites[cell_idx];
    const int row = target_rows[cell_idx];
    const int first_bin = std::clamp(site / bin_sites, 0, bin_count - 1);
    const int last_bin
        = std::clamp((std::max(site, site + gc.width_sites - 1)) / bin_sites,
                     0,
                     bin_count - 1);
    double demand = 0.0;
    for (int dy = 0; dy < gc.height_rows && row + dy < row_count; ++dy) {
      for (int bin = first_bin; bin <= last_bin; ++bin) {
        const int overlap
            = binOverlapSites(site, gc.width_sites, bin, bin_sites, site_count);
        if (overlap == 0) {
          continue;
        }
        const double density
            = static_cast<double>(
                  current_occupancy[((row + dy) * bin_count) + bin])
              * bin_inv_capacities[bin];
        demand += static_cast<double>(overlap) * density;
      }
    }
    return demand;
  };

  auto add_footprint_delta = [&](std::vector<int64_t>& delta,
                                 std::vector<int>& touched,
                                 const int row,
                                 const int site,
                                 const int width,
                                 const int height,
                                 const int sign) {
    const int first_bin = std::clamp(site / bin_sites, 0, bin_count - 1);
    const int last_bin = std::clamp(
        (std::max(site, site + width - 1)) / bin_sites, 0, bin_count - 1);
    for (int dy = 0; dy < height && row + dy < row_count; ++dy) {
      for (int bin = first_bin; bin <= last_bin; ++bin) {
        const int overlap
            = binOverlapSites(site, width, bin, bin_sites, site_count);
        if (overlap == 0) {
          continue;
        }
        const int idx = ((row + dy) * bin_count) + bin;
        if (delta[idx] == 0) {
          touched.push_back(idx);
        }
        delta[idx] += sign * overlap;
      }
    }
  };

  auto footprint_overflow_score
      = [&](const std::vector<int64_t>& current_occupancy,
            const std::vector<int64_t>* delta,
            const int row,
            const int site,
            const int width,
            const int height) {
          const int first_bin = std::clamp(site / bin_sites, 0, bin_count - 1);
          const int last_bin = std::clamp(
              (std::max(site, site + width - 1)) / bin_sites, 0, bin_count - 1);
          double overflow = 0.0;
          int covered_sites = 0;
          for (int dy = 0; dy < height && row + dy < row_count; ++dy) {
            for (int bin = first_bin; bin <= last_bin; ++bin) {
              const int overlap
                  = binOverlapSites(site, width, bin, bin_sites, site_count);
              if (overlap == 0) {
                continue;
              }
              const int idx = ((row + dy) * bin_count) + bin;
              int64_t occ = current_occupancy[idx];
              if (delta != nullptr) {
                occ += (*delta)[idx];
              }
              occ = std::max<int64_t>(0, occ);
              const double density
                  = static_cast<double>(occ) * bin_inv_capacities[bin];
              overflow += static_cast<double>(overlap)
                          * std::max(0.0, density - target_density);
              covered_sites += overlap;
            }
          }
          return covered_sites == 0
                     ? 0.0
                     : overflow / static_cast<double>(covered_sites);
        };

  auto hpwl_regression_penalty = [&](const GuidanceCell& gc,
                                     const int row,
                                     const int site) {
    if (gc.hpwl_terms.empty()) {
      return 0.0;
    }
    const int64_t left = gridToDbu(GridX{site}, site_width).v;
    const int64_t bottom = grid_->gridYToDbu(GridY{row}).v;
    const int64_t center_x
        = left + static_cast<int64_t>(gc.cell->getWidth().v) / 2;
    const int64_t center_y
        = bottom + static_cast<int64_t>(gc.cell->getHeight().v) / 2;
    return std::min(legalmHpwlRegressionPenaltySites(gc.hpwl_terms,
                                                     center_x,
                                                     center_y,
                                                     site_width.v),
                    max_hpwl_regression_penalty_sites);
  };

  int escape_attempted_cells = 0;
  int escape_moved_cells = 0;
  int escape_candidate_evals = 0;
  int escape_component_rejects = 0;
  int escape_triggered = 0;
  int escape_window_samples = 0;
  double escape_recent_overflow_bin_stddev = 0.0;
  auto apply_footprint_to_occupancy =
      [&](std::vector<int64_t>& current_occupancy,
          const int row,
          const int site,
          const int width,
          const int height,
          const int sign) {
        const int first_bin = std::clamp(site / bin_sites, 0, bin_count - 1);
        const int last_bin = std::clamp(
            (std::max(site, site + width - 1)) / bin_sites, 0, bin_count - 1);
        for (int dy = 0; dy < height && row + dy < row_count; ++dy) {
          for (int bin = first_bin; bin <= last_bin; ++bin) {
            const int overlap
                = binOverlapSites(site, width, bin, bin_sites, site_count);
            if (overlap == 0) {
              continue;
            }
            const int idx = ((row + dy) * bin_count) + bin;
            current_occupancy[idx] = std::max<int64_t>(
                0,
                current_occupancy[idx] + static_cast<int64_t>(sign) * overlap);
          }
        }
      };

  auto recent_overflow_bin_stddev = [&](const std::vector<int>& history) {
    if (history.size() < kLegalmLocalOptimumWindow) {
      return 0.0;
    }
    const int begin
        = static_cast<int>(history.size()) - kLegalmLocalOptimumWindow;
    double sum = 0.0;
    for (int idx = begin; idx < static_cast<int>(history.size()); ++idx) {
      sum += static_cast<double>(history[idx]);
    }
    const double mean = sum / static_cast<double>(kLegalmLocalOptimumWindow);
    double variance = 0.0;
    for (int idx = begin; idx < static_cast<int>(history.size()); ++idx) {
      const double diff = static_cast<double>(history[idx]) - mean;
      variance += diff * diff;
    }
    return std::sqrt(variance / static_cast<double>(kLegalmLocalOptimumWindow));
  };

  auto apply_connectivity_escape = [&](std::vector<int64_t>&
                                           current_occupancy) {
    ++escape_triggered;
    const int moved_before = escape_moved_cells;
    std::vector<std::vector<std::pair<double, int>>> congested_by_component(
        plate_component_count);
    std::vector<unsigned char> component_has_overflow(plate_component_count, 0);
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
      const GuidanceCell& gc = cells[i];
      const int source_component = footprint_component(target_rows[i],
                                                       target_sites[i],
                                                       gc.width_sites,
                                                       gc.height_rows,
                                                       gc.group);
      if (source_component < 0) {
        continue;
      }
      const double overflow = footprint_overflow_score(current_occupancy,
                                                       nullptr,
                                                       target_rows[i],
                                                       target_sites[i],
                                                       gc.width_sites,
                                                       gc.height_rows);
      if (overflow <= 0.0) {
        continue;
      }
      component_has_overflow[source_component] = 1;
      congested_by_component[source_component].push_back({overflow, i});
    }

    for (auto& component_cells : congested_by_component) {
      std::stable_sort(component_cells.begin(),
                       component_cells.end(),
                       [&](const auto& lhs, const auto& rhs) {
                         if (lhs.first != rhs.first) {
                           return lhs.first > rhs.first;
                         }
                         return cells[lhs.second].cell->getId()
                                < cells[rhs.second].cell->getId();
                       });
    }

    for (int source_component = 0; source_component < plate_component_count;
         ++source_component) {
      auto& component_cells = congested_by_component[source_component];
      if (component_cells.empty()) {
        continue;
      }
      const int relocate_count
          = (static_cast<int>(component_cells.size()) + 1) / 2;
      const int64_t source_area
          = std::max<int64_t>(1, component_area[source_component]);
      for (int order = 0; order < relocate_count; ++order) {
        const int i = component_cells[order].second;
        const GuidanceCell& gc = cells[i];
        ++escape_attempted_cells;

        int best_row = -1;
        int best_site = -1;
        int best_displacement = std::numeric_limits<int>::max();
        for (int dest_component = 0; dest_component < plate_component_count;
             ++dest_component) {
          if (dest_component == source_component
              || component_has_overflow[dest_component]
              || component_group[dest_component] != gc.group
              || component_area[dest_component] < 2 * source_area) {
            continue;
          }
          for (const int segment_idx :
               component_segment_indices[dest_component]) {
            const PlateSegment& segment = plate_segments[segment_idx];
            if (segment.x1 - segment.x0 < gc.width_sites
                || segment.row + gc.height_rows > row_count) {
              ++escape_component_rejects;
              continue;
            }
            const std::array<int, 3> sites{
                std::clamp(gc.x_site, segment.x0, segment.x1 - gc.width_sites),
                segment.x0,
                segment.x1 - gc.width_sites};
            for (const int site : sites) {
              if (site == target_sites[i] && segment.row == target_rows[i]) {
                continue;
              }
              if (footprint_component(segment.row,
                                      site,
                                      gc.width_sites,
                                      gc.height_rows,
                                      gc.group)
                  != dest_component) {
                ++escape_component_rejects;
                continue;
              }
              if (!canBePlaced(gc.cell, GridX{site}, GridY{segment.row})) {
                continue;
              }
              ++escape_candidate_evals;
              const int displacement
                  = std::abs(site - gc.x_site)
                    + row_equiv_sites * std::abs(segment.row - gc.row);
              if (displacement < best_displacement) {
                best_displacement = displacement;
                best_row = segment.row;
                best_site = site;
              }
            }
          }
        }
        if (best_row >= 0 && best_site >= 0) {
          apply_footprint_to_occupancy(current_occupancy,
                                       target_rows[i],
                                       target_sites[i],
                                       gc.width_sites,
                                       gc.height_rows,
                                       -1);
          target_rows[i] = best_row;
          target_sites[i] = best_site;
          apply_footprint_to_occupancy(current_occupancy,
                                       target_rows[i],
                                       target_sites[i],
                                       gc.width_sites,
                                       gc.height_rows,
                                       1);
          ++escape_moved_cells;
        }
      }
    }
    return escape_moved_cells > moved_before;
  };

  std::vector<std::vector<int>> partitions(partition_count);
  std::vector<int> partitioned_cell_indices;
  partitioned_cell_indices.reserve(cells.size());
  auto rebuild_partitions = [&](const int tp_scheme,
                                int& partitioned_cells,
                                int& boundary_excluded_cells) {
    const int site_shift = (tp_scheme * xhint_sites) / 3;
    const int row_shift = (tp_scheme * yhint_rows) / 3;
    for (auto& partition : partitions) {
      partition.clear();
    }
    partitioned_cell_indices.clear();
    partitioned_cells = 0;
    boundary_excluded_cells = 0;
    for (int i = 0; i < static_cast<int>(cells.size()); ++i) {
      const GuidanceCell& gc = cells[i];
      const int current_site = target_sites[i];
      const int current_row = target_rows[i];
      const int col0 = shifted_partition(current_site, site_shift, xhint_sites);
      const int col1 = shifted_partition(
          current_site + gc.width_sites - 1, site_shift, xhint_sites);
      const int row0 = shifted_partition(current_row, row_shift, yhint_rows);
      const int row1 = shifted_partition(
          current_row + gc.height_rows - 1, row_shift, yhint_rows);
      if (col0 != col1 || row0 != row1 || col0 < 0 || row0 < 0
          || col0 >= partition_cols || row0 >= partition_rows) {
        ++boundary_excluded_cells;
        continue;
      }
      partitions[(row0 * partition_cols) + col0].push_back(i);
      partitioned_cell_indices.push_back(i);
      ++partitioned_cells;
    }
  };

  std::vector<int> overflow_bin_history;
  std::vector<std::vector<int64_t>> worker_delta(
      thread_count, std::vector<int64_t>(row_bin_count, 0));
  std::vector<std::vector<int>> worker_touched_bins(thread_count);
  std::vector<std::vector<AlmAcceptedMove>> worker_accepted_moves(thread_count);
  for (int worker = 0; worker < thread_count; ++worker) {
    worker_touched_bins[worker].reserve(1024);
    worker_accepted_moves[worker].reserve(
        std::max<int>(1024, static_cast<int>(cells.size()) / thread_count));
  }
  std::vector<double> demand_cache(cells.size(), 0.0);
  std::vector<int> local_accepted_moves(thread_count, 0);
  std::vector<int> local_vertical_moves(thread_count, 0);
  std::vector<int64_t> local_candidate_evals(thread_count, 0);
  std::vector<int64_t> local_static_rejects(thread_count, 0);
  std::vector<int64_t> local_site_compat_rejects(thread_count, 0);
  std::vector<int64_t> local_tech_evals(thread_count, 0);
  std::vector<int64_t> local_edge_spacing_terms(thread_count, 0);
  std::vector<int64_t> local_pin_short_terms(thread_count, 0);
  std::vector<int64_t> local_pin_access_terms(thread_count, 0);
  int64_t total_incremental_occupancy_moves = 0;
  int64_t total_demand_scored_cells = 0;
  double alm_demand_ms = 0.0;
  double alm_sort_ms = 0.0;
  double alm_bgd_ms = 0.0;
  double alm_commit_ms = 0.0;
  double alm_stats_ms = 0.0;
  const auto alm_begin = Clock::now();
  for (int iter = 0; iter < alm_iteration_limit; ++iter) {
    const OverflowStats iter_start_stats = final_stats;
    const bool restart_optimizer = iter < kLegalmKely || iter == kLegalmKthre;
    const std::vector<double>& active_lambda = lambda;

    std::fill(local_accepted_moves.begin(), local_accepted_moves.end(), 0);
    std::fill(local_vertical_moves.begin(), local_vertical_moves.end(), 0);
    std::fill(local_candidate_evals.begin(), local_candidate_evals.end(), 0);
    std::fill(local_static_rejects.begin(), local_static_rejects.end(), 0);
    std::fill(
        local_site_compat_rejects.begin(), local_site_compat_rejects.end(), 0);
    std::fill(local_tech_evals.begin(), local_tech_evals.end(), 0);
    std::fill(
        local_edge_spacing_terms.begin(), local_edge_spacing_terms.end(), 0);
    std::fill(local_pin_short_terms.begin(), local_pin_short_terms.end(), 0);
    std::fill(local_pin_access_terms.begin(), local_pin_access_terms.end(), 0);
    const int tp_scheme = (iter / kLegalmKpart) % 3;
    const int site_shift = (tp_scheme * xhint_sites) / 3;
    const int row_shift = (tp_scheme * yhint_rows) / 3;
    int partitioned_cells = last_partitioned_cells;
    int boundary_excluded_cells = last_boundary_excluded_cells;
    if (iter % kLegalmKpart == 0 || iter == 0) {
      rebuild_partitions(tp_scheme, partitioned_cells, boundary_excluded_cells);
    }

    const auto demand_begin = Clock::now();
    parallelFor(static_cast<int>(partitioned_cell_indices.size()),
                thread_count,
                [&](int begin, int end, int) {
                  for (int pos = begin; pos < end; ++pos) {
                    const int i = partitioned_cell_indices[pos];
                    demand_cache[i] = cell_demand(occupancy, i);
                  }
                });
    const auto demand_end = Clock::now();
    alm_demand_ms += elapsed_ms(demand_begin, demand_end);
    total_demand_scored_cells
        += static_cast<int64_t>(partitioned_cell_indices.size());

    const auto sort_begin = Clock::now();
    parallelFor(partition_count, thread_count, [&](int begin, int end, int) {
      for (int part = begin; part < end; ++part) {
        auto& partition = partitions[part];
        std::stable_sort(partition.begin(),
                         partition.end(),
                         [&](const int lhs, const int rhs) {
                           const double lhs_demand = demand_cache[lhs];
                           const double rhs_demand = demand_cache[rhs];
                           if (lhs_demand != rhs_demand) {
                             return lhs_demand > rhs_demand;
                           }
                           return cells[lhs].cell->getId()
                                  < cells[rhs].cell->getId();
                         });
      }
    });
    const auto sort_end = Clock::now();
    alm_sort_ms += elapsed_ms(sort_begin, sort_end);

    const auto bgd_begin = Clock::now();
    parallelFor(
        partition_count, thread_count, [&](int begin, int end, int worker) {
          auto& local_delta = worker_delta[worker];
          auto& touched_bins = worker_touched_bins[worker];
          auto& accepted_move_records = worker_accepted_moves[worker];
          accepted_move_records.clear();
          int accepted_moves = 0;
          int vertical_moves = 0;
          int64_t candidate_evals = 0;
          int64_t static_rejects = 0;
          int64_t site_compat_rejects = 0;
          int64_t tech_evals = 0;
          int64_t edge_spacing_terms = 0;
          int64_t pin_short_terms = 0;
          int64_t pin_access_terms = 0;
          for (int part = begin; part < end; ++part) {
            touched_bins.clear();
            const int partition_row = part / partition_cols;
            const int partition_col = part % partition_cols;
            for (const int i : partitions[part]) {
              const GuidanceCell& gc = cells[i];
              const int current_site = target_sites[i];
              const int current_row = target_rows[i];
              const double current_overflow
                  = footprint_overflow_score(occupancy,
                                             &local_delta,
                                             current_row,
                                             current_site,
                                             gc.width_sites,
                                             gc.height_rows);
              const LegalmFootprint current_fp{
                  current_row,
                  current_site,
                  gc.width_sites,
                  gc.height_rows,
                  gc.group,
              };
              if (!restart_optimizer && current_overflow <= 0.0) {
                continue;
              }
              const double current_penalty
                  = almFootprintCost(occupancy,
                                     &local_delta,
                                     active_lambda,
                                     bin_inv_capacities,
                                     current_fp,
                                     current_fp,
                                     bin_count,
                                     bin_sites,
                                     site_count,
                                     target_density,
                                     alm_sigma);
              auto paper_tech_cost = [&](const int row, const int site) {
                if (!paper_tech_penalty_enabled) {
                  return 0.0;
                }
                const size_t orient_idx = (static_cast<size_t>(row)
                                           * static_cast<size_t>(site_count))
                                          + static_cast<size_t>(site);
                const auto orient
                    = odb::dbOrientType(site_sym_classes[gc.site_sym_class]
                                            .orientations[orient_idx]);
                const LegalmTechPenaltyResult tech_penalty
                    = computeLegalmTechPenalty(drc_engine_.get(),
                                               gc.cell,
                                               GridX{site},
                                               GridY{row},
                                               orient);
                ++tech_evals;
                edge_spacing_terms += tech_penalty.edge_spacing_violations;
                pin_short_terms += tech_penalty.pin_short_violations;
                pin_access_terms += tech_penalty.pin_access_violations;
                return tech_penalty.paperCost(kPtech, row_equiv_sites);
              };
              const int current_total_delta
                  = std::abs(current_site - gc.x_site)
                    + row_equiv_sites * std::abs(current_row - gc.row);
              const int current_tail = std::max(
                  0, current_total_delta - max_displacement_threshold_sites);
              const double current_cost
                  = current_penalty
                      + gc.height_class_weight
                            * static_cast<double>(current_total_delta)
                      + kAlphaMax * static_cast<double>(current_tail)
                      + kHpwlRegressionPenaltyWeight
                            * hpwl_regression_penalty(gc,
                                                       current_row,
                                                       current_site)
                      + paper_tech_cost(current_row, current_site);
              auto evaluate_finite_alm_candidate =
                  [&](const int candidate_row,
                      const int candidate_site,
                      const double incumbent_cost) {
                    if (!inside_partition(candidate_row,
                                          candidate_site,
                                          gc.width_sites,
                                          gc.height_rows,
                                          partition_col,
                                          partition_row,
                                          site_shift,
                                          row_shift)) {
                      return LegalmBgdEvaluation{};
                    }
                    const int total_delta
                        = std::abs(candidate_site - gc.x_site)
                          + row_equiv_sites * std::abs(candidate_row - gc.row);
                    const int candidate_tail = std::max(
                        0, total_delta - max_displacement_threshold_sites);
                    const double displacement_lower_bound
                        = gc.height_class_weight
                              * static_cast<double>(total_delta)
                          + kAlphaMax * static_cast<double>(candidate_tail);
                    if (displacement_lower_bound >= incumbent_cost) {
                      return LegalmBgdEvaluation{};
                    }
                    if (footprint_component(candidate_row,
                                            candidate_site,
                                            gc.width_sites,
                                            gc.height_rows,
                                            gc.group)
                        < 0) {
                      ++static_rejects;
                      return LegalmBgdEvaluation{};
                    }
                    if (!row_site_compatible(
                            gc, candidate_row, candidate_site)) {
                      ++site_compat_rejects;
                      return LegalmBgdEvaluation{};
                    }
                    ++candidate_evals;
                    const LegalmFootprint candidate_fp{
                        candidate_row,
                        candidate_site,
                        gc.width_sites,
                        gc.height_rows,
                        gc.group,
                    };
                    const double candidate_penalty
                        = almFootprintCost(occupancy,
                                           &local_delta,
                                           active_lambda,
                                           bin_inv_capacities,
                                           current_fp,
                                           candidate_fp,
                                           bin_count,
                                           bin_sites,
                                           site_count,
                                           target_density,
                                           alm_sigma);
                    const double candidate_base_cost
                        = candidate_penalty
                          + gc.height_class_weight
                                * static_cast<double>(total_delta)
                          + kAlphaMax * static_cast<double>(candidate_tail)
                          + kHpwlRegressionPenaltyWeight
                                * hpwl_regression_penalty(gc,
                                                          candidate_row,
                                                          candidate_site);
                    if (candidate_base_cost >= incumbent_cost) {
                      return LegalmBgdEvaluation{};
                    }
                    const double candidate_cost
                        = candidate_base_cost
                          + paper_tech_cost(candidate_row, candidate_site);
                    return LegalmBgdEvaluation{true, candidate_cost};
                  };

              const LegalmBgdCandidate best_candidate
                  = legalmBestBgdCandidate(candidate_stencil,
                                           current_row,
                                           current_site,
                                           gc.vertical_step_rows,
                                           gc.width_sites,
                                           gc.height_rows,
                                           row_count,
                                           site_count,
                                           current_cost,
                                           evaluate_finite_alm_candidate);
              const int best_site = best_candidate.site;
              const int best_row = best_candidate.row;

              if (best_site != current_site || best_row != current_row) {
                add_footprint_delta(local_delta,
                                    touched_bins,
                                    current_row,
                                    current_site,
                                    gc.width_sites,
                                    gc.height_rows,
                                    -1);
                add_footprint_delta(local_delta,
                                    touched_bins,
                                    best_row,
                                    best_site,
                                    gc.width_sites,
                                    gc.height_rows,
                                    1);
                accepted_move_records.push_back(
                    {i, current_row, current_site, best_row, best_site});
                ++accepted_moves;
                vertical_moves += best_row != current_row ? 1 : 0;
              }
            }
            for (const int idx : touched_bins) {
              local_delta[idx] = 0;
            }
          }
          local_accepted_moves[worker] = accepted_moves;
          local_vertical_moves[worker] = vertical_moves;
          local_candidate_evals[worker] = candidate_evals;
          local_static_rejects[worker] = static_rejects;
          local_site_compat_rejects[worker] = site_compat_rejects;
          local_tech_evals[worker] = tech_evals;
          local_edge_spacing_terms[worker] = edge_spacing_terms;
          local_pin_short_terms[worker] = pin_short_terms;
          local_pin_access_terms[worker] = pin_access_terms;
        });
    const auto bgd_end = Clock::now();
    alm_bgd_ms += elapsed_ms(bgd_begin, bgd_end);

    last_tp_scheme = tp_scheme;
    last_partitioned_cells = partitioned_cells;
    last_boundary_excluded_cells = boundary_excluded_cells;

    last_iter_moves = std::accumulate(
        local_accepted_moves.begin(), local_accepted_moves.end(), 0);
    total_accepted_moves += last_iter_moves;
    vertical_guided_moves += std::accumulate(
        local_vertical_moves.begin(), local_vertical_moves.end(), 0);
    total_candidate_evals += std::accumulate(
        local_candidate_evals.begin(), local_candidate_evals.end(), int64_t{0});
    total_static_candidate_rejects += std::accumulate(
        local_static_rejects.begin(), local_static_rejects.end(), int64_t{0});
    total_site_compat_candidate_rejects
        += std::accumulate(local_site_compat_rejects.begin(),
                           local_site_compat_rejects.end(),
                           int64_t{0});
    total_tech_candidate_evals += std::accumulate(
        local_tech_evals.begin(), local_tech_evals.end(), int64_t{0});
    total_edge_spacing_terms
        += std::accumulate(local_edge_spacing_terms.begin(),
                           local_edge_spacing_terms.end(),
                           int64_t{0});
    total_pin_short_terms += std::accumulate(
        local_pin_short_terms.begin(), local_pin_short_terms.end(), int64_t{0});
    total_pin_access_terms += std::accumulate(local_pin_access_terms.begin(),
                                              local_pin_access_terms.end(),
                                              int64_t{0});
    const auto commit_begin = Clock::now();
    for (const auto& moves : worker_accepted_moves) {
      for (const AlmAcceptedMove& move : moves) {
        const GuidanceCell& gc = cells[move.cell_idx];
        apply_footprint_to_occupancy(occupancy,
                                     move.old_row,
                                     move.old_site,
                                     gc.width_sites,
                                     gc.height_rows,
                                     -1);
        apply_footprint_to_occupancy(occupancy,
                                     move.new_row,
                                     move.new_site,
                                     gc.width_sites,
                                     gc.height_rows,
                                     1);
        target_rows[move.cell_idx] = move.new_row;
        target_sites[move.cell_idx] = move.new_site;
        ++total_incremental_occupancy_moves;
      }
    }
    const auto commit_end = Clock::now();
    alm_commit_ms += elapsed_ms(commit_begin, commit_end);

    const auto stats_begin = Clock::now();
    final_stats = collect_stats(occupancy);
    const auto stats_end = Clock::now();
    alm_stats_ms += elapsed_ms(stats_begin, stats_end);
    remember_best_targets(final_stats, iter);
    overflow_history.push_back(final_stats.total_overflow_sites);
    overflow_bin_history.push_back(final_stats.overflow_bins);

    if (final_stats.total_overflow_sites <= 0.0) {
      logger_->info(
          DPL,
          1212,
          "Differential guidance ALM iter {}: overflow {:.1f}->{:.1f} "
          "sites, bins {}->{}, max density {:.2f}->{:.2f}, accepted moves {}, "
          "lambda mode {}, hf {:.3f}.",
          iter,
          iter_start_stats.total_overflow_sites,
          final_stats.total_overflow_sites,
          iter_start_stats.overflow_bins,
          final_stats.overflow_bins,
          iter_start_stats.max_density,
          final_stats.max_density,
          last_iter_moves,
          restart_optimizer ? "alm_restart" : "alm",
          hf);
      break;
    }

    if (iter > kLegalmKthre && iter % kLegalmKh == 0) {
      const double lambda_sum
          = std::accumulate(lambda.begin(), lambda.end(), 0.0);
      const double lambda_avg
          = lambda_sum
            / static_cast<double>(std::max<int64_t>(1, lambda.size()));
      hf += kAlphaHf * lambda_avg;
    }

    if (overflow_bin_history.size() >= kLegalmLocalOptimumWindow) {
      escape_window_samples = kLegalmLocalOptimumWindow;
      escape_recent_overflow_bin_stddev
          = recent_overflow_bin_stddev(overflow_bin_history);
      if (escape_recent_overflow_bin_stddev == 0.0
          && apply_connectivity_escape(occupancy)) {
        final_stats = collect_stats(occupancy);
        overflow_history.back() = final_stats.total_overflow_sites;
        overflow_bin_history.back() = final_stats.overflow_bins;
      }
    }

    parallelFor(row_count, thread_count, [&](int begin, int end, int) {
      for (int row = begin; row < end; ++row) {
        for (int bin = 0; bin < bin_count; ++bin) {
          const int idx = (row * bin_count) + bin;
          const double density
              = static_cast<double>(occupancy[idx]) * bin_inv_capacities[bin];
          const double signed_overflow = density - target_density;
          const double clipped_violation
              = std::max(signed_overflow, -1.0 / alm_sigma);
          lambda[idx]
              = std::max(lambda[idx]
                             + hf
                                   * (clipped_violation
                                      + 0.5 * alm_sigma * clipped_violation
                                            * clipped_violation),
                         0.0);
        }
      }
    });

    logger_->info(
        DPL,
        1217,
        "Differential guidance ALM iter {}: overflow {:.1f}->{:.1f} "
        "sites, bins {}->{}, max density {:.2f}->{:.2f}, accepted moves {}, "
        "lambda mode {}, hf {:.3f}.",
        iter,
        iter_start_stats.total_overflow_sites,
        final_stats.total_overflow_sites,
        iter_start_stats.overflow_bins,
        final_stats.overflow_bins,
        iter_start_stats.max_density,
        final_stats.max_density,
        last_iter_moves,
        restart_optimizer ? "alm_restart" : "alm",
        hf);
  }

  const auto alm_end = Clock::now();
  const bool restored_best_targets
      = overflow_state_better(best_stats, final_stats);
  if (restored_best_targets) {
    target_sites = best_target_sites;
    target_rows = best_target_rows;
    final_stats = best_stats;
    logger_->info(DPL,
                  1219,
                  "Differential guidance restored best Stage 2 target field "
                  "from iter {} with overflow {:.1f} sites across {} bins.",
                  best_stats_iter,
                  final_stats.total_overflow_sites,
                  final_stats.overflow_bins);
  }
  std::vector<GuidanceTarget> targets(cells.size());
  parallelFor(static_cast<int>(cells.size()),
              thread_count,
              [&](int begin, int end, int) {
                for (int i = begin; i < end; ++i) {
                  const GuidanceCell& gc = cells[i];
                  GuidanceTarget target;
                  target.x = gridToDbu(GridX{target_sites[i]}, site_width).v;
                  target.y = grid_->gridYToDbu(GridY{target_rows[i]}).v;
                  target.shift_sites = target_sites[i] - gc.x_site;
                  target.shift_rows = target_rows[i] - gc.row;
                  target.active = true;
                  target.score
                      = std::abs(target.shift_sites)
                        + row_equiv_sites * std::abs(target.shift_rows);
                  targets[i] = target;
                }
              });

  guided_initial_locations_.assign(network_->getNumNodes(), {0, 0});
  guided_initial_valid_.assign(network_->getNumNodes(), 0);
  legalm_target_rows_.assign(network_->getNumNodes(), 0);
  legalm_target_sites_.assign(network_->getNumNodes(), 0);
  legalm_target_valid_.assign(network_->getNumNodes(), 0);
  source_edge_tension_target_rows_.assign(network_->getNumNodes(), 0);
  source_edge_tension_target_sites_.assign(network_->getNumNodes(), 0);
  source_edge_tension_target_valid_.assign(network_->getNumNodes(), 0);
  legalm_stage2_final_overflow_sites_ = final_stats.total_overflow_sites;
  legalm_stage2_final_overflow_bins_ = final_stats.overflow_bins;
  legalm_stage2_final_max_density_ = final_stats.max_density;
  legalm_stage2_overflow_free_ = final_stats.total_overflow_sites <= 0.0;
  const int full_target_cells = static_cast<int>(targets.size());
  int moved_cells = 0;
  int max_abs_shift = 0;
  int max_abs_row_shift = 0;
  int guided_row_moves = 0;
  int64_t total_abs_shift = 0;
  for (int i = 0; i < static_cast<int>(targets.size()); ++i) {
    const GuidanceTarget& target = targets[i];
    const DbuPt snapped
        = legalPt(cells[i].cell, {DbuX{target.x}, DbuY{target.y}});
    const int id = cells[i].cell->getId();
    if (id < 0 || id >= static_cast<int>(guided_initial_valid_.size())) {
      continue;
    }
    guided_initial_locations_[id] = {snapped.x.v, snapped.y.v};
    guided_initial_valid_[id] = 1;
    legalm_target_rows_[id] = target_rows[i];
    legalm_target_sites_[id] = target_sites[i];
    legalm_target_valid_[id] = 1;
    source_edge_tension_target_rows_[id] = target_rows[i];
    source_edge_tension_target_sites_[id] = target_sites[i];
    source_edge_tension_target_valid_[id] = 1;
    const int abs_shift = std::abs(target.shift_sites);
    const int abs_row_shift = std::abs(target.shift_rows);
    max_abs_shift = std::max(max_abs_shift, abs_shift);
    max_abs_row_shift = std::max(max_abs_row_shift, abs_row_shift);
    guided_row_moves += abs_row_shift > 0 ? 1 : 0;
    if (abs_shift != 0 || abs_row_shift != 0) {
      total_abs_shift += abs_shift + row_equiv_sites * abs_row_shift;
      ++moved_cells;
    }
  }

  const double avg_shift = moved_cells == 0
                               ? 0.0
                               : static_cast<double>(total_abs_shift)
                                     / static_cast<double>(moved_cells);
  const auto guidance_end = Clock::now();
  const double guidance_total_ms = elapsed_ms(guidance_start, guidance_end);
  const double alm_ms = elapsed_ms(alm_begin, alm_end);
  const double non_alm_ms = guidance_total_ms - alm_ms;

  logger_->metric("dpl_evolve__diff_guidance__status", 1);
  logger_->metric("dpl_evolve__diff_guidance__total_ms", guidance_total_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_loop_ms", alm_ms);
  logger_->metric("dpl_evolve__diff_guidance__non_alm_ms", non_alm_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_demand_ms", alm_demand_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_sort_ms", alm_sort_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_bgd_eval_ms", alm_bgd_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_commit_ms", alm_commit_ms);
  logger_->metric("dpl_evolve__diff_guidance__alm_collect_stats_ms",
                  alm_stats_ms);
  logger_->metric("dpl_evolve__diff_guidance__threads", thread_count);
  logger_->metric("dpl_evolve__diff_guidance__eligible_cells",
                  static_cast<int>(cells.size()));
  logger_->metric("dpl_evolve__diff_guidance__skipped_cells", skipped_cells);
  logger_->metric("dpl_evolve__diff_guidance__stage1_static_intervals",
                  static_interval_count);
  logger_->metric("dpl_evolve__diff_guidance__stage1_static_free_sites",
                  static_cast<double>(static_free_sites));
  logger_->metric("dpl_evolve__diff_guidance__plate_component_count",
                  plate_component_count);
  logger_->metric("dpl_evolve__diff_guidance__plate_max_component_sites",
                  static_cast<double>(max_plate_component_area));
  logger_->metric("dpl_evolve__diff_guidance__stage1_direct_cells",
                  stage1_direct_cells);
  logger_->metric("dpl_evolve__diff_guidance__stage1_projected_cells",
                  stage1_projected_cells);
  logger_->metric("dpl_evolve__diff_guidance__stage1_failed_projection_cells",
                  stage1_failed_projection_cells);
  logger_->metric("dpl_evolve__diff_guidance__stage1_row_projected_cells",
                  stage1_row_projected_cells);
  logger_->metric("dpl_evolve__diff_guidance__stage1_avg_shift_sites",
                  cells.empty() ? 0.0
                                : static_cast<double>(stage1_total_abs_shift)
                                      / static_cast<double>(cells.size()));
  logger_->metric("dpl_evolve__diff_guidance__stage1_max_site_shift",
                  stage1_max_site_shift);
  logger_->metric("dpl_evolve__diff_guidance__stage1_max_row_shift",
                  stage1_max_row_shift);
  logger_->metric("dpl_evolve__diff_guidance__moved_cells", moved_cells);
  logger_->metric("dpl_evolve__diff_guidance__candidate_moves", moved_cells);
  logger_->metric("dpl_evolve__diff_guidance__full_target_cells",
                  full_target_cells);
  logger_->metric("dpl_evolve__diff_guidance__alm_iterations",
                  alm_iteration_limit);
  logger_->metric("dpl_evolve__diff_guidance__paper_input_T",
                  alm_iteration_limit);
  logger_->metric("dpl_evolve__diff_guidance__default_input_T",
                  kDefaultAlmOuterIterations);
  logger_->metric("dpl_evolve__diff_guidance__paper_kpart", kLegalmKpart);
  logger_->metric("dpl_evolve__diff_guidance__paper_kely", kLegalmKely);
  logger_->metric("dpl_evolve__diff_guidance__paper_kthre", kLegalmKthre);
  logger_->metric("dpl_evolve__diff_guidance__paper_kh", kLegalmKh);
  logger_->metric("dpl_evolve__diff_guidance__paper_local_optimum_window",
                  kLegalmLocalOptimumWindow);
  logger_->metric("dpl_evolve__diff_guidance__paper_alpha_sigma", kAlphaSigma);
  logger_->metric("dpl_evolve__diff_guidance__paper_alpha_lambda",
                  kAlphaLambda);
  logger_->metric("dpl_evolve__diff_guidance__paper_alpha_hf", kAlphaHf);
  logger_->metric("dpl_evolve__diff_guidance__paper_alpha_max", kAlphaMax);
  logger_->metric("dpl_evolve__diff_guidance__paper_ptech", kPtech);
  logger_->metric("dpl_evolve__diff_guidance__hpwl_proxy_terms",
                  static_cast<double>(total_hpwl_proxy_terms));
  logger_->metric("dpl_evolve__diff_guidance__hpwl_proxy_weight",
                  kHpwlRegressionPenaltyWeight);
  logger_->metric("dpl_evolve__diff_guidance__hpwl_proxy_max_penalty_sites",
                  max_hpwl_regression_penalty_sites);
  logger_->metric("dpl_evolve__diff_guidance__paper_eq35_soft_penalty_enabled",
                  paper_tech_penalty_enabled ? 1 : 0);
  logger_->metric("dpl_evolve__diff_guidance__paper_eq35_candidate_evals",
                  static_cast<double>(total_tech_candidate_evals));
  logger_->metric("dpl_evolve__diff_guidance__paper_eq35_edge_spacing_terms",
                  static_cast<double>(total_edge_spacing_terms));
  logger_->metric("dpl_evolve__diff_guidance__paper_eq35_pin_short_terms",
                  static_cast<double>(total_pin_short_terms));
  logger_->metric("dpl_evolve__diff_guidance__paper_eq35_pin_access_terms",
                  static_cast<double>(total_pin_access_terms));
  logger_->metric("dpl_evolve__diff_guidance__paper_delta_threshold_rows", 3);
  logger_->metric("dpl_evolve__diff_guidance__paper_xhint_microns",
                  kPaper.xhint_microns);
  logger_->metric("dpl_evolve__diff_guidance__paper_yhint_rows",
                  kPaper.yhint_rows);
  logger_->metric(
      "dpl_evolve__diff_guidance__cpu_cap_candidate_vertical_radius",
      cpu_caps.candidate_vertical_radius);
  logger_->metric(
      "dpl_evolve__diff_guidance__cpu_cap_candidate_horizontal_steps",
      cpu_caps.candidate_horizontal_steps);
  logger_->metric("dpl_evolve__diff_guidance__paper_target_density",
                  target_density);
  logger_->metric("dpl_evolve__diff_guidance__alm_initial_overflow_bins",
                  initial_stats.overflow_bins);
  logger_->metric("dpl_evolve__diff_guidance__alm_final_overflow_bins",
                  final_stats.overflow_bins);
  logger_->metric("dpl_evolve__diff_guidance__alm_best_overflow_bins",
                  best_stats.overflow_bins);
  logger_->metric("dpl_evolve__diff_guidance__alm_initial_overflow_sites",
                  initial_stats.total_overflow_sites);
  logger_->metric("dpl_evolve__diff_guidance__alm_final_overflow_sites",
                  final_stats.total_overflow_sites);
  logger_->metric("dpl_evolve__diff_guidance__alm_best_overflow_sites",
                  best_stats.total_overflow_sites);
  logger_->metric("dpl_evolve__diff_guidance__alm_best_iter",
                  best_stats_iter);
  logger_->metric("dpl_evolve__diff_guidance__alm_best_target_updates",
                  best_stats_updates);
  logger_->metric("dpl_evolve__diff_guidance__alm_restored_best_target",
                  restored_best_targets ? 1 : 0);
  logger_->metric("dpl_evolve__diff_guidance__alm_bgd_candidate_evals",
                  static_cast<double>(total_candidate_evals));
  logger_->metric("dpl_evolve__diff_guidance__alm_bgd_static_rejects",
                  static_cast<double>(total_static_candidate_rejects));
  logger_->metric("dpl_evolve__diff_guidance__alm_bgd_site_compat_rejects",
                  static_cast<double>(total_site_compat_candidate_rejects));
  logger_->metric("dpl_evolve__diff_guidance__alm_bgd_accepted_moves",
                  static_cast<double>(total_accepted_moves));
  logger_->metric("dpl_evolve__diff_guidance__alm_incremental_occupancy_moves",
                  static_cast<double>(total_incremental_occupancy_moves));
  logger_->metric("dpl_evolve__diff_guidance__alm_demand_scored_cells",
                  static_cast<double>(total_demand_scored_cells));
  logger_->metric("dpl_evolve__diff_guidance__alm_reused_worker_delta_buffers",
                  1);
  logger_->metric("dpl_evolve__diff_guidance__shared_bgd_driver", 1);
  logger_->metric("dpl_evolve__diff_guidance__alm_last_iter_moves",
                  last_iter_moves);
  logger_->metric("dpl_evolve__diff_guidance__candidate_stencil_size",
                  static_cast<int>(candidate_stencil.size()));
  logger_->metric("dpl_evolve__diff_guidance__candidate_vertical_radius",
                  cpu_caps.candidate_vertical_radius);
  logger_->metric("dpl_evolve__diff_guidance__candidate_horizontal_steps",
                  cpu_caps.candidate_horizontal_steps);
  logger_->metric("dpl_evolve__diff_guidance__tp_scheme", last_tp_scheme);
  logger_->metric("dpl_evolve__diff_guidance__tp_partition_count",
                  partition_count);
  logger_->metric("dpl_evolve__diff_guidance__tp_xhint_sites", xhint_sites);
  logger_->metric("dpl_evolve__diff_guidance__tp_yhint_rows", yhint_rows);
  logger_->metric("dpl_evolve__diff_guidance__tp_partitioned_cells",
                  last_partitioned_cells);
  logger_->metric("dpl_evolve__diff_guidance__tp_boundary_excluded_cells",
                  last_boundary_excluded_cells);
  logger_->metric("dpl_evolve__diff_guidance__escape_triggered",
                  escape_triggered);
  logger_->metric("dpl_evolve__diff_guidance__escape_attempted_cells",
                  escape_attempted_cells);
  logger_->metric("dpl_evolve__diff_guidance__escape_moved_cells",
                  escape_moved_cells);
  logger_->metric("dpl_evolve__diff_guidance__escape_candidate_evals",
                  escape_candidate_evals);
  logger_->metric("dpl_evolve__diff_guidance__escape_component_rejects",
                  escape_component_rejects);
  logger_->metric("dpl_evolve__diff_guidance__escape_window_samples",
                  escape_window_samples);
  logger_->metric("dpl_evolve__diff_guidance__escape_overflow_bin_stddev",
                  escape_recent_overflow_bin_stddev);
  logger_->metric("dpl_evolve__diff_guidance__row_equiv_sites",
                  row_equiv_sites);
  logger_->metric("dpl_evolve__diff_guidance__max_disp_threshold_sites",
                  max_displacement_threshold_sites);
  logger_->metric("dpl_evolve__diff_guidance__average_initial_demand",
                  average_initial_demand);
  logger_->metric("dpl_evolve__diff_guidance__alm_sigma", alm_sigma);
  logger_->metric("dpl_evolve__diff_guidance__lambda_seed", lambda_seed);
  logger_->metric("dpl_evolve__diff_guidance__final_hf", hf);
  logger_->metric("dpl_evolve__diff_guidance__vertical_accepted_moves",
                  vertical_guided_moves);
  logger_->metric("dpl_evolve__diff_guidance__guided_row_moves",
                  guided_row_moves);
  logger_->metric("dpl_evolve__diff_guidance__overflow_bins",
                  final_stats.overflow_bins);
  logger_->metric("dpl_evolve__diff_guidance__bin_count", bin_count);
  logger_->metric("dpl_evolve__diff_guidance__max_density",
                  final_stats.max_density);
  logger_->metric("dpl_evolve__diff_guidance__target_density", target_density);
  logger_->metric("dpl_evolve__diff_guidance__avg_shift_sites", avg_shift);
  logger_->metric("dpl_evolve__diff_guidance__max_shift_sites", max_abs_shift);
  logger_->metric("dpl_evolve__diff_guidance__max_shift_rows",
                  max_abs_row_shift);
  logger_->info(
      DPL,
      1210,
      "Differential guidance prepared a complete {}-cell target field "
      "({} moved targets) using {} threads (overflow bins {} -> {}, "
      "max density {:.2f} -> {:.2f}, avg moved-target shift {:.2f} "
      "site-equivalent sites, row moves {}).",
      full_target_cells,
      moved_cells,
      thread_count,
      initial_stats.overflow_bins,
      final_stats.overflow_bins,
      initial_stats.max_density,
      final_stats.max_density,
      avg_shift,
      guided_row_moves);
  logger_->info(DPL,
                1218,
                "Differential guidance timing: total {:.2f} ms, ALM/BGD loop "
                "{:.2f} ms, non-ALM {:.2f} ms, demand {:.2f} ms, sort {:.2f} "
                "ms, BGD eval {:.2f} ms, commit {:.2f} ms, stats {:.2f} ms.",
                guidance_total_ms,
                alm_ms,
                non_alm_ms,
                alm_demand_ms,
                alm_sort_ms,
                alm_bgd_ms,
                alm_commit_ms,
                alm_stats_ms);
}

}  // namespace dpl_evolve
