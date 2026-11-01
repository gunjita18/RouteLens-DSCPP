
#include "../src/algorithms/scenario.hpp"

#include <cassert>
#include <cmath>
#include <iostream>
#include <vector>

namespace {

// A (30.300, 78.000) -- B (30.300, 78.010) -- C (30.300, 78.020), about 960 m
// apart, with edges in both directions. D hangs off A and rejoins at C so that
// C stays reachable when the A-B-C road is closed.
struct Fixture {
    Graph* g = nullptr;
    std::vector<rl::LatLon> pos;
    int ab = -1, ba = -1, bc = -1, cb = -1;
};

Fixture makeFixture() {
    Fixture f;
    f.g = graph_create(3);
    assert(f.g != nullptr);
    f.pos = {{30.300, 78.000}, {30.300, 78.010}, {30.300, 78.020}};

    f.ab = graph_add_edge(f.g, 0, 1, 960, 60, 0, nullptr, 0);
    f.ba = graph_add_edge(f.g, 1, 0, 960, 60, 0, nullptr, 0);
    f.bc = graph_add_edge(f.g, 1, 2, 960, 60, 0, nullptr, 0);
    f.cb = graph_add_edge(f.g, 2, 1, 960, 60, 0, nullptr, 0);
    assert(f.ab >= 0 && f.ba >= 0 && f.bc >= 0 && f.cb >= 0);
    return f;
}

} // namespace

int main() {
    // ---- BFS ----
    {
        Fixture f = makeFixture();

        rl::ReachResult r = rl::bfsReachable(f.g, 0, 2);
        assert(r.reachable);
        assert(r.hops == 2);

        rl::ReachResult same = rl::bfsReachable(f.g, 1, 1);
        assert(same.reachable && same.hops == 0);

        std::vector<bool> blocked(f.g->edge_count, false);
        blocked[f.bc] = true;
        rl::ReachResult cut = rl::bfsReachable(f.g, 0, 2, &blocked);
        assert(!cut.reachable);
        assert(cut.hops == -1);
        assert(cut.visited == 2);  // reached A and B only

        // Directed: C can still reach A through the reverse edges.
        assert(rl::bfsReachable(f.g, 2, 0, &blocked).reachable);

        // Invalid input is reported as unreachable.
        assert(!rl::bfsReachable(f.g, 0, 9).reachable);
        std::vector<bool> wrongSize(1, false);
        assert(!rl::bfsReachable(f.g, 0, 2, &wrongSize).reachable);
        graph_free(f.g);
    }

    // ---- closures: block ----
    {
        Fixture f = makeFixture();
        rl::Closure c;
        c.center = {30.300, 78.005};  // midway between A and B
        c.radiusM = 100.0;
        c.block = true;

        rl::ScenarioEffect e = rl::applyClosures(f.g, f.pos, nullptr, {c});
        assert(e.blocked.size() == 4);
        assert(e.blocked[f.ab] && e.blocked[f.ba]);
        assert(!e.blocked[f.bc] && !e.blocked[f.cb]);
        assert(e.blockedEdges == 2);
        assert(e.slowedEdges == 0);
        assert(e.edgesPerClosure.size() == 1 && e.edgesPerClosure[0] == 2);
        graph_free(f.g);
    }

    // ---- closures: slow, overlap, and block precedence ----
    {
        Fixture f = makeFixture();
        rl::Closure slow;
        slow.center = {30.300, 78.015};  // midway between B and C
        slow.radiusM = 100.0;
        slow.block = false;
        slow.slowFactor = 3.0;

        rl::Closure slowMore = slow;
        slowMore.slowFactor = 2.0;

        rl::ScenarioEffect e =
            rl::applyClosures(f.g, f.pos, nullptr, {slow, slowMore});
        assert(e.slowedEdges == 2);
        assert(std::abs(e.durationFactor[f.bc] - 6.0) < 1e-9);  // 3 x 2
        assert(std::abs(e.durationFactor[f.ab] - 1.0) < 1e-9);

        // A block on the same road wins over a slowdown.
        rl::Closure block = slow;
        block.block = true;
        rl::ScenarioEffect both =
            rl::applyClosures(f.g, f.pos, nullptr, {slow, block});
        assert(both.blocked[f.bc]);
        assert(both.blockedEdges == 2);
        assert(both.slowedEdges == 0);
        graph_free(f.g);
    }

    // ---- closures: a road shape that bends away from the straight line ----
    {
        Fixture f = makeFixture();
        // The A-B road bows north; a closure on that bend must hit it even
        // though the straight A-B segment is far away.
        std::vector<std::vector<rl::LatLon>> geometry(f.g->edge_count);
        geometry[f.ab] = {{30.300, 78.000}, {30.310, 78.005}, {30.300, 78.010}};

        rl::Closure c;
        c.center = {30.310, 78.005};
        c.radiusM = 50.0;
        c.block = true;

        rl::ScenarioEffect withShape =
            rl::applyClosures(f.g, f.pos, &geometry, {c});
        assert(withShape.blocked[f.ab]);

        rl::ScenarioEffect straight =
            rl::applyClosures(f.g, f.pos, nullptr, {c});
        assert(!straight.blocked[f.ab]);
        graph_free(f.g);
    }

    // ---- closures: invalid closures are ignored ----
    {
        Fixture f = makeFixture();
        rl::Closure bad;
        bad.center = {30.300, 78.005};
        bad.radiusM = 100.0;
        bad.block = false;
        bad.slowFactor = 0.5;  // would speed the road up

        rl::ScenarioEffect e = rl::applyClosures(f.g, f.pos, nullptr, {bad});
        assert(e.blockedEdges == 0 && e.slowedEdges == 0);
        assert(e.edgesPerClosure.size() == 1 && e.edgesPerClosure[0] == 0);

        // Bad graph input gives an empty effect.
        assert(rl::applyClosures(nullptr, f.pos, nullptr, {bad})
                   .blocked.empty());
        graph_free(f.g);
    }

    // ---- scenario costs ----
    {
        Fixture f = makeFixture();
        Weights time = {0.0, 1.0, 0.0, 0.0};  // 1 per minute

        rl::Closure slow;
        slow.center = {30.300, 78.005};
        slow.radiusM = 100.0;
        slow.block = false;
        slow.slowFactor = 3.0;

        rl::ScenarioEffect e = rl::applyClosures(f.g, f.pos, nullptr, {slow});
        std::vector<double> costs = rl::scenarioCosts(f.g, &time, e);
        assert(costs.size() == 4);
        assert(std::abs(costs[f.ab] - 3.0) < 1e-9);  // 1 min -> 3 min
        assert(std::abs(costs[f.bc] - 1.0) < 1e-9);

        // Distance-only weights are unaffected by slowdowns.
        Weights distance = {1.0, 0.0, 0.0, 0.0};
        std::vector<double> d = rl::scenarioCosts(f.g, &distance, e);
        assert(std::abs(d[f.ab] - 0.96) < 1e-9);

        Weights zero = {0, 0, 0, 0};
        assert(rl::scenarioCosts(f.g, &zero, e).empty());
        graph_free(f.g);
    }

    std::cout << "All scenario tests passed.\n";
    return 0;
}
