// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management/staff_roster_lines.hpp"

#include "simulation/staff/care.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;
using namespace rct3::ui::screens;
using namespace rct3::ui::screens::management;
using namespace rct3::ui::screens::info;

namespace
{
    save::model::Staff worker(save::model::StaffKind kind, std::string name)
    {
        save::model::Staff s;
        s.kind = kind;
        s.person.name = std::move(name);
        return s;
    }

    /// The names of the lines, in order.
    std::vector<std::u16string> names(const std::vector<RosterLine>& lines)
    {
        std::vector<std::u16string> out;
        for (const RosterLine& l : lines)
            out.push_back(l.name);
        return out;
    }

    using List = std::vector<std::u16string>;
} // namespace

TEST_CASE("the staff list starts without the vendors and shows or hides each job by its switch")
{
    const StringTable s;
    save::model::PeopleModel people;
    people.staff = { worker(save::model::StaffKind::Vendor, "Vi"), worker(save::model::StaffKind::Handyman, "Hal"), worker(save::model::StaffKind::ParkInspector, "Ina") };
    const ManagementFacts facts { .people = &people };
    CHECK(names(roster_lines(facts, s, kStartRosterJobs, RosterSort::Job)) == List { u"Hal", u"Ina" });
    CHECK(names(roster_lines(facts, s, kAllRosterJobs, RosterSort::Job)) == List { u"Hal", u"Vi", u"Ina" });
    // Without the handymen's switch.
    CHECK(names(roster_lines(facts, s, kStartRosterJobs & ~1u, RosterSort::Job)) == List { u"Ina" });
    CHECK(roster_lines(facts, s, 0, RosterSort::Job).empty());
}

TEST_CASE("the staff list never shows lifeguards, whatever the switches")
{
    const StringTable s;
    save::model::PeopleModel people;
    people.staff = { worker(save::model::StaffKind::LifeGuard, "Lu"), worker(save::model::StaffKind::Handyman, "Hal") };
    CHECK(names(roster_lines({ .people = &people }, s, kAllRosterJobs, RosterSort::Job)) == List { u"Hal" });
}

TEST_CASE("sorted by job the staff follow the switches' order, then the park's")
{
    const StringTable s;
    save::model::PeopleModel people;
    people.staff = { worker(save::model::StaffKind::AnimalKeeper, "Kay"), worker(save::model::StaffKind::Mechanic, "Mo"), worker(save::model::StaffKind::Handyman, "Hal"), worker(save::model::StaffKind::Mechanic, "Max") };
    const std::vector<RosterLine> lines = roster_lines({ .people = &people }, s, kAllRosterJobs, RosterSort::Job);
    CHECK(names(lines) == List { u"Hal", u"Mo", u"Max", u"Kay" });
    CHECK(lines[0].index == 2);
}

TEST_CASE("each sort puts the staff in its own order, ties in the order of the jobs")
{
    const StringTable s;
    save::model::PeopleModel people;
    save::model::Staff a = worker(save::model::StaffKind::Mechanic, "Bea");
    a.wage_level = 0;
    a.happiness = 0.9f;
    a.laziness = 0.1f;
    a.warnings = 2;
    a.training = 1;
    a.task_flags = 0b1;
    a.person.start_time = 50;
    save::model::Staff b = worker(save::model::StaffKind::Handyman, "Cid");
    b.wage_level = 2;
    b.happiness = 0.2f;
    b.laziness = 0.7f;
    b.warnings = 0;
    b.training = 3;
    b.task_flags = 0b111;
    b.person.start_time = 10;
    save::model::Staff c = worker(save::model::StaffKind::SecurityGuard, "Al");
    c.wage_level = 2;
    c.happiness = 0.5f;
    c.laziness = 0.7f;
    c.warnings = 1;
    c.training = 3;
    c.task_flags = 0b11;
    c.person.start_time = 30;
    people.staff = { a, b, c };
    const ManagementFacts facts { .people = &people };
    const auto sorted = [&](RosterSort sort) { return names(roster_lines(facts, s, kAllRosterJobs, sort)); };
    CHECK(sorted(RosterSort::Job) == List { u"Cid", u"Bea", u"Al" });
    CHECK(sorted(RosterSort::Name) == List { u"Al", u"Bea", u"Cid" });
    CHECK(sorted(RosterSort::Tasks) == List { u"Cid", u"Al", u"Bea" });
    CHECK(sorted(RosterSort::Wage) == List { u"Bea", u"Cid", u"Al" }); // By the money: a mechanic on the lowest level earns more than a guard on the highest
    CHECK(sorted(RosterSort::Happiness) == List { u"Bea", u"Al", u"Cid" });
    CHECK(sorted(RosterSort::Warnings) == List { u"Bea", u"Al", u"Cid" });
    CHECK(sorted(RosterSort::Training) == List { u"Cid", u"Al", u"Bea" });
    CHECK(sorted(RosterSort::StartDate) == List { u"Cid", u"Al", u"Bea" });
    CHECK(sorted(RosterSort::Laziness) == List { u"Cid", u"Al", u"Bea" });
}

TEST_CASE("the patrol and uniform sorts read the running park's looks, missing ones as none")
{
    const StringTable s;
    save::model::PeopleModel people;
    people.staff = { worker(save::model::StaffKind::Handyman, "Hal"), worker(save::model::StaffKind::Handyman, "Hugo"), worker(save::model::StaffKind::Handyman, "Hank") };
    const std::array<StaffLook, 2> looks = { { { .colours = { 5, 0 }, .patrol_tiles = 4 }, { .colours = { 2, 9 }, .patrol_tiles = 12 } } };
    const ManagementFacts facts { .people = &people, .staff_looks = looks };
    CHECK(names(roster_lines(facts, s, kAllRosterJobs, RosterSort::PatrolArea)) == List { u"Hugo", u"Hal", u"Hank" });
    CHECK(names(roster_lines(facts, s, kAllRosterJobs, RosterSort::Uniform)) == List { u"Hank", u"Hugo", u"Hal" });
}

TEST_CASE("a line holds the worker's wage and what their next training costs, nothing once fully trained")
{
    const StringTable s;
    save::model::PeopleModel people;
    save::model::Staff green = worker(save::model::StaffKind::Handyman, "Hal");
    green.training = 0;
    green.wage_level = 1;
    save::model::Staff trained = green;
    trained.training = simulation::staff::kMostTraining;
    people.staff = { green, trained };
    const std::vector<RosterLine> lines = roster_lines({ .people = &people }, s, kAllRosterJobs, RosterSort::Job);
    REQUIRE(lines.size() == 2);
    CHECK(lines[0].wage_level == 1);
    CHECK(lines[0].training_cost > 0);
    CHECK(lines[1].training_cost == 0);
    CHECK(lines[0].wage == simulation::staff::wage_at(simulation::StaffRole::Handyman, 1));
}
