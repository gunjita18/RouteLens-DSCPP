
#ifndef ROUTELENS_KDTREE_HPP
#define ROUTELENS_KDTREE_HPP

#include <cstddef>
#include <vector>

namespace routelens {

struct Point {
    double x;
    double y;
    // Caller's payload carried through the search, e.g. which road this point
    // lies on. Ignored by the tree itself; defaults to 0 for plain points.
    std::size_t id = 0;
};

class KDTree {
public:
    explicit KDTree(std::vector<Point> points);

    ~KDTree();

    KDTree(const KDTree&) = delete;
    KDTree& operator=(const KDTree&) = delete;

    bool empty() const;

    Point nearest(const Point& target) const;

private:
    struct Node {
        Point point;
        Node* left;
        Node* right;

        explicit Node(const Point& p)
            : point(p), left(nullptr), right(nullptr) {}
    };

    Node* root_;

    Node* build(std::vector<Point>& points,
                std::size_t begin,
                std::size_t end,
                int depth);

    void destroy(Node* node);

    void nearestSearch(const Node* node,
                       const Point& target,
                       int depth,
                       Point& best,
                       double& bestDistance) const;
};

}  // namespace routelens

#endif
