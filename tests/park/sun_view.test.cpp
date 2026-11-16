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
    CHECK(view.texel.value == doctest::Approx(100.0f / 2048 * kSunViewSteps / (kSunViewSteps - 1)));

    const glm::vec3 middle = project(view, centre);
    CHECK(std::abs(middle.x) <= 1 / kSunViewSteps);
    CHECK(std::abs(middle.y) <= 1 / kSunViewSteps);
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

TEST_CASE("the sun's view stays put in steps while the camera pans, and holds the square round it in every one")
{
    // Straight down, the map's sides lie along the world's x and z.
    constexpr float kRadius = 64;
    std::size_t moves = 0;
    glm::mat4 last = sun_view({ 0, 1, 0 }, { 0, 0, 0 }, kRadius, 2048).view_projection;
    for (int i = 0; i <= 1000; ++i)
    {
        const glm::vec3 centre(static_cast<float>(i) * 0.1f, 0, static_cast<float>(i) * 0.07f);
        const SunView view = sun_view({ 0, 1, 0 }, centre, kRadius, 2048);
        moves += view.view_projection != last ? 1 : 0;
        last = view.view_projection;
        for (const glm::vec3 corner : { glm::vec3(-kRadius, 0, -kRadius), glm::vec3(kRadius, 0, kRadius), glm::vec3(-kRadius, 0, kRadius), glm::vec3(kRadius, 0, -kRadius) })
        {
            const glm::vec3 p = project(view, centre + corner);
            CHECK(std::abs(p.x) <= 1.0001f); // The square's edge may fall on the map's
            CHECK(std::abs(p.y) <= 1.0001f);
        }
    }
    // 100 units along x, steps of a sixteenth of about 137: some twelve moves, and as many again along z.
    CHECK(moves > 0);
    CHECK(moves <= 30);
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

TEST_CASE("the sun's share of its next shadow step grows with its angle from where shadows were drawn")
{
    const float step = glm::radians(0.5f);
    const glm::vec3 kept = glm::normalize(glm::vec3(0.3f, 0.8f, 0.1f));
    const auto turned = [&](float angle) { return glm::vec3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1)) * glm::vec4(kept, 0)); };
    CHECK(sun_step_share(kept, kept, step) == 0);
    CHECK(sun_step_share(kept, turned(step * 0.25f), step) == doctest::Approx(0.25f).epsilon(0.01));
    CHECK(sun_step_share(kept, turned(step * 0.75f), step) == doctest::Approx(0.75f).epsilon(0.01));
    CHECK(sun_step_share(kept, turned(step * 3), step) == 1);
}
