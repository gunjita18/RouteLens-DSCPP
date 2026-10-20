
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

add_executable(routelens_server
    src/server/main.cpp
)
target_link_libraries(routelens_server PRIVATE Crow::Crow)
target_compile_options(routelens_server PRIVATE -Wall -Wextra -pedantic)
