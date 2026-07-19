// SPDX-License-Identifier: BSD-3-Clause
// Copyright (c) 2022-2025, The OpenROAD Authors

%module dpl_evolve_py

%{

#include "ord/OpenRoad.hh"
#include "dpl_evolve/Opendp.h"
#include "utl/Logger.h"

using std::vector;
using namespace odb;

%}

%include "../../Exception-py.i"

%include <std_unordered_map.i>
%include <std_set.i>

%import "odb.i"
%import "odb/dbTypes.h"

%include "dpl_evolve/Opendp.h"
