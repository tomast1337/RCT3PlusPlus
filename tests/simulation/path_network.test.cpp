// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/path_network.hpp"

#include <algorithm>
#include <array>
#include <doctest/doctest.h>

using namespace rct3::simulation;

TEST_CASE("path tiles side by side join, unless their levels are more than a step apart")
{
    PathNetwork p;
    const NodeId a = p.add({ 0, 0, 0 });
    const NodeId b = p.add({ 1, 0, 1 }); // A slope up
    const NodeId c = p.add({ 2, 0, 3 }); // Too high to join
    const NodeId d = p.add({ 0, 1, 0 });
    CHECK(p.nodes()[a].next[0] == b);
    CHECK(p.nodes()[b].next[2] == a);
    CHECK(p.nodes()[b].next[0] == kNoNode);
    CHECK(p.nodes()[c].next[2] == kNoNode);
    CHECK(p.nodes()[a].next[1] == d);
    CHECK(p.add({ 0, 0, 0 }) == a);
    CHECK(p.nodes().size() == 4);
}

TEST_CASE("a route is a shortest walk, and enters a queue only at its end")
{
    PathNetwork p;
    // A loop: along row 0 to column 3, and back along row 2, joined by columns 0 and 3.
    for (int c = 0; c <= 3; ++c)
    {
        p.add({ c, 0, 0 });
        p.add({ c, 2, 0 });
    }
    p.add({ 0, 1, 0 });
    p.add({ 3, 1, 0 });
    const auto from = *p.find({ 0, 0, 0 });
    const auto walk = p.route(from, *p.find({ 3, 1, 0 }));
    REQUIRE(walk);
    CHECK(walk->size() == 4);
    CHECK(walk->back() == *p.find({ 3, 1, 0 }));
    CHECK(p.route(from, from)->empty());

    const NodeId queue = p.add({ 4, 0, 0 }, true);
    const NodeId beyond = p.add({ 5, 0, 0 });
    CHECK(p.route(from, queue));
    CHECK_FALSE(p.route(from, beyond));
}

TEST_CASE("one search finds the nearest of several places, the first where several share a tile")
{
    PathNetwork p;
    for (int c = 0; c <= 6; ++c)
        p.add({ c, 0, 0 });
    const NodeId queue = p.add({ 7, 0, 0 }, true);
    const auto at = [&](int c) { return *p.find({ c, 0, 0 }); };
    const std::array far_first { at(6), at(4), at(2), at(2) };
    const auto found = p.route_nearest(at(0), far_first);
    REQUIRE(found);
    CHECK(found->first == 2);
    CHECK(found->second.size() == 2);
    const std::array none { NodeId { 99 } };
    CHECK_FALSE(p.route_nearest(at(0), none));
    const std::array in_queue { queue };
    CHECK(p.route_nearest(at(0), in_queue)->second.back() == queue);
    CHECK(p.route_nearest(at(4), far_first)->first == 1); // Standing on one
}

TEST_CASE("a point on a path tile finds it")
{
    PathNetwork p;
    p.tile_size = 2;
    const NodeId n = p.add({ 3, 4, 2 });
    CHECK(p.centre(n) == glm::vec3 { 7, 2, 9 });
    CHECK(p.nearest({ 6.5f, 2.2f, 8.1f }) == n);
    CHECK_FALSE(p.nearest({ 0, 0, 0 }));
}

namespace
{
    std::size_t corners(const PathNetwork& p)
    {
        std::size_t n = 0;
        for (const PathNode& node : p.nodes())
            n += static_cast<std::size_t>(std::ranges::count_if(node.corner, [](NodeId c) { return c != kNoNode; }));
        return n;
    }
} // namespace

TEST_CASE("a walk cuts across a plaza on the diagonal")
{
    PathNetwork p;
    for (int col = 0; col < 4; ++col)
        for (int row = 0; row < 4; ++row)
            p.add({ col, row, 0 });
    const NodeId from = *p.find({ 0, 0, 0 });
    const NodeId to = *p.find({ 3, 3, 0 });
    const auto walk = p.route(from, to);
    REQUIRE(walk);
    CHECK(walk->size() == 3); // Three steps across, not six round
    CHECK(walk->front() == *p.find({ 1, 1, 0 }));
    CHECK(p.nodes()[from].corner[0] == *p.find({ 1, 1, 0 }));
    // Straight along an edge stays straight: no zigzag of diagonals.
    const auto edge = p.route(from, *p.find({ 3, 0, 0 }));
    REQUIRE(edge);
    CHECK(edge->size() == 3);
}

TEST_CASE("no diagonal round a corner, over a gap, across a step in height or through a queue")
{
    {
        PathNetwork corner; // An L: the inside tile is grass
        corner.add({ 0, 0, 0 });
        corner.add({ 1, 0, 0 });
        corner.add({ 1, 1, 0 });
        CHECK(corners(corner) == 0);
        CHECK(corner.route(0, 2)->size() == 2);
    }
    {
        PathNetwork slope;
        slope.add({ 0, 0, 0 });
        slope.add({ 1, 0, 0 });
        slope.add({ 0, 1, 0 });
        slope.add({ 1, 1, 1 }); // A step up
        CHECK(corners(slope) == 0);
    }
    {
        PathNetwork queue;
        queue.add({ 0, 0, 0 });
        queue.add({ 1, 0, 0 });
        queue.add({ 0, 1, 0 });
        queue.add({ 1, 1, 0 }, true);
        CHECK(corners(queue) == 0);
    }
    PathNetwork square; // The fourth tile added last closes it, both ways across
    square.add({ 0, 0, 0 });
    square.add({ 1, 0, 0 });
    square.add({ 0, 1, 0 });
    CHECK(corners(square) == 0);
    square.add({ 1, 1, 0 });
    CHECK(corners(square) == 4);
}

TEST_CASE("a tile taken away is joined to nothing and found by nothing; one laid there again is new")
{
    PathNetwork p;
    for (int col = 0; col < 2; ++col)
        for (int row = 0; row < 2; ++row)
            p.add({ col, row, 0 });
    p.add({ 2, 0, 0 });
    const NodeId gone = *p.find({ 1, 0, 0 });
    CHECK(corners(p) > 0);
    p.remove(gone);
    CHECK(corners(p) == 0); // The square is broken
    CHECK(p.nodes()[gone].removed);
    CHECK_FALSE(p.find({ 1, 0, 0 }));
    CHECK(std::ranges::all_of(p.nodes(), [&](const PathNode& n) { return !std::ranges::contains(n.next, gone); }));
    // (2, 0) is cut off now; (0, 0) still reaches (1, 1) round the other way.
    CHECK_FALSE(p.route(*p.find({ 0, 0, 0 }), *p.find({ 2, 0, 0 })));
    CHECK(p.route(*p.find({ 0, 0, 0 }), *p.find({ 1, 1, 0 }))->size() == 2);
    p.remove(gone); // Twice is harmless

    const NodeId again = p.add({ 1, 0, 0 });
    CHECK(again != gone);
    CHECK(p.route(*p.find({ 0, 0, 0 }), *p.find({ 2, 0, 0 }))->size() == 2);
}

TEST_CASE("a walker stands on the path's surface under their feet, wherever across the tile they walk")
{
    PathNetwork p; // Tiles 4 across, from the origin
    const NodeId top = p.add({ 0, 0, 0 }, false, 0.0f);
    const NodeId ramp = p.add({ 1, 0, -1 }, false, -1.0f);
    const NodeId floor = p.add({ 2, 0, -2 }, false, -2.0f);
    p.set_corners(ramp, { 0.0f, -2.0f, 0.0f, -2.0f }); // Falls two units towards higher columns
    // Down the ramp: a quarter of the way in, its middle, three quarters, whatever the side of the path.
    CHECK(p.surface(top, ramp, { 5.0f, 2.0f }) == doctest::Approx(-0.5f));
    CHECK(p.surface(top, ramp, { 6.0f, 0.5f }) == doctest::Approx(-1.0f));
    CHECK(p.surface(ramp, floor, { 7.0f, 3.5f }) == doctest::Approx(-1.5f));
    // Still on the tile above, or already on the floor below, it is flat.
    CHECK(p.surface(top, ramp, { 3.9f, 2.0f }) == doctest::Approx(0.0f));
    CHECK(p.surface(ramp, floor, { 8.5f, 2.0f }) == doctest::Approx(-2.0f));

    // Ground that rises across the path: one walking its high side stands higher than one on its low side.
    const NodeId bank = p.add({ 0, 1, 0 }, false, 0.5f);
    p.set_corners(bank, { 0.0f, 0.0f, 1.0f, 1.0f });
    CHECK(p.surface(bank, bank, { 2.0f, 5.0f }) == doctest::Approx(0.25f));
    CHECK(p.surface(bank, bank, { 2.0f, 7.0f }) == doctest::Approx(0.75f));
}

TEST_CASE("a wall between two tiles parts them, a queue keeps its own sides, and taking it away joins them again")
{
    PathNetwork p;
    // A square of four tiles, and a queue beside it.
    const NodeId a = p.add({ 0, 0, 0 });
    const NodeId b = p.add({ 1, 0, 0 });
    const NodeId c = p.add({ 0, 1, 0 });
    const NodeId d = p.add({ 1, 1, 0 });
    const NodeId q = p.add({ 2, 0, 0 }, true);
    REQUIRE(p.nodes()[a].corner[0] == d);
    p.add_wall(0, 0, 0, -0.5f, 2); // Between a and b
    p.add_wall(1, 0, 0, -0.5f, 2); // Between b and the queue
    CHECK(p.nodes()[a].next[0] == kNoNode);
    CHECK(p.nodes()[b].next[2] == kNoNode);
    CHECK(p.nodes()[a].corner[0] == kNoNode); // The square is broken, so no crossing
    CHECK(p.nodes()[b].next[0] == q);
    const auto around = p.route(a, b);
    REQUIRE(around);
    CHECK(*around == std::vector<NodeId> { c, d, b });
    // A wall low on the ground does not part a deck far above it.
    p.add_wall(0, 0, 1, -0.5f, 2);
    CHECK(p.nodes()[a].next[1] == kNoNode);
    p.remove_wall(0, 0, 1, -0.5f, 2);
    p.remove_wall(0, 0, 0, -0.5f, 2);
    CHECK(p.nodes()[a].next[0] == b);
    CHECK(p.nodes()[a].next[1] == c);
    CHECK(p.nodes()[a].corner[0] == d);
    PathNetwork high;
    const NodeId low = high.add({ 0, 0, 6 });
    high.add_wall(0, 0, 0, -0.5f, 2);
    CHECK(high.nodes()[low].next[0] == high.add({ 1, 0, 6 }));
}

TEST_CASE("a no-entry side keeps guests from coming in across it, but not from leaving or staff from passing")
{
    PathNetwork p;
    const NodeId a = p.add({ 0, 0, 0 });
    const NodeId b = p.add({ 1, 0, 0 });
    const NodeId c = p.add({ 2, 0, 0 });
    p.set_no_entry(b, 2, true); // The side facing a
    CHECK_FALSE(p.may_step(a, b, Walker::Guest));
    CHECK(p.may_step(b, a, Walker::Guest));
    CHECK(p.may_step(c, b, Walker::Guest));
    CHECK_FALSE(p.route(a, c));
    CHECK(p.route(c, a));
    CHECK(p.route(a, c, Walker::Staff));
    p.set_no_entry(b, 2, false);
    CHECK(p.route(a, c));
}

TEST_CASE("a no-entry side also bars the cut across a square into its tile")
{
    PathNetwork p;
    for (int c = 0; c <= 1; ++c)
        for (int r = 0; r <= 1; ++r)
            p.add({ c, r, 0 });
    const NodeId from = *p.find({ 0, 0, 0 });
    const NodeId to = *p.find({ 1, 1, 0 });
    REQUIRE(std::ranges::contains(p.nodes()[from].corner, to));
    CHECK(p.may_step(from, to, Walker::Guest));
    p.set_no_entry(to, 3, true); // Its -row side
    CHECK_FALSE(p.may_step(from, to, Walker::Guest));
    const auto walk = p.route(from, to);
    REQUIRE(walk);
    CHECK(walk->size() == 2); // Round by (0, 1), coming in across the -col side
}

TEST_CASE("staff never step onto a tile kept from them, unless they stand on one")
{
    PathNetwork p;
    const NodeId inside = p.add({ 0, 0, 0 });
    const NodeId gate = p.add({ 1, 0, 0 });
    const NodeId outside = p.add({ 2, 0, 0 });
    p.keep_staff_off(gate, true);
    p.keep_staff_off(outside, true);
    CHECK_FALSE(p.route(inside, outside, Walker::Staff));
    CHECK(p.route(inside, outside, Walker::Guest));
    CHECK(p.route(outside, inside, Walker::Staff)); // Found outside, they can come back in
    CHECK_FALSE(p.may_step(inside, gate, Walker::Staff));
}

TEST_CASE("given a purse, a route takes a link through a queue when it is shorter and its toll fits")
{
    PathNetwork p;
    // A long path along row 0, with a queue tile off its start whose link ends near the far end.
    for (int c = 0; c <= 20; ++c)
        p.add({ c, 0, 0 });
    const NodeId start = *p.find({ 0, 0, 0 });
    const NodeId far = *p.find({ 20, 0, 0 });
    const NodeId queue = p.add({ 0, 1, 0 }, true);
    p.link({ .from = queue, .to = *p.find({ 19, 0, 0 }), .length = 5, .toll = 100 });
    CHECK(p.link_between(queue, *p.find({ 19, 0, 0 })));
    CHECK_FALSE(p.link_between(queue, far));
    // Without a purse, or one too light, the walk is along the path.
    CHECK(p.route(start, far)->size() == 20);
    CHECK(p.route(start, far, Walker::Guest, 99)->size() == 20);
    const auto ride = p.route(start, far, Walker::Guest, 100);
    REQUIRE(ride);
    CHECK(*ride == std::vector<NodeId> { queue, *p.find({ 19, 0, 0 }), far });
    // A link longer than the walk is left alone, and the queue is no way through otherwise.
    p.clear_links();
    p.link({ .from = queue, .to = *p.find({ 19, 0, 0 }), .length = 30, .toll = 0 });
    CHECK(p.route(start, far, Walker::Guest, 0)->size() == 20);
    CHECK_FALSE(p.route(start, p.add({ 0, 2, 0 }), Walker::Guest, 0));
}
