// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/camera.hpp"

#include <cmath>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid slope(int size)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (int row = 0; row < size; ++row)
        {
            for (int col = 0; col < size; ++col)
            {
                // Rising one unit a tile towards +X.
                const auto x0 = static_cast<float>(col);
                g.cells[static_cast<std::size_t>(row * size + col)].corners = { x0, x0 + 1, x0, x0 + 1 };
            }
        }
        return g;
    }
} // namespace

TEST_CASE("the eye sits back from and above the target, which lands mid-view")
{
    ParkCamera c;
    c.target = { 10, 2, 10 };
    const glm::vec3 e = eye(c);
    CHECK(e.y > c.target.y);
    CHECK(e.z < c.target.z); // Yaw 0 looks along +Z
    const glm::vec4 clip = view_projection(c, 1.5f) * glm::vec4(c.target, 1);
    CHECK(clip.x / clip.w == doctest::Approx(0).epsilon(0.001));
    CHECK(clip.y / clip.w == doctest::Approx(0).epsilon(0.001));
    CHECK(clip.z / clip.w > 0);
    CHECK(clip.z / clip.w < 1);
}

TEST_CASE("moving follows the way the camera faces, and keeps to the ground")
{
    const save::TerrainGrid g = slope(8);
    ParkCamera c = overview(g);
    CHECK(c.target.x == doctest::Approx(16));
    CHECK(c.target.y == doctest::Approx(4));

    c.yaw = 0;
    const ParkCamera ahead = on_ground(panned(c, 0, 4), g);
    CHECK(ahead.target.z == doctest::Approx(c.target.z + 4));
    const ParkCamera right = on_ground(panned(c, 4, 0), g);
    CHECK(right.target.x == doctest::Approx(c.target.x + 4));
    CHECK(right.target.y == doctest::Approx(c.target.y + 1));

    const ParkCamera off = on_ground(panned(c, 1000, 0), g);
    CHECK(off.target.x < 32);
}

TEST_CASE("tilt and distance stay within their limits")
{
    ParkCamera c;
    CHECK(turned(c, 0, 10).pitch == doctest::Approx(kMaxPitch));
    CHECK(turned(c, 0, -10).pitch == doctest::Approx(kMinPitch));
    CHECK(turned(c, 1, 0).yaw == doctest::Approx(1));
    CHECK(zoomed(c, 100).distance == doctest::Approx(kMaxDistance));
    CHECK(zoomed(c, 0.001f).distance == doctest::Approx(kMinDistance));
}

TEST_CASE("the line of sight through a point of the view passes through what is drawn there")
{
    ParkCamera c;
    c.target = { 10, 2, 10 };
    c.yaw = 0.7f;
    const float aspect = 1.6f;
    const glm::vec3 thing { 14, 3, 8 };
    const glm::vec4 clip = view_projection(c, aspect) * glm::vec4(thing, 1);
    const Ray r = view_ray(c, aspect, { clip.x / clip.w, clip.y / clip.w });
    CHECK(glm::length(r.direction) == doctest::Approx(1));
    const glm::vec3 to = thing - r.origin;
    const float along = glm::dot(to, r.direction);
    CHECK(along > 0);
    CHECK(glm::length(to - r.direction * along) == doctest::Approx(0).epsilon(0.001));
    // Through the middle of the view it passes through the target.
    const Ray middle = view_ray(c, aspect, { 0, 0 });
    const glm::vec3 t = c.target - middle.origin;
    CHECK(glm::length(t - middle.direction * glm::dot(t, middle.direction)) == doctest::Approx(0).epsilon(0.001));
}

TEST_CASE("framing points looks at their middle from far enough to see them all")
{
    const std::vector<glm::vec3> points { { 0, 0, 0 }, { 40, 10, 20 }, { 10, 0, 0 } };
    const ParkCamera c = framed(points, 0.5f, 0.6f);
    CHECK(c.target.x == doctest::Approx(20));
    CHECK(c.target.y == doctest::Approx(5));
    CHECK(c.target.z == doctest::Approx(10));
    CHECK(c.yaw == 0.5f);
    CHECK(c.pitch == 0.6f);
    // Every corner of the box shows in a square view.
    const glm::mat4 m = view_projection(c, 1);
    for (const glm::vec3& p : points)
    {
        const glm::vec4 clip = m * glm::vec4(p, 1);
        CHECK(std::abs(clip.x / clip.w) <= 1);
        CHECK(std::abs(clip.y / clip.w) <= 1);
    }
    // A single point is seen from the nearest the camera goes.
    CHECK(framed(std::vector<glm::vec3> { { 3, 4, 5 } }, 0, 0.6f).distance == kMinDistance);
}
