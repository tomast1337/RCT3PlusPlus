// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fingerprint.hpp"

#include "core/types.hpp"
#include "simulation/sandbox.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// A small park with something to do: a loop of paths, a food stall, a drink stand, a ride, a bin, a
    /// handyman and a few parties of guests, run for `ticks`.
    u64 run_park(random::Seed seed, u32 ticks)
    {
        Sandbox park(seed);
        park.path({ 0, 0 }, { 9, 0 }).path({ 0, 1 }, { 0, 9 }).path({ 1, 9 }, { 9, 9 }).path({ 9, 1 }, { 9, 8 });
        park.shop("Food", Product::Food, { 9, 0 }, 150);
        park.shop("Drinks", Product::Drink, { 0, 9 }, 100);
        park.flat_ride("Ride", { 9, 9 }, 200);
        park.item(PathItemKind::Bin, { 4, 0 });
        park.staff(StaffRole::Handyman, { 0, 5 });
        park.litter({ 5, 9 }, 3);
        for (i32 i = 0; i < 4; ++i)
            park.party({ park.guest({ i, 0 }, { .thirst = 0.6f, .hunger = 0.5f }), park.guest({ i, 0 }), park.guest({ i, 0 }) });
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
