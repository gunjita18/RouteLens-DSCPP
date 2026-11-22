
cmake_minimum_required(VERSION 3.16)

project(RouteLens LANGUAGES C CXX)

set(CROW_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CROW_BUILD_TESTS OFF CACHE BOOL "" FORCE)
add_subdirectory(external/Crow)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

enable_testing()

# ---------------------------------------------------------------- hand-written C core (Aayush, Phase III)
# Add each C test with one line:  routelens_c_test(test_name tests/test_name.c)
# It builds the test against routelens_core, registers it with CTest (label
# "c_core") and runs it from the repo root (so tests can find data/osm/*.csv).
# With -DROUTELENS_VALGRIND=ON a second test runs it under Valgrind and fails
# on any memory leak or invalid memory access (CI does this).
option(ROUTELENS_VALGRIND "Also run every C test under Valgrind" OFF)
if(ROUTELENS_VALGRIND)
    find_program(VALGRIND_EXECUTABLE valgrind REQUIRED)
endif()

add_library(routelens_core STATIC
    src/core/trie.c
)
target_include_directories(routelens_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_options(routelens_core PRIVATE -Wall -Wextra -pedantic)

function(routelens_c_test name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE routelens_core)
    target_compile_options(${name} PRIVATE -Wall -Wextra -pedantic)
    add_test(NAME ${name} COMMAND ${name} WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
    set_tests_properties(${name} PROPERTIES LABELS c_core)
    if(ROUTELENS_VALGRIND)
        add_test(NAME ${name}_valgrind
                 COMMAND ${VALGRIND_EXECUTABLE} --leak-check=full --show-leak-kinds=all
                         --errors-for-leak-kinds=all --error-exitcode=1 $<TARGET_FILE:${name}>
                 WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
        set_tests_properties(${name}_valgrind PROPERTIES LABELS "c_core;valgrind")
    endif()
endfunction()

# Trie for place-name autocomplete (/api/places, search box on the website)
routelens_c_test(test_trie tests/test_trie.c)

add_executable(test_request_params
    tests/test_request_params.cpp
)
target_include_directories(test_request_params PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_compile_options(test_request_params PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RequestParamsTests COMMAND test_request_params)

find_package(Threads REQUIRED)

# "Traffic data: <time>" label rule.
add_executable(test_traffic_data
    tests/test_traffic_data.cpp
)
target_include_directories(test_traffic_data PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_compile_options(test_traffic_data PRIVATE -Wall -Wextra -pedantic)
add_test(NAME TrafficDataLabelTests COMMAND test_traffic_data)

# Interface-preview provider (sample routes, clearly labelled).
add_executable(test_preview_provider
    tests/test_preview_provider.cpp
)
target_include_directories(test_preview_provider PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_compile_options(test_preview_provider PRIVATE -Wall -Wextra -pedantic)
add_test(NAME PreviewProviderTests COMMAND test_preview_provider)

# Road network: the map-data module's graph and OSM loader, plus the
# linear-search road snapper that connects clicked points to it.
add_library(routelens_osm STATIC
    src/data/osm_loader.cpp
    src/data/geo.cpp
    src/spatial/road_snapper.cpp
    src/algorithms/dijkstra.cpp
    src/algorithms/astar.cpp
    src/algorithms/yen.cpp
    src/algorithms/penalty.cpp
    src/algorithms/scenario.cpp
    src/algorithms/route_ranking.cpp
)
target_compile_options(routelens_osm PRIVATE -Wall -Wextra)
target_link_libraries(routelens_osm PUBLIC routelens_ds)

# Runs on a small built-in network; also checks the real Dehradun CSVs when
# data/osm/nodes.csv and edges.csv are present (they are not in git).
add_executable(test_road_snapper
    tests/test_road_snapper.cpp
)
target_include_directories(test_road_snapper PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spatial
)
target_link_libraries(test_road_snapper PRIVATE routelens_osm)
target_compile_options(test_road_snapper PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RoadSnapperTests COMMAND test_road_snapper
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})

add_executable(routelens_server
    src/server/main.cpp
    src/server/osm_route_provider.cpp
)
target_link_libraries(routelens_server PRIVATE Crow::Crow routelens_osm routelens_traffic routelens_routing routelens_core)
target_compile_options(routelens_server PRIVATE -Wall -Wextra -pedantic)


# ---------------------------------------------------------------- road network tools and tests
# Use the routelens_osm library defined above.
add_executable(routelens_osm_info src/app/osm_info_main.cpp)
target_link_libraries(routelens_osm_info PRIVATE routelens_osm)
target_compile_options(routelens_osm_info PRIVATE -Wall -Wextra -pedantic)

add_executable(test_osm tests/test_osm.cpp)
target_link_libraries(test_osm PRIVATE routelens_osm)
target_compile_options(test_osm PRIVATE -Wall -Wextra -pedantic)
add_test(NAME OsmLoaderTests COMMAND test_osm
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
# Dijkstra shortest-path tests
add_executable(test_dijkstra tests/test_dijkstra.cpp)
target_link_libraries(test_dijkstra PRIVATE routelens_osm)
target_compile_options(test_dijkstra PRIVATE -Wall -Wextra -pedantic)
add_test(NAME DijkstraTests COMMAND test_dijkstra)
# A* shortest-path tests
add_executable(test_astar tests/test_astar.cpp)
target_link_libraries(test_astar PRIVATE routelens_osm)
target_compile_options(test_astar PRIVATE -Wall -Wextra -pedantic)
add_test(NAME AStarTests COMMAND test_astar)
# A* heuristic: same optimal cost as Dijkstra on random graphs, fewer expansions
add_executable(test_astar_heuristic tests/test_astar_heuristic.cpp)
target_link_libraries(test_astar_heuristic PRIVATE routelens_osm)
target_compile_options(test_astar_heuristic PRIVATE -Wall -Wextra -pedantic)
add_test(NAME AStarHeuristicTests COMMAND test_astar_heuristic)
# Penalty-method alternative routes
add_executable(test_penalty tests/test_penalty.cpp)
target_link_libraries(test_penalty PRIVATE routelens_osm)
target_compile_options(test_penalty PRIVATE -Wall -Wextra -pedantic)
add_test(NAME PenaltyTests COMMAND test_penalty)
# What-if scenario mode: closures, slowdowns, BFS reachability
add_executable(test_scenario tests/test_scenario.cpp)
target_link_libraries(test_scenario PRIVATE routelens_osm)
target_compile_options(test_scenario PRIVATE -Wall -Wextra -pedantic)
add_test(NAME ClosureTests COMMAND test_scenario)
# Route provider: penalty + Yen candidates, blocked and slowed roads, major
# junctions and traffic scenarios (those come from routelens_traffic)
add_executable(test_route_provider
    tests/test_route_provider.cpp
    src/server/osm_route_provider.cpp
)
target_link_libraries(test_route_provider PRIVATE routelens_traffic routelens_routing)
target_compile_options(test_route_provider PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RouteProviderTests COMMAND test_route_provider)
# Algorithm evaluation harness (see evaluation/run_evaluation.sh)
add_executable(routelens_eval src/app/eval_main.cpp)
target_link_libraries(routelens_eval PRIVATE routelens_osm routelens_routing)
target_compile_options(routelens_eval PRIVATE -Wall -Wextra -pedantic)
# Yen's K-shortest loopless paths tests
add_executable(test_yen tests/test_yen.cpp)
target_link_libraries(test_yen PRIVATE routelens_osm)
target_compile_options(test_yen PRIVATE -Wall -Wextra -pedantic)
add_test(NAME YenTests COMMAND test_yen)
# Pareto filtering and route ranking tests
add_executable(test_route_ranking tests/test_route_ranking.cpp)
target_link_libraries(test_route_ranking PRIVATE routelens_osm)
target_compile_options(test_route_ranking PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RouteRankingTests COMMAND test_route_ranking)
# ---------------------------------------------------------------- traffic module (rules, per-edge state, ETA)
add_library(routelens_traffic STATIC
    src/traffic/traffic.cpp
    src/traffic/eta.cpp
    src/traffic/junctions.cpp
    src/oop/scenario.cpp
    src/oop/traffic_provider.cpp
    src/data/geo.cpp
)
target_link_libraries(routelens_traffic PUBLIC routelens_osm)
target_compile_options(routelens_traffic PRIVATE -Wall -Wextra -pedantic)

add_executable(test_traffic tests/test_traffic.cpp)
target_link_libraries(test_traffic PRIVATE routelens_traffic)
target_compile_options(test_traffic PRIVATE -Wall -Wextra -pedantic)
add_test(NAME TrafficTests COMMAND test_traffic)

# ---------------------------------------------------------------- major junctions
add_executable(routelens_junctions_info src/app/junctions_info_main.cpp)
target_link_libraries(routelens_junctions_info PRIVATE routelens_traffic)
target_compile_options(routelens_junctions_info PRIVATE -Wall -Wextra -pedantic)

add_executable(test_junctions tests/test_junctions.cpp)
target_link_libraries(test_junctions PRIVATE routelens_traffic)
target_compile_options(test_junctions PRIVATE -Wall -Wextra -pedantic)
add_test(NAME JunctionTests COMMAND test_junctions)

# ---------------------------------------------------------------- scenarios
add_executable(test_scenarios tests/test_scenarios.cpp)
target_link_libraries(test_scenarios PRIVATE routelens_traffic)
target_compile_options(test_scenarios PRIVATE -Wall -Wextra -pedantic)
add_test(NAME ScenarioTests COMMAND test_scenarios)


# ---------------------------------------------------------------- keep assert() in tests
# The tests check results with assert(). A Release build defines NDEBUG, which
# compiles every assert away: the tests would then pass without checking
# anything (CI builds Release), or crash where an assert wraps a call that the
# test needs. Keep asserts on for all test targets; library code is unaffected.
# Keep this block after the last test target.
get_directory_property(_rl_all_targets BUILDSYSTEM_TARGETS)
foreach(_rl_target IN LISTS _rl_all_targets)
    if(_rl_target MATCHES "^test_")
        target_compile_options(${_rl_target} PRIVATE -UNDEBUG)
    endif()
endforeach()

# ---------------------------------------------------------------- Hand-written C data structures
# Pure C11, no STL. One .c/.h pair per structure, one test file per structure.
add_library(routelens_ds STATIC
    src/core/dynarray.c
    src/core/list.c
    src/core/hashtable.c
    src/core/queue.c
    src/core/bfs.c
    src/core/traffic_reader.c
    src/core/search.c
    src/core/dsu.c
    src/core/graph.c
    src/core/minheap.c
    src/core/stack.c
    src/core/sort.c
    src/core/dijkstra.c
    src/core/penalty.c
)
target_include_directories(routelens_ds PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/src/core)
target_compile_options(routelens_ds PRIVATE -Wall -Wextra -pedantic)

# routelens_c_test (top of this file) runs each from the repo root, so
# test_hashtable can find data/osm/nodes.csv, and adds a Valgrind run in CI.
foreach(ds_test dynarray list hashtable queue bfs traffic_reader search dsu minheap stack sort c_dijkstra c_penalty)
    routelens_c_test(test_${ds_test} tests/test_${ds_test}.c)
    target_link_libraries(test_${ds_test} PRIVATE routelens_ds)
endforeach()

# ---------------------------------------------------------------- C++ routing layer over the C core (Gunjita)
# Route value class and the RouteFinder hierarchy (DijkstraRouter, PenaltyRouter)
# wrapping the C routing core.
add_library(routelens_routing STATIC
    src/oop/route.cpp
    src/oop/route_finder.cpp
)
target_link_libraries(routelens_routing PUBLIC routelens_ds)
target_compile_options(routelens_routing PRIVATE -Wall -Wextra -pedantic)

add_executable(test_route tests/test_route.cpp)
target_link_libraries(test_route PRIVATE routelens_routing)
target_compile_options(test_route PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RouteTests COMMAND test_route)

add_executable(test_route_finder tests/test_route_finder.cpp)
target_link_libraries(test_route_finder PRIVATE routelens_routing)
target_compile_options(test_route_finder PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RouteFinderTests COMMAND test_route_finder)

# ---------------------------------------------------------------- traffic snapshot script
# Offline unit tests for preprocess/fetch_traffic.py (no network, no API key).
find_package(Python3 COMPONENTS Interpreter)
if(Python3_Interpreter_FOUND)
    add_test(NAME fetch_traffic_test
             COMMAND ${Python3_EXECUTABLE} preprocess/test_fetch_traffic.py
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# ---------------------------------------------------------------- traffic providers (class hierarchy)
add_executable(test_traffic_provider tests/test_traffic_provider.cpp)
target_link_libraries(test_traffic_provider PRIVATE routelens_traffic)
target_compile_options(test_traffic_provider PRIVATE -Wall -Wextra -pedantic)
add_test(NAME TrafficProviderTests COMMAND test_traffic_provider)

# ---------------------------------------------------------------- lookup experiment: hash table vs binary search
# Timings are only meaningful with optimisation, so this target always uses -O2.
add_executable(routelens_lookup_benchmark src/app/lookup_benchmark.c)
target_link_libraries(routelens_lookup_benchmark PRIVATE routelens_ds)
target_compile_options(routelens_lookup_benchmark PRIVATE -O2 -Wall -Wextra -pedantic)

# ---------------------------------------------------------------- ETA study script
# Offline unit tests for evaluation/eta_study.py (no server, no network).
if(Python3_Interpreter_FOUND)
    add_test(NAME eta_study_test
             COMMAND ${Python3_EXECUTABLE} evaluation/test_eta_study.py
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# Offline unit tests for evaluation/eta_reference.py (no server, no network, no API key).
if(Python3_Interpreter_FOUND)
    add_test(NAME eta_reference_test
             COMMAND ${Python3_EXECUTABLE} evaluation/test_eta_reference.py
             WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
endif()

# The C routing core against the C++ Dijkstra on the real network (skips itself if data/osm is absent).
add_executable(test_c_routing_real tests/test_c_routing_real.cpp)
target_link_libraries(test_c_routing_real PRIVATE routelens_osm)
target_compile_options(test_c_routing_real PRIVATE -Wall -Wextra -pedantic)
add_test(NAME CRoutingRealNetworkTests COMMAND test_c_routing_real
         WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR})
