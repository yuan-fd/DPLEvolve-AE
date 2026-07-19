// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmTechPenalty.h"

#include <algorithm>

#include "PlacementDRC.h"
#include "infrastructure/Objects.h"

namespace dpl_evolve {

int LegalmTechPenaltyResult::totalViolations() const
{
  return edge_spacing_violations + pin_short_violations + pin_access_violations;
}

double LegalmTechPenaltyResult::paperCost(const double ptech,
                                          const int row_equiv_sites) const
{
  return ptech * static_cast<double>(std::max(1, row_equiv_sites))
         * static_cast<double>(totalViolations());
}

LegalmTechPenaltyResult computeLegalmTechPenalty(const PlacementDRC* drc,
                                                 const Node* cell,
                                                 const GridX x,
                                                 const GridY y,
                                                 odb::dbOrientType orient)
{
  LegalmTechPenaltyResult result;
  if (drc == nullptr || cell == nullptr || cell->getMaster() == nullptr
      || cell->getMaster()->getEdges().empty()) {
    return result;
  }

  if (drc->hasCellEdgeSpacingTable()) {
    result.edge_spacing_violations
        = drc->countEdgeSpacingViolations(cell, x, y, orient);
  }

  // Pin-short and pin-access terms require a routing/pin-access cache.  They
  // stay zero here until that cache exists, rather than folding in unrelated
  // OpenROAD detailed-placement DRC categories.
  return result;
}

}  // namespace dpl_evolve
