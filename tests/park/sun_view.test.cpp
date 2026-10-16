// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/sun_view.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    glm::vec3 project(const SunView& view, glm::vec3 p)
    {
        const glm::vec4 c = view.view_projection * glm::vec4(p, 1.0f);
        return glm::vec3(c) / c.w;
    }
} // namespace

TEST_CASE("the sun's view holds the square round its centre, nearer the sun shallower")
{
    const glm::vec3 to_sun = glm::normalize(glm::vec3(0.3f, 1.0f, -0.4f));
    const glm::vec3 centre(100, 20, 50);
    const SunView view = sun_view(to_sun, centre, 50, 2048);
    CHECK(view.texel == doctest::Approx(100.0f / 2048));

    const glm::vec3 middle = project(view, centre);
    CHECK(std::abs(middle.x) < 0.01f);
    CHECK(std::abs(middle.y) < 0.01f);
    CHECK(middle.z == doctest::Approx(0.5f).epsilon(0.01));
    // Further along the sunlight lies deeper; a point towards the sun shallower.
    CHECK(project(view, centre - to_sun * 10.0f).z > middle.z);
    CHECK(project(view, centre + to_sun * 10.0f).z < middle.z);
    // The same spot seen along the sunlight lands on the same texel.
    const glm::vec3 a = project(view, centre + glm::vec3(5, 0, 5));
    const glm::vec3 b = project(view, centre + glm::vec3(5, 0, 5) + to_sun * 30.0f);
    CHECK(a.x == doctest::Approx(b.x).epsilon(0.001));
    CHECK(a.y == doctest::Approx(b.y).epsilon(0.001));
}

TEST_CASE("the sun's view moves only in whole texels")
{
    const glm::vec3 to_sun(0.2f, 1.0f, 0.1f);
    const SunView a = sun_view(to_sun, { 10, 0, 10 }, 64, 2048);
    const SunView b = sun_view(to_sun, { 10.001f, 0, 10.001f }, 64, 2048);
    CHECK(a.view_projection == b.view_projection);
}

TEST_CASE("a straight-down sun still gives a view")
{
    const SunView view = sun_view({ 0, 1, 0 }, { 0, 0, 0 }, 10, 512);
    const glm::vec3 p = project(view, { 0, 0, 0 });
    CHECK(std::isfinite(p.x));
    CHECK(project(view, { 0, -5, 0 }).z > p.z);
}

TEST_CASE("shadows reach further round a camera further out")
{
    CHECK(shadow_radius(10) == doctest::Approx(40));
    CHECK(shadow_radius(100) > shadow_radius(60));
    CHECK(shadow_radius(10000) == doctest::Approx(300));
    // In steps: a small zoom keeps the size.
    CHECK(shadow_radius(61) == shadow_radius(62));
    CHECK(shadow_radius(61) >= 61 * 1.5f);
}

TEST_CASE("the shadows' sun moves only once it has moved enough")
{
    const glm::vec3 kept = glm::normalize(glm::vec3(0.3f, 1, 0));
    const glm::vec3 near = glm::normalize(glm::vec3(0.301f, 1, 0));
    const glm::vec3 far = glm::normalize(glm::vec3(0.4f, 1, 0));
    CHECK(steady_sun(kept, near, 0.01f) == kept);
    CHECK(steady_sun(kept, far, 0.01f) == far);
}
