// SPDX-License-Identifier: GPL-3.0-or-later
#include "core/types.hpp"
#include "simulation/actions/action.hpp"
#include "simulation/components.hpp"
#include "simulation/staff/care.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation::actions;

namespace
{
    struct Park
    {
        simulation::SimWorld sim { };
        simulation::park::ParkState state { };
        save::model::ParkModel model { };
        save::model::RideModel rides { };
        simulation::Research research { };
        std::optional<std::vector<simulation::ResearchItem>> research_items { };
        std::optional<i64> ledger_month { };
        simulation::DayClock day_clock { };
        rct3::park::CarriedKinds carried { };
        std::vector<save::model::StallIndex> unjoined_stalls { };
        std::vector<rct3::park::track::TrackRules> track_rules { };
        std::vector<std::optional<rct3::park::track::TrackCourse>> courses { };
        rct3::park::pools::PoolMap pools { };
        std::optional<save::model::RideIndex> building { };
        std::vector<save::model::RideIndex> readying { };
        std::vector<save::model::RideShow> shows { };
        save::model::PeopleModel people { };
        std::vector<entt::entity> staff_walkers { };
        std::vector<rct3::park::person::Outfit> wardrobe { };
        usize costumes_given = 0;
        bool simulating = true;
        std::vector<save::model::PlacedObject> objects { };
        Changed changed { };
        save::model::TerrainGrid grid { };
        save::model::EnclosureMap enclosures { };
        World world { .sim = sim,
                      .park = state,
                      .model = model,
                      .rides = rides,
                      .research = research,
                      .research_items = research_items,
                      .ledger_month = ledger_month,
                      .day_clock = day_clock,
                      .carried = carried,
                      .unjoined_stalls = unjoined_stalls,
                      .track_rules = track_rules,
                      .courses = courses,
                      .pools = pools,
                      .building = building,
                      .readying = readying,
                      .shows = shows,
                      .people = people,
                      .staff_walkers = staff_walkers,
                      .wardrobe = wardrobe,
                      .costumes_given = costumes_given,
                      .simulating = simulating,
                      .objects = objects,
                      .changed = changed,
                      .grid = grid,
                      .enclosures = enclosures };

        /// A short path, unless `pathless`, and a uniform of each part for handymen and two entertainer costumes,
        /// 0 and 2.
        explicit Park(bool pathless = false)
        {
            for (i32 c = 0; c < (pathless ? 0 : 3); ++c)
                sim.paths.add({ c, 0, 0 });
            for (u8 section = 0; section < 3; ++section)
            {
                wardrobe.push_back({ .id = 10u + section, .type = save::model::PersonType::Handyman, .section = section });
                for (const u8 costume : { 0, 2 })
                    wardrobe.push_back({ .id = 20u + 10u * costume + section, .type = save::model::PersonType::Entertainer, .section = section, .costume = costume });
            }
        }

        save::model::PeopleObjectId hire(save::model::StaffKind kind)
        {
            REQUIRE(simulation::actions::apply(world, Action { HireStaff { .kind = kind } }));
            return people.staff.back().person.id;
        }
    };
} // namespace

TEST_CASE("a worker hired walks the paths in their job's uniform, saved under an id of their own")
{
    Park p;
    const save::model::PeopleObjectId first = p.hire(save::model::StaffKind::Handyman);
    const save::model::PeopleObjectId second = p.hire(save::model::StaffKind::Handyman);
    CHECK(first != 0);
    CHECK(second != first);
    REQUIRE(p.people.staff.size() == 2);
    REQUIRE(p.staff_walkers.size() == 2);
    const save::model::Staff& kept = p.people.staff[0];
    CHECK(kept.type_id == 1);
    CHECK(p.people.staff[1].type_id == 2);
    CHECK(kept.person.type == save::model::PersonType::Handyman);
    CHECK(kept.person.appearance.head.id == 10);
    CHECK(kept.person.appearance.legs.id == 12);
    CHECK(kept.person.appearance.palettes[0] < rct3::park::person::kColourCells);
    CHECK(std::holds_alternative<save::model::HandymanWork>(kept.work));
    CHECK(p.sim.registry.get<simulation::Staff>(p.staff_walkers[0]).role == simulation::StaffRole::Handyman);
    CHECK(p.changed.hired_staff == p.staff_walkers);
}

TEST_CASE("entertainers hired wear the costumes in turn")
{
    Park p;
    for (const u8 costume : { 0, 2, 0 })
    {
        p.hire(save::model::StaffKind::Entertainer);
        CHECK(p.people.staff.back().person.sub_type == costume);
        CHECK(p.people.staff.back().person.appearance.body.id == 21u + 10u * costume);
    }
}

TEST_CASE("nobody is hired where there is no path, no simulation or no such job to hire for")
{
    Park p;
    CHECK(simulation::actions::apply(p.world, Action { HireStaff { .kind = save::model::StaffKind::Vendor } }).error() == Refusal::Forbidden);
    CHECK(simulation::actions::apply(p.world, Action { HireStaff { .kind = save::model::StaffKind::AnimalKeeper } }).error() == Refusal::NothingToDo);
    CHECK(simulation::actions::apply(p.world, Action { HireStaff { .kind = save::model::StaffKind::Mechanic } }).error() == Refusal::NothingToDo); // No uniform
    p.simulating = false;
    CHECK(simulation::actions::apply(p.world, Action { HireStaff { .kind = save::model::StaffKind::Handyman } }).error() == Refusal::Forbidden);
    Park pathless { true };
    CHECK(simulation::actions::apply(pathless.world, Action { HireStaff { .kind = save::model::StaffKind::Handyman } }).error() == Refusal::NothingToDo);
    CHECK(p.people.staff.empty());
    CHECK(pathless.people.staff.empty());
}

TEST_CASE("a worker sacked leaves the park, and their place goes on the record")
{
    Park p;
    const save::model::PeopleObjectId first = p.hire(save::model::StaffKind::Handyman);
    const save::model::PeopleObjectId second = p.hire(save::model::StaffKind::Handyman);
    const entt::entity walker = p.staff_walkers[0];
    REQUIRE(simulation::actions::apply(p.world, Action { FireStaff { .worker = first } }));
    CHECK_FALSE(p.sim.registry.valid(walker));
    REQUIRE(p.people.staff.size() == 1);
    CHECK(p.people.staff[0].person.id == second);
    CHECK(p.staff_walkers.size() == 1);
    CHECK(p.changed.sacked_staff == std::vector<usize> { 0 });
    CHECK(simulation::actions::apply(p.world, Action { FireStaff { .worker = first } }).error() == Refusal::Missing);
}

TEST_CASE("a worker's page changes them: name, switches, tasks and patrol")
{
    Park p;
    const save::model::PeopleObjectId id = p.hire(save::model::StaffKind::Handyman);
    const save::model::Staff& kept = p.people.staff[0];
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Name, .text = "Sam" } }));
    CHECK(kept.person.name == "Sam");
    CHECK(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Name } }).error() == Refusal::NothingToDo);
    CHECK(kept.person.name == "Sam");
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Highlight } }));
    CHECK(kept.person.highlight);
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::ToConsole } }));
    CHECK(kept.person.thoughts_to_console);
    const i32 tasks = kept.task_flags;
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Task, .value = 1 } }));
    CHECK(kept.task_flags == (tasks ^ 2));
    CHECK(p.sim.registry.get<simulation::Staff>(p.staff_walkers[0]).tasks == kept.task_flags);
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::PatrolZone, .value = 3 } }));
    CHECK(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = 12345, .setting = StaffSetting::Highlight } }).error() == Refusal::Missing);
}

TEST_CASE("pay, training and warnings change the walker, and the saved worker shows it")
{
    Park p;
    const save::model::PeopleObjectId id = p.hire(save::model::StaffKind::Handyman);
    const save::model::Staff& kept = p.people.staff[0];
    const simulation::Staff& walker = p.sim.registry.get<simulation::Staff>(p.staff_walkers[0]);
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Wage, .value = 2 } }));
    CHECK(walker.wage_level == 2);
    CHECK(kept.wage_level == 2);
    for (i32 n = 0; n < simulation::staff::kMostWarnings; ++n)
        REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Warn } }));
    CHECK(kept.warnings == simulation::staff::kMostWarnings);
    CHECK(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Warn } }).error() == Refusal::NothingToDo);

    SUBCASE("training is paid for, and not given where the park cannot pay")
    {
        p.state.cash = units::Money { 0 };
        p.sim.registry.get<simulation::Staff>(p.staff_walkers[0]).training = 0;
        REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Train } }));
        CHECK(walker.training == 0);
        p.state.cash = units::Money { 1'000'000 };
        REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Train } }));
        CHECK(walker.training == 1);
        CHECK(kept.training == 1);
        CHECK(*p.state.cash < units::Money { 1'000'000 });
    }
    SUBCASE("a worker with no walker is changed as saved")
    {
        p.staff_walkers[0] = entt::null;
        REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Wage, .value = 0 } }));
        CHECK(kept.wage_level == 0);
        CHECK(walker.wage_level == 2);
        CHECK(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::ClearPatrol } }).error() == Refusal::Missing);
    }
}

TEST_CASE("a worker's colours and costume change their clothes, and go on the record")
{
    Park p;
    const save::model::PeopleObjectId id = p.hire(save::model::StaffKind::Entertainer);
    const save::model::Person& kept = p.people.staff[0].person;
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Colour, .value = 7, .pot = 2 } }));
    CHECK(kept.appearance.palettes[2] == 7);
    CHECK(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Colour, .value = 7, .pot = kept.appearance.palettes.size() } }).error() == Refusal::Missing);
    REQUIRE(simulation::actions::apply(p.world, Action { ChangeStaff { .worker = id, .setting = StaffSetting::Costume, .value = 2 } }));
    CHECK(kept.sub_type == 2);
    CHECK(kept.appearance.head.id == 40);
    CHECK(p.changed.dressed_staff == std::vector<save::model::PeopleObjectId> { id, id });
}
