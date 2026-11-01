#ifndef RL_MIN_HEAP_HPP
#define RL_MIN_HEAP_HPP

#include <stdexcept>
#include <utility>
#include <vector>

namespace rl {

struct HeapNode {
    int vertex;
    double priority;
};

class BinaryMinHeap {
private:
    std::vector<HeapNode> heap;

    void siftUp(int index) {
        while (index > 0) {
            int parent = (index - 1) / 2;

            if (heap[parent].priority <= heap[index].priority)
                break;

            std::swap(heap[parent], heap[index]);
            index = parent;
        }
    }

    void siftDown(int index) {
        int n = static_cast<int>(heap.size());

        while (true) {
            int smallest = index;
            int left = 2 * index + 1;
            int right = 2 * index + 2;

            if (left < n &&
                heap[left].priority < heap[smallest].priority)
                smallest = left;

            if (right < n &&
                heap[right].priority < heap[smallest].priority)
                smallest = right;

            if (smallest == index)
                break;

            std::swap(heap[index], heap[smallest]);
            index = smallest;
        }
    }

public:
    bool empty() const {
        return heap.empty();
    }

    std::size_t size() const {
        return heap.size();
    }

    void push(int vertex, double priority) {
        heap.push_back({vertex, priority});
        siftUp(static_cast<int>(heap.size()) - 1);
    }

    const HeapNode& top() const {
        if (empty())
            throw std::out_of_range("Heap is empty");

        return heap.front();
    }

    HeapNode pop() {
        if (empty())
            throw std::out_of_range("Heap is empty");

        HeapNode result = heap.front();

        if (heap.size() == 1) {
            heap.pop_back();
            return result;
        }

        heap.front() = heap.back();
        heap.pop_back();
        siftDown(0);

        return result;
    }
};

} // namespace rl

#endif
