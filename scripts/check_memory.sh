#!/usr/bin/env bash
# Runs the data-structure tests under Valgrind and fails on any memory error or leak.
#
# usage (from the repository root, after building into ./build):
#   scripts/check_memory.sh [build-dir]
#
# Needs: valgrind (sudo apt install valgrind). The C++ tests that load the real
# road network are slow under Valgrind (about a minute); that is expected.
set -u
BUILD="${1:-build}"
C_TESTS="dynarray list hashtable queue bfs traffic_reader search dsu"
CPP_TESTS="traffic_provider scenarios osm route_provider"

command -v valgrind >/dev/null || { echo "valgrind not found (sudo apt install valgrind)"; exit 2; }
[ -d "$BUILD" ] || { echo "build directory '$BUILD' not found; build the project first"; exit 2; }

failed=0
for t in $C_TESTS $CPP_TESTS; do
    exe="$BUILD/test_$t"
    if [ ! -x "$exe" ]; then echo "SKIP  test_$t (not built)"; continue; fi
    if valgrind --leak-check=full --show-leak-kinds=all --errors-for-leak-kinds=all \
                --error-exitcode=9 -q "$exe" >/tmp/valgrind_$t.log 2>&1; then
        echo "CLEAN test_$t"
    else
        echo "FAIL  test_$t (see /tmp/valgrind_$t.log)"
        failed=1
    fi
done
exit $failed
