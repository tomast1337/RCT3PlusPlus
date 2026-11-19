// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/spinning_part.hpp"

#include "core/types.hpp"

#include <cmath>
#include <cstdint>
#include <doctest/doctest.h>
#include <numbers>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A spinning coaster's tub, its weight a metre behind its pivot.
    SpinningPart tub()
    {
        return { .lever = { 0, 0, -1 }, .damping = { 0.3f }, .pull = { 0.3f } };
    }

    /// A car heading +Z, level, going round a steady left turn (its middle off to +X) at 8 m/s, 5 m/s² towards it.
    SpinCar turning()
    {
        return { .acceleration = { 5, 0, 0 }, .turning = { 8 * 5 / 64.0f } };
    }

    void run(Spin& spin, const SpinningPart& part, const SpinCar& car, f32 seconds)
    {
        for (f32 t = 0; t < seconds; t += 0.05f)
            spin_on(spin, part, car, 0.05f);
    }

    constexpr f32 kQuarter = std::numbers::pi_v<f32> / 2;
} // namespace

TEST_CASE("a spinning part on a car going straight on level track stays as it is")
{
    Spin spin { .angle = { 0.4f } };
    run(spin, tub(), SpinCar { }, 10);
    CHECK(spin.angle.value == doctest::Approx(0.4f));
    CHECK(spin.rate.value == doctest::Approx(0));
}

TEST_CASE("in a steady turn a spinning part turns its weight out of the turn and settles turning with its car")
{
    Spin spin;
    run(spin, tub(), turning(), 60);
    // Its weight, behind its pivot at first, ends up out of the turn, at -X, a little behind as its damping drags.
    const glm::vec3 weight = glm::mat3(glm::rotate(glm::mat4(1.0f), spin.angle.value, glm::vec3(0, 1, 0))) * glm::vec3(0, 0, -1);
    CHECK(weight.x < -0.95f);
    CHECK(spin.angle.value == doctest::Approx(kQuarter).epsilon(0.1));
    CHECK(spin.rate.value == doctest::Approx(turning().turning.value).epsilon(1e-2));
    // Settled: a further second moves it no more on its car.
    const f32 settled = spin.angle.value;
    run(spin, tub(), turning(), 1);
    CHECK(spin.angle.value == doctest::Approx(settled).epsilon(1e-3));
}

TEST_CASE("a free spinning part keeps its heading while its car turns under it")
{
    SpinningPart part = tub();
    part.pull = { };
    part.damping = { };
    Spin spin;
    run(spin, part, turning(), 1);
    CHECK(spin.angle.value == doctest::Approx(-turning().turning.value).epsilon(0.02));
}

TEST_CASE("on a slope a spinning part turns its weight downhill")
{
    // The car heads +Z, banked with its +X side up: gravity seen in the car pulls towards -X.
    const f32 bank = 0.3f;
    const glm::mat3 banked(glm::rotate(glm::mat4(1.0f), bank, glm::vec3(0, 0, 1)));
    Spin spin { .angle = { 0.1f } };
    run(spin, tub(), SpinCar { .axes = banked }, 60);
    const glm::vec3 weight = glm::mat3(glm::rotate(glm::mat4(1.0f), spin.angle.value, glm::vec3(0, 1, 0))) * glm::vec3(0, 0, -1);
    CHECK((banked * weight).y < -0.25f);
}

TEST_CASE("a spinning part's bearing friction stops a slow spin dead")
{
    SpinningPart part = tub();
    part.damping = { };
    part.friction = 0.5f;
    Spin spin { .rate = { 0.1f } };
    spin_on(spin, part, SpinCar { }, 0.05f);
    CHECK(spin.rate.value == 0);
    // A fast one it only slows.
    Spin fast { .rate = { 5 } };
    spin_on(fast, part, SpinCar { }, 0.05f);
    CHECK(fast.rate.value > 4.5f);
    CHECK(fast.rate.value < 5);
}

TEST_CASE("a spinning part's damping slows its spin by its rate each second")
{
    Spin spin { .rate = { 2 } };
    run(spin, tub(), SpinCar { }, 1);
    CHECK(spin.rate.value == doctest::Approx(2 * std::exp(-0.3f)).epsilon(0.02));
}

TEST_CASE("a spinning part held on a chain stops spinning there")
{
    SpinningPart part = tub();
    part.held_on_chains = true;
    Spin spin { .rate = { 3 } };
    run(spin, part, SpinCar { .ground = SpinGround::Chain }, 3);
    CHECK(spin.rate.value == 0);
    // One not held spins on.
    Spin free { .rate = { 3 } };
    run(free, tub(), SpinCar { .ground = SpinGround::Chain }, 3);
    CHECK(free.rate.value > 1);
}

TEST_CASE("at a station a spinning part squares up to the nearest of its stops, and stays there")
{
    for (const u32 stops : { 1u, 4u })
        for (const f32 from : { 0.5f, -2.5f, 3.0f })
            for (const f32 rate : { 0.0f, 3.0f, -1.5f })
            {
                CAPTURE(stops);
                CAPTURE(from);
                CAPTURE(rate);
                SpinningPart part = tub();
                part.stops = stops;
                Spin spin { .angle = { from }, .rate = { rate } };
                run(spin, part, SpinCar { .ground = SpinGround::Station }, 20);
                CHECK(spin.squaring == Squaring::Squared);
                CHECK(spin.rate.value == 0);
                const f32 step = 2 * std::numbers::pi_v<f32> / static_cast<f32>(stops);
                const f32 off = std::remainder(spin.angle.value, step);
                CHECK(std::abs(off) < 1e-4f);
                const f32 squared = spin.angle.value;
                run(spin, part, SpinCar { .acceleration = { 3, 0, 0 }, .ground = SpinGround::Station }, 2);
                CHECK(spin.angle.value == squared);
            }
    // All but still, it turns the shorter way back to facing ahead.
    Spin spin { .angle = { 0.6f } };
    spin_on(spin, tub(), SpinCar { .ground = SpinGround::Station }, 0.05f);
    CHECK(spin.rate.value < 0);
}

TEST_CASE("at a station a part that settles slows to a stop wherever it faces")
{
    SpinningPart part = tub();
    part.rest = SpinRest::Settles;
    Spin spin { .angle = { 1 }, .rate = { 2 } };
    run(spin, part, SpinCar { .ground = SpinGround::Station }, 10);
    CHECK(spin.squaring == Squaring::Squared);
    CHECK(spin.rate.value == 0);
    CHECK(std::abs(std::remainder(spin.angle.value, kQuarter)) > 0.01f);
}

TEST_CASE("a sprung spinning part is drawn back to face ahead, and held within its furthest and fastest")
{
    SpinningPart part = tub();
    part.spring = 4;
    part.damping = { 2 };
    part.furthest = units::Radians { 0.5f };
    part.fastest = units::TurnRate { 1 };
    Spin spin;
    for (i32 k = 0; k < 400; ++k)
    {
        spin_on(spin, part, turning(), 0.05f);
        CHECK(std::abs(spin.angle.value) <= 0.5f + 1e-6f);
        CHECK(std::abs(spin.rate.value) <= 1 + 1e-6f);
    }
    Spin back { .angle = { 0.4f } };
    run(back, part, SpinCar { }, 10);
    CHECK(std::abs(back.angle.value) < 0.02f);
}
