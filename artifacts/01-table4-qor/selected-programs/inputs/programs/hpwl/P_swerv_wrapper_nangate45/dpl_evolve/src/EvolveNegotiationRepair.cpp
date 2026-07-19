// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2018-2025, The OpenROAD Authors

#include "LegalmCommon.h"
#include "NegotiationLegalizer.h"

namespace dpl_evolve {

// Explicit policy-assisted repair after the paper-style LEGALM path.  This is
// not a LEGALM paper stage.  LEGALM writes Opendp Node positions first, so sync
// them to ODB before the negotiation legalizer imports its compact DB-backed
// state.
bool Opendp::runEvolveNegotiationRepair(const bool run_abacus)
{
  updateDbInstLocations();
  initGrid();
  setFixedGridCells();
  if (!arch_->getRegions().empty()) {
    groupInitPixels2();
    groupInitPixels();
  }

  logger_->info(DPL,
                1215,
                "DPL-Evolve repair stage: NegotiationLegalizer with Abacus {}.",
                run_abacus ? "enabled" : "disabled");

  NegotiationLegalizer negotiation(this,
                                   db_,
                                   logger_,
                                   padding_.get(),
                                   debug_observer_.get(),
                                   network_.get());
  negotiation.setRunAbacus(run_abacus);
  negotiation.legalize();
  negotiation.setDplPositions();
  reportEvolvePlacementMetrics(run_abacus ? "abacus_negotiation_repair"
                                          : "negotiation_repair");

  const int violations = negotiation.numViolations();
  logger_->metric("dpl_evolve__repair__negotiation_violations", violations);
  if (violations > 0) {
    logger_->warn(DPL,
                  1216,
                  "DPL-Evolve negotiation repair left {} placement violations.",
                  violations);
    return false;
  }
  // LEGALM may have recorded cells that its interval legalizer could not place.
  // Negotiation repair has just checked the final state, so stale LEGALM
  // failure records must not make the top-level DPL error path report a clean
  // repair as failed.
  placement_failures_.clear();
  return true;
}

}  // namespace dpl_evolve
