// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_fences.hpp"

#include "park/object_placement.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <set>
#include <tuple>

using namespace rct3;
using namespace rct3::park;

namespace
{

    save::PlacementSpot spot(int col, int row, int height, std::uint8_t direction)
    {
        save::PlacementSpot s;
        s.col = col;
        s.row = row;
        s.height = height;
        s.direction = direction;
        return s;
    }

    save::Ride flat_ride(save::PlacementSpot at)
    {
        save::Ride r;
        r.kind = save::RideKind::Flat;
        r.base.spot = at;
        r.fence_symbol = "Hedge";
        r.fence_overlay = "Fences/Hedge";
        r.fence_colours = { 4, 5, 6 };
        return r;
    }

    ObjectModel footprint(int x_squares, int z_squares)
    {
        ObjectModel m;
        m.squares = { x_squares, z_squares };
        return m;
    }

    using Edge = std::tuple<int, int, int>; // Column, row and turn of a piece

    std::set<Edge> edges(const std::vector<save::PlacedObject>& pieces)
    {
        std::set<Edge> out;
        for (const save::PlacedObject& p : pieces)
            out.emplace(p.col, p.row, p.rotation);
        return out;
    }

    std::array<int, 2> beyond(const Edge& e)
    {
        const glm::vec2 x = turned_x(std::get<2>(e));
        return { std::get<0>(e) + static_cast<int>(std::lround(x.x)), std::get<1>(e) + static_cast<int>(std::lround(x.y)) };
    }

} // namespace

TEST_CASE("a ride without a fence has none, and a stand-in fence replaces its own")
{
    save::Ride r = flat_ride(spot(10, 20, 0, 0));
    const ObjectModel item = footprint(2, 2);
    r.fence_symbol.clear();
    CHECK(ride_fences(r, &item, 0).empty());

    r.override_fence_symbol = "Tape";
    r.override_fence_overlay = "Fences/Tape";
    const auto pieces = ride_fences(r, &item, 0);
    REQUIRE(pieces.size() == 8);
    CHECK(pieces[0].symbol == "Tape");
    CHECK(pieces[0].fixture);
    CHECK(pieces[0].overlay == "Fences/Tape");

    save::Ride facility = flat_ride(spot(10, 20, 0, 0));
    facility.kind = save::RideKind::Special;
    CHECK(ride_fences(facility, &item, 0).empty());
}

TEST_CASE("an entrance and an exit are walled on the two sides beside the way they face")
{
    save::Ride r = flat_ride(spot(10, 20, 0, 0));
    r.kind = save::RideKind::Tracked;
    save::Station s;
    s.entrance_spot = spot(3, 4, 7, 0);
    s.exit_spot = spot(5, 6, 2, 3);
    r.stations = { s };
    const ObjectModel item = footprint(2, 2); // A tracked ride is not fenced round
    const auto pieces = ride_fences(r, &item, 50);
    CHECK(edges(pieces) == std::set<Edge> { { 3, 4, 3 }, { 3, 4, 1 }, { 5, 6, 2 }, { 5, 6, 0 } });
    for (const save::PlacedObject& p : pieces)
    {
        CHECK(p.base_height == (p.col == 3 ? 7 : 2));
        CHECK(p.force_absolute_height);
        CHECK(p.symbol == "Hedge");
        CHECK(p.colours == std::array<std::int32_t, 3> { 4, 5, 6 });
        CHECK(p.id > 50);
        CHECK(p.free_standing());
    }
}

TEST_CASE("a flat ride is fenced round the rim of its footprint, at its height")
{
    const save::Ride r = flat_ride(spot(10, 20, 4, 0));
    const ObjectModel item = footprint(3, 2);
    const auto pieces = ride_fences(r, &item, 0);
    CHECK(pieces.size() == 10);
    const std::set<Edge> fenced = edges(pieces);
    CHECK(fenced.size() == pieces.size());
    for (const Edge& e : fenced)
    {
        // On a tile of the footprint, facing out of it.
        CHECK(std::get<0>(e) >= 10);
        CHECK(std::get<0>(e) <= 12);
        CHECK(std::get<1>(e) >= 20);
        CHECK(std::get<1>(e) <= 21);
        const auto [c, row] = beyond(e);
        CHECK((c < 10 || c > 12 || row < 20 || row > 21));
    }
    CHECK(std::ranges::all_of(pieces, [](const save::PlacedObject& p) { return p.base_height == 4; }));
}

TEST_CASE("a turned flat ride's footprint lies across, and its doors leave gaps")
{
    save::Ride r = flat_ride(spot(10, 20, 0, 1)); // Three squares along its X now run along the park's rows
    save::Station s;
    s.entrance_spot = spot(12, 21, 0, 0); // Beside the footprint's +x side
    s.exit_spot = spot(10, 19, 0, 1);     // Beside its -z side
    save::Station second;
    second.entrance_spot = spot(9, 20, 0, 2); // Only the first station opens a gap
    r.stations = { s, second };
    const ObjectModel item = footprint(3, 2);
    const std::set<Edge> fenced = edges(ride_fences(r, &item, 0));

    std::set<Edge> ring;
    for (const Edge& e : fenced)
    {
        if (e == Edge { 12, 21, 3 } || e == Edge { 12, 21, 1 } || e == Edge { 10, 19, 0 } || e == Edge { 10, 19, 2 } || e == Edge { 9, 20, 1 } || e == Edge { 9, 20, 3 })
            continue;
        ring.insert(e);
        CHECK(std::get<0>(e) >= 10);
        CHECK(std::get<0>(e) <= 11);
        CHECK(std::get<1>(e) >= 20);
        CHECK(std::get<1>(e) <= 22);
    }
    CHECK(ring.size() == 8); // Ten round the rim, less the two doors
    CHECK_FALSE(ring.contains({ 11, 21, 2 }));
    CHECK_FALSE(ring.contains({ 10, 20, 3 }));
    CHECK(ring.contains({ 10, 20, 0 })); // Shut across the second station's entrance
}

TEST_CASE("a ride on water is not fenced round, and one a square across is closed on both sides")
{
    save::Ride r = flat_ride(spot(0, 0, 0, 0));
    ObjectModel pool = footprint(2, 2);
    pool.water_only = true;
    CHECK(ride_fences(r, &pool, 0).empty());
    CHECK(ride_fences(r, nullptr, 0).empty());

    const ObjectModel strip = footprint(1, 3);
    CHECK(ride_fences(r, &strip, 0).size() == 8);
}

TEST_CASE("a ride fenced round an outline leaves out the squares outside it")
{
    // An L of three squares in a two by two footprint: the square at x 1, z 1 is not inside.
    const save::Ride r = flat_ride(spot(0, 0, 0, 0));
    ObjectModel item = footprint(2, 2);
    item.fence_outline = { true, true, true, false };
    const std::set<Edge> fenced = edges(ride_fences(r, &item, 0));
    CHECK(fenced.size() == 8);
    CHECK(fenced.contains({ 1, 0, 1 })); // Facing the missing square
    CHECK(fenced.contains({ 0, 1, 2 }));
    for (const Edge& e : fenced)
        CHECK_FALSE((std::get<0>(e) == 1 && std::get<1>(e) == 1));
}
