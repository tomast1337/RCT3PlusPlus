// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/swinging_part.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A suspended car's seat swinging side to side about the car's forward line, from a pivot 1.5 m up.
    SwingingPart sideways()
    {
        return { .line = { 0, 0, 1 }, .arm = { 1.5f }, .damping = { 1 }, .pull = { 1 } };
    }

    /// The car heading +Z, level.
    const glm::mat3 kLevel { 1.0f };

    /// Where the part hangs, from its pivot, swung by `angle` about +Z from straight down.
    glm::vec3 hang(float angle)
    {
        return glm::mat3(glm::rotate(glm::mat4(1.0f), angle, glm::vec3(0, 0, 1))) * glm::vec3(0, -1, 0);
    }

    void run(Swing& swing, const SwingingPart& part, glm::vec3 acceleration, float seconds)
    {
        for (float t = 0; t < seconds; t += 0.05f)
            swing_on(swing, part, kLevel, acceleration, 0.05f);
    }
} // namespace

TEST_CASE("a swinging part at rest on a standing car hangs straight down")
{
    Swing swing;
    run(swing, sideways(), glm::vec3(0.0f), 10);
    CHECK(swing.angle.value == doctest::Approx(0));
    CHECK(swing.rate.value == doctest::Approx(0));
}

TEST_CASE("in a steady turn a swinging part swings out of it, as far as the turn's pull against gravity")
{
    // Turning left at 5 m/s² towards the turn's middle, at +X: it settles hanging along gravity less that pull.
    for (const float pull : { 1.0f, 0.5f })
    {
        CAPTURE(pull);
        SwingingPart part = sideways();
        part.pull = { pull };
        Swing swing;
        run(swing, part, { 5, 0, 0 }, 20);
        const float expected = std::atan(pull * 5 / units::kGravity.value);
        CHECK(std::abs(swing.angle.value) == doctest::Approx(expected).epsilon(1e-3));
        CHECK(hang(swing.angle.value).x < 0); // Out, away from the turn's middle
        CHECK(std::abs(swing.rate.value) < 1e-3f);
    }
}

TEST_CASE("a swinging part's damping makes it settle, and faster the more it has")
{
    const auto left_after = [](float damping) {
        SwingingPart part = sideways();
        part.damping = { damping };
        Swing swing { .angle = { 0.6f } };
        run(swing, part, glm::vec3(0.0f), 3);
        // How far it would swing out from here: its angle, and its turning as an angle at its swing's pace.
        const float pace = std::sqrt(part.arm.value * units::kGravity.value);
        return std::hypot(swing.angle.value, swing.rate.value / pace);
    };
    CHECK(left_after(2) < 0.05f);
    CHECK(left_after(2) < left_after(0.2f));
    CHECK(left_after(0.2f) < 0.6f);
}

TEST_CASE("a swinging part's centring draws it back to rest even against a turn's pull")
{
    SwingingPart part = sideways();
    part.centring = { 4 };
    Swing swing;
    run(swing, part, { 5, 0, 0 }, 20);
    CHECK(std::abs(swing.angle.value) < 0.9f * std::atan(5 / units::kGravity.value));
}

TEST_CASE("a swinging part swings no further than its furthest angle, and turns no faster than its fastest")
{
    SwingingPart part = sideways();
    part.furthest = units::Radians { 0.3f };
    part.fastest = units::TurnRate { 0.5f };
    Swing swing;
    for (int k = 0; k < 400; ++k)
    {
        swing_on(swing, part, kLevel, { 20, 0, 0 }, 0.05f);
        CHECK(std::abs(swing.angle.value) <= 0.3f + 1e-6f);
        CHECK(std::abs(swing.rate.value) <= 0.5f + 1e-6f);
    }
    CHECK(std::abs(swing.angle.value) == doctest::Approx(0.3f));
}

TEST_CASE("a swinging part's bearing friction stops a slow swing dead rather than turning it back")
{
    SwingingPart part = sideways();
    part.damping = { };
    part.friction = 0.5f;
    Swing swing { .angle = { 0.05f }, .rate = { 0.01f } };
    swing_on(swing, part, kLevel, glm::vec3(0.0f), 0.05f);
    CHECK(swing.rate.value == 0);
}

TEST_CASE("a part swinging forward and back swings back as its car speeds up, and hangs level up a slope")
{
    SwingingPart part = sideways();
    part.line = { 1, 0, 0 };
    Swing swing;
    run(swing, part, { 0, 0, 3 }, 20);
    const glm::vec3 hung = glm::mat3(glm::rotate(glm::mat4(1.0f), swing.angle.value, glm::vec3(1, 0, 0))) * glm::vec3(0, -1, 0);
    CHECK(hung.z < 0); // Behind its pivot
    CHECK(std::abs(swing.angle.value) == doctest::Approx(std::atan(3 / units::kGravity.value)).epsilon(1e-3));
    // Standing on a slope, its rest is level: it does not swing.
    Swing still;
    const glm::mat3 climbing(glm::rotate(glm::mat4(1.0f), -0.4f, glm::vec3(1, 0, 0)));
    for (int k = 0; k < 100; ++k)
        swing_on(still, part, climbing, glm::vec3(0.0f), 0.05f);
    CHECK(still.angle.value == doctest::Approx(0));
}

TEST_CASE("a part sprung to its car rests on the car, banked with it")
{
    SwingingPart part = sideways();
    part.spring = 50;
    Swing swing;
    const glm::mat3 banked(glm::rotate(glm::mat4(1.0f), 0.5f, glm::vec3(0, 0, 1)));
    for (int k = 0; k < 400; ++k)
        swing_on(swing, part, banked, glm::vec3(0.0f), 0.05f);
    // Gravity pulls it towards straight down, half a radian off the car's own down; the spring holds it near the car.
    CHECK(std::abs(swing.angle.value) < 0.5f);
    CHECK(std::abs(swing.angle.value) > 0.01f);
}
