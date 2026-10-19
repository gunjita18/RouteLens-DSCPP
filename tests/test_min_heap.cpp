#include "../src/algorithms/min_heap.hpp"

#include <cassert>
#include <iostream>
#include <stdexcept>

int main() {
    rl::BinaryMinHeap heap;

    // Test empty heap
    assert(heap.empty());
    assert(heap.size() == 0);

    // Test insertion
    heap.push(1, 30.0);
    heap.push(2, 10.0);
    heap.push(3, 20.0);

    assert(heap.size() == 3);
    assert(heap.top().vertex == 2);
    assert(heap.top().priority == 10.0);

    // Test removal in ascending priority order
    auto first = heap.pop();
    assert(first.vertex == 2);
    assert(first.priority == 10.0);

    auto second = heap.pop();
    assert(second.vertex == 3);
    assert(second.priority == 20.0);

    auto third = heap.pop();
    assert(third.vertex == 1);
    assert(third.priority == 30.0);

    assert(heap.empty());

    // Test popping from an empty heap
    bool exceptionThrown = false;

    try {
        heap.pop();
    } catch (const std::out_of_range&) {
        exceptionThrown = true;
    }

    assert(exceptionThrown);

    std::cout << "All Binary Min-Heap tests passed!" << std::endl;

    return 0;
}
