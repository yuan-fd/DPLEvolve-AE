# SPDX-License-Identifier: BSD-3-Clause
# Copyright (c) 2018-2025, The OpenROAD Authors

include("openroad")
find_package(TCL)
find_package(Boost CONFIG)
find_package(LEMON NAMES LEMON lemon REQUIRED)

swig_lib(NAME         dpl_evolve
         NAMESPACE    dpl_evolve
         I_FILE       src/Opendp.i
         SCRIPTS      src/Opendp.tcl
         SWIG_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
                       ${CMAKE_CURRENT_SOURCE_DIR}/src
                       ${ODB_HOME}/src/swig/common
                       ${ODB_HOME}/src/swig/tcl
                       ${ODB_HOME}/include
)

add_library(dpl_evolve_lib
  src/Opendp.cpp
  src/dbToOpendp.cpp
  src/infrastructure/Grid.cpp
  src/CheckPlacement.cpp
  src/infrastructure/Objects.cpp
  src/infrastructure/Padding.cpp
  src/Place.cpp
  src/FillerPlacement.cpp
  src/DecapPlacement.cpp
  src/OptMirror.cpp
  src/PlacementDRC.cpp
  src/NegotiationLegalizer.cpp
  src/NegotiationLegalizerPass.cpp
  src/Optdp.cpp
  src/infrastructure/architecture.cxx
  src/util/color.cxx
  src/infrastructure/network.cxx
  src/util/utility.cxx
  src/legalize_shift.cxx
  src/optimization/detailed.cxx
  src/optimization/detailed_manager.cxx
  src/optimization/detailed_mis.cxx
  src/optimization/detailed_reorder.cxx
  src/optimization/detailed_global.cxx
  src/optimization/detailed_global_legacy.cxx
  src/optimization/detailed_vertical.cxx
  src/objective/detailed_displacement.cxx
  src/objective/detailed_hpwl.cxx
  src/objective/detailed_abu.cxx
  src/optimization/detailed_random.cxx
  src/optimization/detailed_orient.cxx
  src/util/journal.cxx
)

target_link_libraries(dpl_evolve_lib
  PUBLIC
    odb
  PRIVATE
    utl_lib
)

target_include_directories(dpl_evolve_lib
  PUBLIC
    include
    ${LEMON_INCLUDE_DIRS}
  PRIVATE
    src
)

target_sources(dpl_evolve
  PRIVATE
    src/MakeOpendp.cpp
    src/graphics/Graphics.cpp
    src/graphics/DplObserver.h
)

target_include_directories(dpl_evolve
  PUBLIC
    include
  PRIVATE
    src
)

target_link_libraries(dpl_evolve
  PRIVATE
    ${TCL_LIBRARY}
    dpl_evolve_lib
    gui
)

messages(
  TARGET dpl_evolve
)

################################################################

add_custom_target(opendp_evolve_tags etags -o TAGS
  ${OPENDP_SRC}
  ${OPENDP_HOME}/include/dpl_evolve/*.h
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
)

if (Python3_FOUND AND BUILD_PYTHON)
  swig_lib(NAME          dpl_evolve_py
           NAMESPACE     dpl_evolve
           LANGUAGE      python
           I_FILE        src/Opendp-py.i
           SWIG_INCLUDES ${CMAKE_CURRENT_SOURCE_DIR}/include
                         ${CMAKE_CURRENT_SOURCE_DIR}/src
                         ${ODB_HOME}/src/swig/common
                         ${ODB_HOME}/src/swig/python
           SCRIPTS       ${CMAKE_CURRENT_BINARY_DIR}/dpl_evolve_py.py
  )

  target_link_libraries(dpl_evolve_py
    PUBLIC
      ${Boost_LIBRARIES}
      dpl_evolve_lib
      gui
      odb
  )

endif()
