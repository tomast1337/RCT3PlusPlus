// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff/care.hpp"

#include "simulation/tuning.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::simulation;
using namespace rct3::simulation::staff;

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
    Staff untrained { .role = StaffRole::Handyman, .training = 0 };
    set_wage_level(untrained, 0);
    Staff trained { .role = StaffRole::Handyman, .training = kMostTraining };
    set_wage_level(trained, kMostWageLevel);
    live(untrained, 0.5f, 10);
    live(trained, 0.5f, 10);
    CHECK(trained.happiness.value > untrained.happiness.value + 0.3f);
    CHECK(trained.wage.value > untrained.wage.value);
}

TEST_CASE("each job has its own monthly wage at each level; inspectors and lifeguards are not paid")
{
    // As the game's staff window shows them, as hired and after a raise.
    CHECK(wage_at(StaffRole::Handyman, 1) == 3500);
    CHECK(wage_at(StaffRole::Handyman, 2) == 4200);
    CHECK(wage_at(StaffRole::Mechanic, 1) == 5500);
    CHECK(wage_at(StaffRole::Mechanic, 2) == 6500);
    CHECK(wage_at(StaffRole::Guard, 1) == 1500);
    CHECK(wage_at(StaffRole::Guard, 2) == 2000);
    CHECK(wage_at(StaffRole::Entertainer, 1) == 4000);
    CHECK(wage_at(StaffRole::Entertainer, 2) == 4800);
    for (const StaffRole role : { StaffRole::Handyman, StaffRole::Mechanic, StaffRole::Guard, StaffRole::Vendor, StaffRole::Keeper, StaffRole::Entertainer })
    {
        CHECK(wage_at(role, 0) > 0);
        CHECK(wage_at(role, 0) < wage_at(role, 1));
        CHECK(wage_at(role, 1) < wage_at(role, 2));
        CHECK(wage_at(role, 5) == wage_at(role, kMostWageLevel));
    }
    CHECK(wage_at(StaffRole::Keeper, 1) < wage_at(StaffRole::Guard, 1));
    CHECK(wage_at(StaffRole::Inspector, 2) == 0);
    CHECK(wage_at(StaffRole::Other, 2) == 0);
    CHECK(staff_role(rct3::save::model::StaffKind::LifeGuard) == StaffRole::Other);
    // Training leaves the wage alone.
    Staff s { .role = StaffRole::Mechanic, .training = 0 };
    set_wage_level(s, 1);
    REQUIRE(train(s));
    CHECK(s.wage.value == 5500);
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
    rct3::units::Money last;
    for (int level = 0; level < kMostTraining; ++level)
    {
        CHECK(training_cost(s) > last);
        last = training_cost(s);
        CHECK(train(s));
    }
    CHECK(s.training == kMostTraining);
    CHECK(training_cost(s) == rct3::units::Money { });
    CHECK_FALSE(train(s));
}

TEST_CASE("a park without money hires staff fully trained; where money counts they start untrained")
{
    CHECK(hired_training(false) == kMostTraining);
    CHECK(hired_training(true) == 0);
}

TEST_CASE("each level of training costs what the tuning says for it")
{
    Staff s { .training = 0 };
    for (std::size_t level = 0; level < kTuning.staff.training_cost.size(); ++level)
    {
        CHECK(training_cost(s) == kTuning.staff.training_cost[level]);
        CHECK(train(s));
    }
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

TEST_CASE("staff think about their work and how they feel")
{
    const auto thinks = [](Staff s) { return staff_thoughts(s); };
    using enum ThoughtKind;
    CHECK(thinks({ .happiness = rct3::units::Share { 1 }, .load = rct3::units::Share { 0 } }) == std::vector { StaffBored, StaffHappy });
    CHECK(thinks({ .wage_level = 2, .happiness = rct3::units::Share { 0.6f }, .load = rct3::units::Share { 1 } }) == std::vector { StaffTooBusy, StaffMotivated });
    CHECK(thinks({ .wage_level = 0, .happiness = rct3::units::Share { 0.6f }, .laziness = rct3::units::Share { 0.7f }, .load = rct3::units::Share { 1 } }) == std::vector { StaffUnderpaid, StaffTired });

    SUBCASE("one miserable for long thinks of quitting, and stops once content again")
    {
        Staff sad { .happiness = 0.1f, .load = 0.5f };
        CHECK(staff_thoughts(sad) == std::vector { StaffMiserable });
        for (int i = 0; i < 100 && staff_thoughts(sad) != std::vector { StaffWantToQuit }; ++i)
            ;
        CHECK(sad.miserable > 1);
        sad.happiness.value = 0.6f;
        CHECK(staff_thoughts(sad) == std::vector { StaffMotivated });
        CHECK(sad.miserable == 0);
    }
}

TEST_CASE("a miserable worker thinks of quitting once a stretch, then quits once")
{
    using enum ThoughtKind;
    Staff sad { .happiness = 0.1f, .load = 0.5f };
    int wanted = 0;
    int quit = 0;
    for (int i = 0; i < 200; ++i)
    {
        wanted += static_cast<int>(std::ranges::count(staff_thoughts(sad), StaffWantToQuit));
        quit += quits(sad) ? 1 : 0;
        CHECK(!(quit == 1 && wanted == 0)); // Thought of it before quitting
    }
    CHECK(wanted == 1);
    CHECK(quit == 1);

    SUBCASE("cheered up and miserable again, they think of it again")
    {
        sad.happiness.value = 0.6f;
        (void)staff_thoughts(sad);
        sad.happiness.value = 0.1f;
        for (int i = 0; i < 200; ++i)
            wanted += static_cast<int>(std::ranges::count(staff_thoughts(sad), StaffWantToQuit));
        CHECK(wanted == 2);
    }
}

TEST_CASE("a park inspector keeps their mood, thinks nothing of it and never quits")
{
    Staff inspector { .role = StaffRole::Inspector, .happiness = 0.5f, .load = 0 };
    for (int i = 0; i < 200; ++i)
    {
        care_for(inspector, false, 10);
        CHECK(staff_thoughts(inspector).empty());
        CHECK(!quits(inspector));
    }
    CHECK(inspector.happiness.value == 0.5f);
    CHECK(inspector.laziness.value == 0);
}

TEST_CASE("staff take up the work their save counts")
{
    Staff handyman;
    take_saved_work(handyman, { .work = rct3::save::model::HandymanWork { .bins_emptied = 4, .paths_swept = 6 } });
    CHECK(handyman.swept == 6);
    CHECK(handyman.emptied == 4);
    Staff mechanic;
    take_saved_work(mechanic, { .work = rct3::save::model::MechanicWork { .objects_fixed = 2, .objects_inspected = 1 } });
    CHECK(mechanic.fixed == 2);
    CHECK(mechanic.inspected == 1);
}
