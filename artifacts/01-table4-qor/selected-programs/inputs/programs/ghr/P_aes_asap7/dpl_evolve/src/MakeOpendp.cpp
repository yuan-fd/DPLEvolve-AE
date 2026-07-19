// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2019-2025, The OpenROAD Authors

#include "dpl_evolve/MakeOpendp.h"

#include "tcl.h"
#include "utl/decode.h"

extern "C" {
extern int Dpl_evolve_Init(Tcl_Interp* interp);
}

namespace dpl_evolve {

// Tcl files encoded into strings.
extern const char* dpl_evolve_tcl_inits[];

void initOpendp(Tcl_Interp* tcl_interp)
{
  // Define swig TCL commands.
  Dpl_evolve_Init(tcl_interp);
  // Eval encoded sta TCL sources.
  utl::evalTclInit(tcl_interp, dpl_evolve::dpl_evolve_tcl_inits);
}

}  // namespace dpl_evolve
