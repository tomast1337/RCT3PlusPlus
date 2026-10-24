// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crash.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    /// Thrown level at 20 m/s from 10 m up.
    Wreck thrown()
    {
        return Wreck { .at = { 0, 10, 0 }, .velocity = { 20, 0, 0 }, .spin = { 0, 0, 3 } };
    }
} // namespace

TEST_CASE("a wreck falls as it flies on, tumbling")
{
    Wreck w = thrown();
    CHECK(fly(w, 0.5f, { }) == WreckEvent::None);
    CHECK(w.at.x == doctest::Approx(10).epsilon(0.01));
    CHECK(w.at.y < 10 - 1);
    CHECK(w.turned != glm::quat(1, 0, 0, 0));
}

TEST_CASE("a wreck bounces the first time it lands and blows up the next")
{
    Wreck w = thrown();
    WreckEvent first = WreckEvent::None;
    for (int i = 0; i < 100 && first == WreckEvent::None; ++i)
        first = fly(w, 0.05f, { });
    CHECK(first == WreckEvent::Bounced);
    CHECK(w.velocity.y > 0);
    CHECK(w.state == WreckState::Flying);
    WreckEvent next = WreckEvent::None;
    for (int i = 0; i < 100 && next == WreckEvent::None; ++i)
        next = fly(w, 0.05f, { });
    CHECK(next == WreckEvent::BlewUp);
    CHECK(w.state == WreckState::Burning);
    CHECK(w.at.y == 0);
}

TEST_CASE("a wreck lands on the ground's own height")
{
    Wreck w = thrown();
    const WreckSurroundings hill { .ground = [](glm::vec2 p) { return p.x > 5 ? 6.0f : 0.0f; } };
    WreckEvent event = WreckEvent::None;
    for (int i = 0; i < 100 && event == WreckEvent::None; ++i)
        event = fly(w, 0.05f, hill);
    CHECK(event == WreckEvent::Bounced);
    CHECK(w.at.y == 6);
}

TEST_CASE("a wreck blows up on what it hits")
{
    Wreck w = thrown();
    const WreckSurroundings wall { .struck = [](glm::vec3 p) { return p.x > 4; } };
    WreckEvent event = WreckEvent::None;
    for (int i = 0; i < 100 && event == WreckEvent::None; ++i)
        event = fly(w, 0.05f, wall);
    CHECK(event == WreckEvent::BlewUp);
    CHECK(w.at.x < 6);
    CHECK(w.at.y > 5); // Still up in the air
}

TEST_CASE("a wreck flying too long blows up in the air")
{
    Wreck w = thrown();
    const WreckSurroundings pit { .ground = [](glm::vec2) { return -10000.0f; } };
    float seconds = 0;
    while (seconds < 60 && w.state == WreckState::Flying)
    {
        fly(w, 0.5f, pit);
        seconds += 0.5f;
    }
    CHECK(w.state == WreckState::Burning);
    CHECK(seconds < 10);
}

TEST_CASE("a wreck burns where it lies for a while, then is gone")
{
    Wreck w = thrown();
    w.state = WreckState::Burning;
    const glm::vec3 lies = w.at;
    CHECK(fly(w, 1, { }) == WreckEvent::None);
    CHECK(w.at == lies);
    WreckEvent event = WreckEvent::None;
    for (int i = 0; i < 100 && event == WreckEvent::None; ++i)
        event = fly(w, 0.5f, { });
    CHECK(event == WreckEvent::Vanished);
    CHECK(w.state == WreckState::Gone);
    CHECK(fly(w, 1, { }) == WreckEvent::None);
}
