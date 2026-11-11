// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/staff/motions.hpp"

#include "park/person/motions.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using simulation::Breakdown;
using simulation::KeeperJob;
using simulation::StaffJob;

TEST_CASE("every motion a worker plays at work is a known one")
{
    for (int job = 0; job <= static_cast<int>(StaffJob::Rest); ++job)
        for (int kind = 0; kind <= static_cast<int>(Breakdown::ControlFailure); ++kind)
        {
            const simulation::staff::WorkLook look = simulation::staff::work_look(static_cast<StaffJob>(job), static_cast<Breakdown>(kind));
            for (const std::string_view name : { look.start, look.loop, look.finish })
                CHECK_MESSAGE((name.empty() || park::person::motion_number(name).has_value()), name);
            CHECK((job == static_cast<int>(StaffJob::None)) == look.loop.empty());
        }
    for (const KeeperJob job : { KeeperJob::Idle, KeeperJob::Strolling, KeeperJob::Filling, KeeperJob::Clearing, KeeperJob::Catching })
        for (const bool walking : { false, true })
            CHECK(park::person::motion_number(simulation::staff::keeper_motion(job, walking)).has_value());
}

TEST_CASE("a handyman sweeps with a broom and waters with a can, and empties a bin with the bag they wear")
{
    CHECK(simulation::staff::work_look(StaffJob::Sweep, Breakdown::None) == simulation::staff::WorkLook { .loop = "sweep", .tool = "Brush" });
    CHECK(simulation::staff::work_look(StaffJob::Water, Breakdown::None) == simulation::staff::WorkLook { .loop = "watergarden", .tool = "WaterCan" });
    CHECK(simulation::staff::work_look(StaffJob::EmptyBin, Breakdown::None).tool.empty());
}

TEST_CASE("a mechanic works on a breakdown as it went wrong, and inspects with a look over the ride")
{
    CHECK(simulation::staff::work_look(StaffJob::Fix, Breakdown::StationBrakesFail).loop == "fixbrakesloop");
    CHECK(simulation::staff::work_look(StaffJob::Fix, Breakdown::DoorsFailToOpen).loop == "fixdooropenclose");
    CHECK(simulation::staff::work_look(StaffJob::Fix, Breakdown::RestraintsFailToClose).loop == "restraintfailopenclose");
    const simulation::staff::WorkLook stuck = simulation::staff::work_look(StaffJob::Fix, Breakdown::TrainStuckLeavingStation);
    CHECK(stuck.start == "fixbrokentrainin");
    CHECK(stuck.finish == "fixbrokentrainout");
    CHECK(simulation::staff::work_look(StaffJob::Inspect, Breakdown::None).loop == "inspectride");
    CHECK(simulation::staff::work_look(StaffJob::Fix, Breakdown::None).tool.empty()); // Their tools are part of them
}

TEST_CASE("a resting worker sits down, sits, and gets up; a guard on watch swings the truncheon, then radios in")
{
    const simulation::staff::WorkLook rest = simulation::staff::work_look(StaffJob::Rest, Breakdown::None);
    CHECK(park::person::motion_number(rest.start) == park::person::motion_number("sitin"));
    CHECK(park::person::motion_number(rest.finish) == park::person::motion_number("sitout"));
    CHECK(simulation::staff::work_look(StaffJob::Watch, Breakdown::None) == simulation::staff::WorkLook { .start = "swingtruncheon", .loop = "talkradio" });
}

TEST_CASE("a keeper carries the container to the troughs and the shovel otherwise")
{
    CHECK(simulation::staff::keeper_motion(KeeperJob::Filling, true) == "walkwithcontainer");
    CHECK(simulation::staff::keeper_motion(KeeperJob::Filling, false) == "containerloop");
    CHECK(simulation::staff::keeper_motion(KeeperJob::Clearing, false) == "shovelloop");
    CHECK(simulation::staff::keeper_motion(KeeperJob::Strolling, true) == "walkwithshovel");
}
