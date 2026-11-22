// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fingerprint.hpp"

#include "core/types.hpp"
#include "parkfile/park_file.hpp"
#include "simulation/guests.hpp"
#include "simulation/sandbox.hpp"

#include <cstdlib>
#include <doctest/doctest.h>
#include <format>
#include <fstream>
#include <string>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A small park with something to do: a loop of paths, a food stall, a drink stand, a ride, a bin, a
    /// handyman and `parties` parties of three guests, the first in each holding a park map.
    void lay_park(Sandbox& park, i32 parties)
    {
        park.path({ 0, 0 }, { 9, 0 }).path({ 0, 1 }, { 0, 9 }).path({ 1, 9 }, { 9, 9 }).path({ 9, 1 }, { 9, 8 });
        park.shop("Food", Product::Food, { 9, 0 }, 150);
        park.shop("Drinks", Product::Drink, { 0, 9 }, 100);
        park.flat_ride("Ride", { 9, 9 }, 200);
        park.item(PathItemKind::Bin, { 4, 0 });
        park.staff(StaffRole::Handyman, { 0, 5 });
        park.litter({ 5, 9 }, 3);
        for (i32 i = 0; i < parties; ++i)
        {
            const entt::entity leader = park.guest({ i % 10, 0 }, { .thirst = 0.6f, .hunger = 0.5f });
            park.world().registry.emplace<ParkMap>(leader);
            park.party({ leader, park.guest({ i % 10, 0 }), park.guest({ i % 10, 0 }) });
        }
    }

    /// That park run for `ticks` on the main thread.
    u64 run_park(random::Seed seed, u32 ticks)
    {
        Sandbox park(seed);
        lay_park(park, 4);
        park.run(ticks);
        return fingerprint(park.world());
    }
} // namespace

TEST_CASE("a park run twice from the same seed ends with the same fingerprint")
{
    constexpr u32 kTicks = 2400; // 600 s of park time
    const u64 first = run_park(random::Seed { 1 }, kTicks);
    CHECK(run_park(random::Seed { 1 }, kTicks) == first);
    // The fingerprint tells runs apart: another seed rolls other dice, and a run a tick longer is at another tick.
    CHECK(run_park(random::Seed { 2 }, kTicks) != first);
    CHECK(run_park(random::Seed { 1 }, kTicks + 1) != first);
}

TEST_CASE("a park goes the same way however many threads step it and however often it is reloaded")
{
    // Enough guests that the workers share them out, each party's map holder looking at it as it sets off. The
    // fingerprint is taken every 240 ticks, so a run that parts from another shows about when. The last run is also
    // written to a park file and read back every 720 ticks.
    constexpr u32 kTicks = 2400; // 600 s of park time
    constexpr u32 kEvery = 240;
    const auto run = [](u32 helpers, bool reload) {
        Sandbox park(random::Seed { 1 }, helpers);
        lay_park(park, 80);
        std::string lines;
        for (u32 leg = 1; leg <= kTicks / kEvery; ++leg)
        {
            park.run(kEvery);
            if (reload && leg % 3 == 0)
            {
                const auto read = parkfile::read_park_file(parkfile::write_park_file(park.world()), park.world());
                REQUIRE_MESSAGE(read, read.error());
            }
            lines += std::format("tick {:d}: {:016x}\n", park.world().tick, fingerprint(park.world()));
        }
        return lines;
    };
    const std::string alone = run(0, false);
    CHECK(run(3, false) == alone);
    CHECK(run(3, true) == alone);
    // CI writes them out on each platform and compares the files: the same park must go the same way everywhere.
    if (const char* file = std::getenv("RCT3PP_FINGERPRINTS"))
    {
        std::ofstream out(file, std::ios::binary); // Binary: the same line ends on Windows
        out << alone;
        REQUIRE(out.good());
    }
}
