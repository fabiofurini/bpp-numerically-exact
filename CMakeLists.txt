cmake_minimum_required(VERSION 3.20)
project(bpp_numerically_exact VERSION 0.1.0 LANGUAGES CXX)

option(BPP_BUILD_TESTS "Build unit tests" ON)
option(BPP_ENABLE_CPLEX "Enable the CPLEX column-generation backend" OFF)
option(BPP_ENABLE_GUROBI "Enable the optional Gurobi column-generation backend (alternative to CPLEX; see MasterRmp)" OFF)
option(BPP_ENABLE_SOPLEX "Enable mandatory rational SoPlex certification (required for numerically exact runs)" OFF)
option(BPP_ENABLE_GMP "Enable exact rational safe-bound arithmetic" ON)

if(BPP_ENABLE_CPLEX AND NOT BPP_ENABLE_SOPLEX)
  message(FATAL_ERROR
    "BPP_ENABLE_SOPLEX=ON is required with BPP_ENABLE_CPLEX=ON: "
    "CPLEX-only mode cannot claim numerically exact bounds")
endif()
if(BPP_ENABLE_GUROBI AND NOT BPP_ENABLE_SOPLEX)
  message(FATAL_ERROR
    "BPP_ENABLE_SOPLEX=ON is required with BPP_ENABLE_GUROBI=ON: "
    "Gurobi-only mode cannot claim numerically exact bounds either")
endif()

# Building with neither, either, or both of BPP_ENABLE_CPLEX/BPP_ENABLE_GUROBI
# is supported: MasterRmp (include/bpp/master_rmp.hpp) selects at runtime
# between whichever were compiled in. Running the complete test suite (in
# particular the Gurobi-vs-CPLEX equivalence checks in
# tests/test_integration.cpp) needs both; either alone is enough for normal
# use.

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(bpp_core
  src/core/instance.cpp
  src/core/pattern.cpp
  src/core/preprocessing.cpp
  src/core/solution.cpp
  src/cuts/sr3.cpp
  src/master/cplex_rmp.cpp
  src/master/gurobi_rmp.cpp
  src/master/master_rmp.cpp
  src/master/soplex_rmp.cpp
  src/master/column_generation.cpp
  src/master/safe_bound.cpp
  src/io/instance_reader.cpp
  src/io/solution_writer.cpp
  src/pricing/floating_root_pricer.cpp
  src/search/branching.cpp
  src/search/branch_and_price.cpp
  src/search/node_queue.cpp
  src/solver/greedy_solver.cpp
  src/solver/heuristics.cpp
)
target_include_directories(bpp_core PUBLIC include)

if(BPP_ENABLE_GMP)
  find_library(GMP_LIBRARY NAMES gmp)
  find_library(GMPXX_LIBRARY NAMES gmpxx)
  if(GMP_LIBRARY AND GMPXX_LIBRARY)
    target_compile_definitions(bpp_core PUBLIC BPP_HAS_GMP=1)
    target_link_libraries(bpp_core PUBLIC ${GMPXX_LIBRARY} ${GMP_LIBRARY})
  else()
    message(WARNING "GMP not found; exact SafeBound operations will be unavailable")
  endif()
endif()

add_executable(bpp-solve src/cli/main.cpp)
target_link_libraries(bpp-solve PRIVATE bpp_core)

if(BPP_ENABLE_SOPLEX)
  # Shared by both floating backends (CPLEX and/or Gurobi): whichever one(s)
  # produce the fast-phase incumbent, the mandatory rational safe-bound
  # certification phase is the same SoPlex/GMP code either way.
  set(SOPLEX_ROOT "${CMAKE_CURRENT_SOURCE_DIR}/legacy/soplex-5.0.1")
  set(SOPLEX_BUILD_ROOT "${SOPLEX_ROOT}/build-large" CACHE PATH
      "SoPlex build directory containing lib/libsoplex-pic.a")
  target_compile_definitions(bpp_core PUBLIC BPP_HAS_SOPLEX=1 SOPLEX_WITH_GMP)
  target_include_directories(bpp_core PUBLIC "${SOPLEX_ROOT}/src")
  target_link_directories(bpp_core PUBLIC "${SOPLEX_BUILD_ROOT}/lib")
  target_link_libraries(bpp_core PUBLIC soplex-pic gmpxx gmp z)
endif()

if(BPP_ENABLE_CPLEX)
  if(NOT CPLEX_ROOT)
    message(FATAL_ERROR "CPLEX is required for the column-generation backend. Set -DCPLEX_ROOT=/path/to/CPLEX_Studio...")
  endif()
  message(STATUS "CPLEX backend will be enabled from ${CPLEX_ROOT}")
  target_compile_definitions(bpp_core PUBLIC BPP_HAS_CPLEX=1)
  target_include_directories(bpp_core PRIVATE "${CPLEX_ROOT}/cplex/include")
  target_link_directories(bpp_core PUBLIC "${CPLEX_ROOT}/cplex/lib/x86-64_linux/static_pic")
  target_link_libraries(bpp_core PUBLIC cplex m pthread dl)

  set(LEGACY_SRC
    legacy/src/Main.cpp legacy/src/DP.cpp legacy/src/DP_POP.cpp legacy/src/DP_POP_HEUR.cpp
    legacy/src/POPULATE_MASTERS.cpp legacy/src/BPPS_BP_MASTER.cpp legacy/src/BPPS_BP_PRICER.cpp
    legacy/src/BPPS_BP_TREE.cpp legacy/src/BPPS_BP_TRIPLETS.cpp legacy/src/BPPS_BP_DIVING.cpp
    legacy/src/BPPS_BP_LP_HEUR.cpp legacy/src/BPPS_BP_MAPPING.cpp legacy/src/BPPS_BP_MAGIC_STAB.cpp
    legacy/src/BPPS_BP_MASTER_SOPLEX.cpp legacy/src/REDUCTIONS.cpp legacy/src/preprocessing.cpp
    legacy/src/global_functions.cpp legacy/src/BPPS_FORM_B.cpp legacy/src/BPPS_FORM_A.cpp
    legacy/source/errors.cpp legacy/source/dp_master.cpp legacy/source/heap.cpp
    legacy/source/mckpsc-ls.cpp legacy/source/sort.cpp legacy/source/tools.cpp)
  add_executable(bpp-solve-legacy ${LEGACY_SRC})
  target_include_directories(bpp-solve-legacy PRIVATE legacy/src legacy/include legacy/source
    "${CPLEX_ROOT}/cplex/include" legacy/soplex-5.0.1/src)
  target_compile_definitions(bpp-solve-legacy PRIVATE IL_STD SOPLEX_WITH_GMP)
  target_compile_options(bpp-solve-legacy PRIVATE -mcmodel=large -ffunction-sections -fdata-sections)
  target_link_directories(bpp-solve-legacy PRIVATE "${CPLEX_ROOT}/cplex/lib/x86-64_linux/static_pic" "${SOPLEX_BUILD_ROOT}/lib")
  target_link_options(bpp-solve-legacy PRIVATE -no-pie -Wl,--no-relax -Wl,--gc-sections)
  target_link_libraries(bpp-solve-legacy PRIVATE ilocplex cplex soplex-pic gmp gmpxx z m pthread dl)
endif()

if(BPP_ENABLE_GUROBI)
  if(NOT GUROBI_ROOT)
    message(FATAL_ERROR "Gurobi is required for the Gurobi backend. Set -DGUROBI_ROOT=/path/to/gurobiNNN/linux64")
  endif()
  # Gurobi's shared library is versioned (e.g. libgurobi130.so for 13.0.x),
  # unlike CPLEX's stable `libcplex.a` name -- detect whichever is actually
  # installed instead of hardcoding one. The paper's own official code pins
  # Gurobi 9.5.1 specifically (see ../official-reference-BCCF/README.md,
  # outside this repository); this project does not pin a version.
  file(GLOB BPP_GUROBI_CANDIDATE_LIBS "${GUROBI_ROOT}/lib/libgurobi[0-9][0-9].so" "${GUROBI_ROOT}/lib/libgurobi[0-9][0-9][0-9].so")
  if(NOT BPP_GUROBI_CANDIDATE_LIBS)
    message(FATAL_ERROR "No libgurobiNN.so found under ${GUROBI_ROOT}/lib")
  endif()
  list(GET BPP_GUROBI_CANDIDATE_LIBS 0 BPP_GUROBI_LIB_PATH)
  get_filename_component(BPP_GUROBI_LIB_NAME "${BPP_GUROBI_LIB_PATH}" NAME_WE)
  string(REGEX REPLACE "^lib" "" BPP_GUROBI_LIB_NAME "${BPP_GUROBI_LIB_NAME}")
  message(STATUS "Gurobi backend will be enabled from ${GUROBI_ROOT} (lib${BPP_GUROBI_LIB_NAME})")
  target_compile_definitions(bpp_core PUBLIC BPP_HAS_GUROBI=1)
  target_include_directories(bpp_core PRIVATE "${GUROBI_ROOT}/include")
  target_link_directories(bpp_core PUBLIC "${GUROBI_ROOT}/lib")
  target_link_libraries(bpp_core PUBLIC ${BPP_GUROBI_LIB_NAME} m pthread dl)
endif()

include(CTest)
if(BPP_BUILD_TESTS)
  # Three distinct suites instead of one monolithic target: unit (isolated
  # components, always built/run), integration (multi-component solve_*
  # pipelines through the CPLEX/Gurobi/SoPlex backends, requires
  # BPP_ENABLE_CPLEX and/or BPP_ENABLE_GUROBI), regression (pinned to
  # specific previously-fixed bugs). See each file's header comment for the
  # exact scope.
  add_executable(bpp-unit-tests tests/test_unit.cpp)
  target_link_libraries(bpp-unit-tests PRIVATE bpp_core)
  add_test(NAME bpp-unit-tests COMMAND bpp-unit-tests)

  add_executable(bpp-integration-tests tests/test_integration.cpp)
  target_link_libraries(bpp-integration-tests PRIVATE bpp_core)
  target_compile_definitions(bpp-integration-tests PRIVATE
    BPP_SOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}")
  add_test(NAME bpp-integration-tests COMMAND bpp-integration-tests)

  add_executable(bpp-regression-tests tests/test_regression.cpp)
  target_link_libraries(bpp-regression-tests PRIVATE bpp_core)
  add_test(NAME bpp-regression-tests COMMAND bpp-regression-tests)
endif()

install(TARGETS bpp-solve RUNTIME DESTINATION bin)
install(DIRECTORY include/ DESTINATION include)
