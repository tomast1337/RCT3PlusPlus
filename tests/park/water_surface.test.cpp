// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/water_surface.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A square of water at `level` from (x, z) to (x + size, z + size), `depth` deep.
    void square(std::vector<WaterVertex>& out, float level, float x, float z, float size, float depth = 1)
    {
        const glm::vec4 colour { 0.2f, 0.4f, 0.5f, 0.6f };
        const glm::vec3 a { x, level, z }, b { x + size, level, z }, c { x, level, z + size }, d { x + size, level, z + size };
        for (const glm::vec3& p : { a, b, c, d, c, b })
            out.push_back({ p, colour, depth });
    }

    glm::mat4 looking_down(glm::vec3 eye)
    {
        return glm::perspective(0.9f, 1.0f, kNear, kFar) * glm::lookAt(eye, eye + glm::vec3(0.0f, -1.0f, 0.001f), glm::vec3(0.0f, 1.0f, 0.0f));
    }
} // namespace

TEST_CASE("water is grouped by height, lowest first, keeping each corner's depth")
{
    std::vector<WaterVertex> water;
    square(water, 5, 0, 0, 4, 2);
    square(water, -1, 10, 0, 4);
    square(water, 5, 20, 0, 4);
    const WaterSurface s = water_surface(water);
    REQUIRE(s.batches.size() == 2);
    CHECK(s.batches[0].level == doctest::Approx(-1));
    CHECK(s.batches[0].count == 6);
    CHECK(s.batches[1].level == doctest::Approx(5));
    CHECK(s.batches[1].first == 6);
    CHECK(s.batches[1].count == 12);
    CHECK(s.batches[1].low.x == doctest::Approx(0));
    CHECK(s.batches[1].high.x == doctest::Approx(24));
    CHECK(s.vertices[6].uv.x == doctest::Approx(2));
}

TEST_CASE("the reflected heights are those covering most of the view, below the eye")
{
    std::vector<WaterVertex> water;
    square(water, 0, -50, -50, 100);  // Fills the view
    square(water, 3, -2, -2, 4);      // A small pool
    square(water, 50, -50, -50, 100); // Above the eye
    const WaterSurface s = water_surface(water);
    const glm::vec3 eye { 0, 20, 0 };
    const std::vector<float> one = reflection_levels(s, looking_down(eye), eye, 1);
    REQUIRE(one.size() == 1);
    CHECK(one[0] == doctest::Approx(0));
    const std::vector<float> two = reflection_levels(s, looking_down(eye), eye, 2);
    CHECK(two.size() == 2);
    CHECK(reflection_levels(s, looking_down(eye), eye, 0).empty());
}

TEST_CASE("water close in height to a reflected one shares it")
{
    std::vector<WaterVertex> water;
    square(water, 0, -50, -50, 100);
    square(water, 0.25f, -2, -2, 4);
    const WaterSurface s = water_surface(water);
    const glm::vec3 eye { 0, 20, 0 };
    const std::vector<float> levels = reflection_levels(s, looking_down(eye), eye, 2);
    REQUIRE(levels.size() == 1); // The pool borrows the lake's
    CHECK(reflection_for(0.25f, levels) == 0);
    CHECK_FALSE(reflection_for(3, levels));
    const std::vector<float> both { 0, 1 };
    CHECK(reflection_for(0.7f, both) == 1);
}

TEST_CASE("the mirror swaps heights about the level")
{
    const glm::vec4 p = mirror(2) * glm::vec4(1, 5, -3, 1);
    CHECK(p.x == doctest::Approx(1));
    CHECK(p.y == doctest::Approx(-1));
    CHECK(p.z == doctest::Approx(-3));
}

TEST_CASE("an oblique near plane puts depth 0 on the plane and keeps the far corner at depth 1")
{
    const glm::mat4 projection = glm::perspective(0.9f, 1.5f, kNear, kFar);
    const glm::mat4 view = glm::lookAt(glm::vec3(0, 10, -30), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
    const glm::mat4 m = projection * view;
    // Keep what is above y = 1 seen through the camera mirrored: the eye mirrored is below the plane.
    const glm::mat4 mirrored = m * mirror(1);
    const glm::vec4 plane { 0, 1, 0, -1 };
    const glm::mat4 clipped = oblique_clip(mirrored, plane);
    for (const glm::vec3 on : { glm::vec3(0, 1, 0), glm::vec3(5, 1, 10), glm::vec3(-8, 1, 40) })
    {
        const glm::vec4 c = clipped * glm::vec4(on, 1);
        CHECK(c.z / c.w == doctest::Approx(0).epsilon(1e-3));
    }
    const glm::vec4 above = clipped * glm::vec4(0, 4, 10, 1);
    const glm::vec4 below = clipped * glm::vec4(0, -2, 10, 1);
    CHECK(above.z / above.w > 0);
    CHECK(below.z / below.w < 0);
    // x, y and w are untouched, so the picture is the same.
    const glm::vec4 p { 3, 7, 20, 1 };
    CHECK((clipped * p).x == doctest::Approx((mirrored * p).x));
    CHECK((clipped * p).w == doctest::Approx((mirrored * p).w));
    const glm::vec4 r = reflection_transform(m, 1) * glm::vec4(0, 1 - kReflectionClipBias, 0, 1);
    CHECK(r.z / r.w == doctest::Approx(0).epsilon(1e-3));
}

TEST_CASE("a line of sight meets the nearest water it crosses, not beside it")
{
    std::vector<WaterVertex> water;
    square(water, 0, 0, 0, 4);
    square(water, 2, 0, 0, 4);
    const WaterSurface s = water_surface(water);
    const auto hit = water_hit({ { 1, 10, 1 }, { 0, -1, 0 } }, s);
    REQUIRE(hit);
    CHECK(*hit == doctest::Approx(8)); // The upper one
    CHECK_FALSE(water_hit({ { 9, 10, 1 }, { 0, -1, 0 } }, s));
    CHECK_FALSE(water_hit({ { 1, 10, 1 }, { 0, 1, 0 } }, s));
    CHECK_FALSE(water_hit({ { 1, 10, 1 }, { 1, 0, 0 } }, s));
}
