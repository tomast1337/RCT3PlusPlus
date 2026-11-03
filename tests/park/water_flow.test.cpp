// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_flow.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // A straight spline of `parts` parts from `from` along -X, `length` long.
    ovl::Spline straight(std::array<float, 3> from, float length, int parts)
    {
        ovl::Spline s;
        for (int i = 0; i <= parts; ++i)
        {
            ovl::SplineNode& n = s.nodes.emplace_back();
            n.position = { from[0] - length * i / parts, from[1], from[2] };
        }
        return s;
    }

} // namespace

TEST_CASE("a piece's water spans its two edges and its picture repeats a whole number of times along it")
{
    const auto part = flow_strip(straight({ 0, 1, -0.5f }, 9, 2), straight({ 0, 1, 0.5f }, 9, 2), straight({ 0, 0, 0 }, 9, 1), "flow");
    REQUIRE(part.has_value());
    CHECK(part->texture == "flow");
    CHECK(part->blend == PartBlend::Blended);
    REQUIRE(part->vertices.size() >= 4);
    CHECK(part->indices.size() == (part->vertices.size() - 2) * 3);
    CHECK(part->vertices.front().position.z == doctest::Approx(-0.5f));
    CHECK(part->vertices[1].position.z == doctest::Approx(0.5f));
    CHECK(part->vertices.front().uv.y == 0);
    // 9 units at about 4 a repeat: 2 whole repeats.
    CHECK(part->vertices.back().uv.y == doctest::Approx(2));
    CHECK(part->vertices.back().uv.x == 1);
    for (const ModelVertex& v : part->vertices)
        CHECK(v.normal.y == doctest::Approx(1));
}

TEST_CASE("water whose edges do not pair up is left out")
{
    CHECK_FALSE(flow_strip(straight({ 0, 0, 0 }, 4, 2), straight({ 0, 0, 1 }, 4, 3), { }, "flow"));
    CHECK_FALSE(flow_strip(straight({ 0, 0, 0 }, 0, 1), straight({ 0, 0, 0 }, 0, 1), { }, "flow"));
}

TEST_CASE("water listed from the track's far end still runs its picture the way the track runs")
{
    // The water's edges run from x = -9 back to 0, the rails from 0 to -9.
    const auto part = flow_strip(straight({ -9, 1, -0.5f }, -9, 2), straight({ -9, 1, 0.5f }, -9, 2), straight({ 0, 0, 0 }, 9, 1), "flow");
    REQUIRE(part.has_value());
    CHECK(part->vertices.front().position.x == doctest::Approx(-9));
    CHECK(part->vertices.front().uv.y == doctest::Approx(2));
    CHECK(part->vertices.back().uv.y == doctest::Approx(0));
    CHECK(part->chain);
}

TEST_CASE("flowing water slides its picture on along v, and back along it on pieces laid the other way round")
{
    const float quarter = kFlowRepeat / kFlowSpeed / 4;
    CHECK(flow_scroll(0, 1) == 0);
    // Adding 0.75 to v reads the picture a quarter further back: it has moved on a quarter.
    CHECK(flow_scroll(quarter, 1) == doctest::Approx(0.75f));
    CHECK(flow_scroll(quarter, 0) == doctest::Approx(0.75f));
    CHECK(flow_scroll(quarter, -3) == doctest::Approx(0.25f));
}
