
#include "../src/spatial/kdtree.hpp"

// Release builds define NDEBUG, which would turn every assert below into a
// no-op and make this test pass without checking anything.
#undef NDEBUG
#include <cassert>
#include <cmath>
#include <iostream>
#include <stdexcept>

using routelens::KDTree;
using routelens::Point;

bool samePoint(const Point& a, const Point& b) {
    return std::abs(a.x - b.x) < 1e-9 &&
           std::abs(a.y - b.y) < 1e-9;
}

int main() {
    // Test 1: Build a tree from sample coordinates.
    KDTree tree({
        {0.0, 0.0},
        {4.0, 4.0},
        {1.0, 2.0},
        {8.0, 1.0},
        {3.0, 7.0}
    });

    assert(!tree.empty());

    // Test 2: Find the nearest point.
    Point result = tree.nearest({2.0, 2.0});

    assert(samePoint(result, {1.0, 2.0}));

    // Test 3: An exact coordinate should return itself.
    result = tree.nearest({8.0, 1.0});

    assert(samePoint(result, {8.0, 1.0}));

    // Test 4: An empty tree should report that it is empty.
    KDTree emptyTree({});

    assert(emptyTree.empty());

    // Test 5: Searching an empty tree should throw an exception.
    bool exceptionThrown = false;

    try {
        emptyTree.nearest({1.0, 1.0});
    } catch (const std::runtime_error&) {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    std::cout << "All KD-tree tests passed!\n";

    return 0;
}
