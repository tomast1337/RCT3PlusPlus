// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/path_network.hpp"

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

TEST_CASE("a point on a path tile finds it")
{
    PathNetwork p;
    p.tile_size = 2;
    const NodeId n = p.add({ 3, 4, 2 });
    CHECK(p.centre(n) == glm::vec3 { 7, 2, 9 });
    CHECK(p.nearest({ 6.5f, 2.2f, 8.1f }) == n);
    CHECK_FALSE(p.nearest({ 0, 0, 0 }));
}
