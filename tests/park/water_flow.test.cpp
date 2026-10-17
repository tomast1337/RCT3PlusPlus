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
    const auto part = flow_strip(straight({ 0, 1, -0.5f }, 9, 2), straight({ 0, 1, 0.5f }, 9, 2), "flow");
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
    CHECK_FALSE(flow_strip(straight({ 0, 0, 0 }, 4, 2), straight({ 0, 0, 1 }, 4, 3), "flow"));
    CHECK_FALSE(flow_strip(straight({ 0, 0, 0 }, 0, 1), straight({ 0, 0, 0 }, 0, 1), "flow"));
}

TEST_CASE("flowing water rolls its picture's rows, back where it started after one repeat")
{
    ovl::RgbaImage image { 1, 4, { } };
    for (std::uint8_t row = 0; row < 4; ++row)
        image.pixels.insert(image.pixels.end(), { row, 0, 0, 255 });
    const float repeat = kFlowRepeat / kFlowSpeed;
    CHECK(flow_shift(4, 0) == 0);
    CHECK(flow_shift(4, repeat / 4 * 1.01f) == 1);
    CHECK(flow_shift(4, repeat * 1.0001f) == 0);
    CHECK(flow_rows(image, 0).pixels == image.pixels);
    const auto moved = flow_rows(image, 1);
    CHECK(moved.pixels[4] == 0); // Row 0 has moved to row 1
    CHECK(moved.pixels[0] == 3);
}
