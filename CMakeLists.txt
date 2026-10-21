
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

add_executable(test_kdtree
    tests/test_kdtree.cpp
    src/spatial/kdtree.cpp
)
target_include_directories(test_kdtree PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/spatial
)
target_compile_options(test_kdtree PRIVATE -Wall -Wextra -pedantic)
add_test(NAME KDTreeTests COMMAND test_kdtree)

add_executable(test_lru_cache
    tests/test_lru_cache.cpp
)
target_include_directories(test_lru_cache PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/traffic
)
target_compile_options(test_lru_cache PRIVATE -Wall -Wextra -pedantic)
add_test(NAME LRUCacheTests COMMAND test_lru_cache)

add_executable(test_rate_limiter
    tests/test_rate_limiter.cpp
)
target_include_directories(test_rate_limiter PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/traffic
)
target_compile_options(test_rate_limiter PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RateLimiterTests COMMAND test_rate_limiter)

add_executable(test_request_params
    tests/test_request_params.cpp
)
target_include_directories(test_request_params PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}/src/server
)
target_compile_options(test_request_params PRIVATE -Wall -Wextra -pedantic)
add_test(NAME RequestParamsTests COMMAND test_request_params)

# Road network: the map-data module's graph and OSM loader, plus the KD-tree
# road snapper that connects clicked points to it.
add_library(routelens_osm STATIC
    src/core/graph.c
    src/data/osm_loader.cpp
    src/spatial/kdtree.cpp
    src/spatial/road_snapper.cpp
    src/algorithms/dijkstra.cpp
)
target_compile_options(routelens_osm PRIVATE -Wall -Wextra)

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
)
target_link_libraries(routelens_server PRIVATE Crow::Crow routelens_osm routelens_traffic)
target_compile_options(routelens_server PRIVATE -Wall -Wextra -pedantic)

# ---------------------------------------------------------------- system libraries
# Install once: sudo apt install -y libcurl4-openssl-dev libcjson-dev
find_package(CURL REQUIRED)
find_path(CJSON_INCLUDE_DIR cjson/cJSON.h)
find_library(CJSON_LIBRARY NAMES cjson)
if(NOT CJSON_INCLUDE_DIR OR NOT CJSON_LIBRARY)
    message(FATAL_ERROR "cJSON not found. Install it with: sudo apt install -y libcjson-dev")
endif()

# ---------------------------------------------------------------- road network tools and tests (Sneha)
# Use the routelens_osm library defined above.
add_executable(routelens_osm_info src/app/osm_info_main.cpp)
target_link_libraries(routelens_osm_info PRIVATE routelens_osm)
target_compile_options(routelens_osm_info PRIVATE -Wall -Wextra -pedantic)

add_executable(test_osm tests/test_osm.cpp)
target_link_libraries(test_osm PRIVATE routelens_osm)
target_compile_options(test_osm PRIVATE -Wall -Wextra -pedantic)
add_test(NAME OsmLoaderTests COMMAND test_osm)
# Dijkstra shortest-path tests
add_executable(test_dijkstra tests/test_dijkstra.cpp)
target_link_libraries(test_dijkstra PRIVATE routelens_osm)
target_compile_options(test_dijkstra PRIVATE -Wall -Wextra -pedantic)
add_test(NAME DijkstraTests COMMAND test_dijkstra)

# ---------------------------------------------------------------- live traffic (Sneha)
add_library(routelens_traffic STATIC
    src/traffic/traffic.cpp
    src/traffic/tomtom_client.cpp
    src/traffic/eta.cpp
    src/data/http_client.cpp
    src/data/geo.cpp
)
target_include_directories(routelens_traffic PUBLIC ${CJSON_INCLUDE_DIR})
target_link_libraries(routelens_traffic PUBLIC routelens_osm CURL::libcurl ${CJSON_LIBRARY})
target_compile_options(routelens_traffic PRIVATE -Wall -Wextra -pedantic)

add_executable(test_traffic tests/test_traffic.cpp)
target_link_libraries(test_traffic PRIVATE routelens_traffic)
target_compile_options(test_traffic PRIVATE -Wall -Wextra -pedantic)
add_test(NAME TrafficTests COMMAND test_traffic)
