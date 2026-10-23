// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff_care.hpp"

#include <doctest/doctest.h>

using namespace rct3::simulation;

namespace
{
    /// `hours` of work at `load`, a minute at a time.
    void live(Staff& s, float load, float hours)
    {
        for (int minute = 0; minute < static_cast<int>(hours * 60); ++minute)
            care_for(s, (minute % 10) < static_cast<int>(load * 10), 60);
    }
} // namespace

TEST_CASE("training and pay make staff content")
{
    Staff untrained { .training = 0 };
    set_wage_level(untrained, 0);
    Staff trained { .training = kMostTraining };
    set_wage_level(trained, kMostWageLevel);
    live(untrained, 0.5f, 10);
    live(trained, 0.5f, 10);
    CHECK(trained.happiness.value > untrained.happiness.value + 0.3f);
    CHECK(trained.wage.value > untrained.wage.value);
}

TEST_CASE("overwork and idleness both make staff unhappy, and unhappy staff grow lazy")
{
    Staff steady { .training = 1 };
    Staff overworked { .training = 1 };
    Staff idle { .training = 1 };
    live(steady, 0.5f, 10);
    live(overworked, 1.0f, 10);
    live(idle, 0.0f, 10);
    CHECK(overworked.happiness.value < steady.happiness.value);
    CHECK(idle.happiness.value < steady.happiness.value);
    CHECK(idle.laziness.value > steady.laziness.value);

    SUBCASE("training slows how fast laziness grows")
    {
        Staff raw { .training = 0, .happiness = 0.2f };
        Staff schooled { .training = kMostTraining, .happiness = 0.2f };
        care_for(raw, false, 60);
        care_for(schooled, false, 60);
        CHECK(raw.laziness.value > schooled.laziness.value);
    }
}

TEST_CASE("laziness slows the work and brings rests")
{
    Staff keen;
    Staff lazy { .laziness = 0.5f };
    CHECK(work_slowdown(keen) == 1);
    CHECK(work_slowdown(lazy) == doctest::Approx(1.5));
    CHECK(rest_chance(keen) == 0);
    CHECK(rest_chance(lazy) > 0);
}

TEST_CASE("training goes up a level at a time, dearer each time, up to fully trained")
{
    Staff s { .training = 0 };
    std::int64_t last = 0;
    for (int level = 0; level < kMostTraining; ++level)
    {
        CHECK(training_cost(s) > last);
        last = training_cost(s);
        CHECK(train(s));
    }
    CHECK(s.training == kMostTraining);
    CHECK(training_cost(s) == 0);
    CHECK_FALSE(train(s));
}

TEST_CASE("a warning shakes off laziness, but upsets a worker who was not lazy")
{
    Staff lazy { .happiness = 0.4f, .laziness = 0.6f };
    warn(lazy);
    CHECK(lazy.laziness.value == 0);
    CHECK(lazy.happiness.value == doctest::Approx(0.4f));
    Staff good { .happiness = 0.9f };
    warn(good);
    CHECK(good.happiness.value < 0.9f);
    CHECK(good.warnings == 1);
}
