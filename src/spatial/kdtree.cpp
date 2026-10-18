
#include "kdtree.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace routelens {

KDTree::KDTree(std::vector<Point> points) : root_(nullptr) {
    if (!points.empty()) {
        root_ = build(points, 0, points.size(), 0);
    }
}

KDTree::~KDTree() {
    destroy(root_);
}

void KDTree::destroy(Node* node) {
    if (node == nullptr) {
        return;
    }

    destroy(node->left);
    destroy(node->right);
    delete node;
}

bool KDTree::empty() const {
    return root_ == nullptr;
}

KDTree::Node* KDTree::build(std::vector<Point>& points,
                            std::size_t begin,
                            std::size_t end,
                            int depth) {
    if (begin >= end) {
        return nullptr;
    }

    const int axis = depth % 2;

    const std::size_t middle = begin + (end - begin) / 2;

    std::nth_element(
        points.begin() + begin,
        points.begin() + middle,
        points.begin() + end,
        [axis](const Point& a, const Point& b) {
            return axis == 0 ? a.x < b.x : a.y < b.y;
        }
    );

    Node* node = new Node(points[middle]);

    try {
        node->left = build(points, begin, middle, depth + 1);
        node->right = build(points, middle + 1, end, depth + 1);
    } catch (...) {
        destroy(node);
        throw;
    }

    return node;
}

double squaredDistance(const Point& a, const Point& b) {
    const double dx = a.x - b.x;
    const double dy = a.y - b.y;

    return dx * dx + dy * dy;
}

Point KDTree::nearest(const Point& target) const {
    if (empty()) {
        throw std::runtime_error("Cannot search an empty KD-tree");
    }

    Point best = root_->point;
    double bestDistance = squaredDistance(best, target);

    nearestSearch(root_, target, 0, best, bestDistance);

    return best;
}

void KDTree::nearestSearch(const Node* node,
                           const Point& target,
                           int depth,
                           Point& best,
                           double& bestDistance) const {
    if (node == nullptr) {
        return;
    }

    const double distance = squaredDistance(node->point, target);

    if (distance < bestDistance) {
        best = node->point;
        bestDistance = distance;
    }

    const int axis = depth % 2;

    const double difference =
        axis == 0
            ? target.x - node->point.x
            : target.y - node->point.y;

    const Node* first = difference < 0 ? node->left : node->right;
    const Node* second = difference < 0 ? node->right : node->left;

    nearestSearch(first, target, depth + 1, best, bestDistance);

    if (difference * difference < bestDistance) {
        nearestSearch(second, target, depth + 1, best, bestDistance);
    }
}

}  // namespace routelens
