// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#pragma once

#include "infrastructure/Coordinates.h"
#include "odb/dbTypes.h"

namespace dpl_evolve {

class Node;
class PlacementDRC;

struct LegalmTechPenaltyResult
{
  int edge_spacing_violations = 0;
  int pin_short_violations = 0;
  int pin_access_violations = 0;

  int totalViolations() const;
  double paperCost(double ptech, int row_equiv_sites) const;
};

// LEGALM 2.0 Eq. (35) uses V_i,t,j, the count of technology/routability
// constraint violations for placing subcell t of cell i at candidate site j
// while all other cells stay fixed.  This helper intentionally exposes only
// the paper terms that are implemented exactly in this CPU path.  Do not use
// PlacementDRC::countDRCViolations here: that OpenROAD aggregate includes
// padding, blocked-layer and one-site-gap categories that are not Eq. (35).
LegalmTechPenaltyResult computeLegalmTechPenalty(const PlacementDRC* drc,
                                                 const Node* cell,
                                                 GridX x,
                                                 GridY y,
                                                 odb::dbOrientType orient);

}  // namespace dpl_evolve
