// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ride_placement.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{

    // Eight by eight, all owned, the ground at height 2 but for a hill on the tile at 6,6.
    save::TerrainGrid owned_land()
    {
        save::TerrainGrid g;
        g.width = 8;
        g.height = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(64);
        for (save::TerrainCell& c : g.cells)
        {
            c.flags = std::to_underlying(save::TileFlag::Owned);
            c.corners = { 2, 2, 2, 2 };
        }
        g.at(6, 6).corners = { 5, 6, 6, 7 };
        return g;
    }

} // namespace

TEST_CASE("a ride's footprint is centred on the tile under the pointer, turned, at the ground's height")
{
    const save::TerrainGrid grid = owned_land();
    const RideSite flat = ride_site({ 4, 4 }, { 3, 2 }, 0, grid);
    CHECK(flat.col == 3);
    CHECK(flat.row == 4);
    CHECK(flat.span == std::array<std::int32_t, 2> { 3, 2 });
    CHECK(flat.height == 2);
    const RideSite turned = ride_site({ 4, 4 }, { 3, 2 }, 5, grid);
    CHECK(turned.turns == 1);
    CHECK(turned.span == std::array<std::int32_t, 2> { 2, 3 });
    CHECK(turned.col == 4);
    CHECK(turned.row == 3);
    CHECK(ride_site({ 6, 6 }, { 1, 1 }, 0, grid).height == 6);
}

TEST_CASE("a ride stands at the mean height of the ground under its whole footprint, halves rounded up")
{
    save::TerrainGrid grid = owned_land();
    CHECK(ride_site({ 6, 6 }, { 3, 3 }, 0, grid).height == 2); // Pointed at the hill, among flat tiles
    grid.at(1, 2).corners = { 3, 3, 3, 3 };
    CHECK(ride_site({ 1, 1 }, { 1, 2 }, 0, grid).height == 3);
}

TEST_CASE("a ride raised over the ground may not have the ground of any tile rise over its base")
{
    const save::TerrainGrid grid = owned_land();
    RideSite site = ride_site({ 6, 6 }, { 3, 3 }, 0, grid);
    site.height = 3;
    CHECK(ground_problem(site, grid) == SiteProblem::IntoGround);
    site.height = 7;
    CHECK(ground_problem(site, grid) == SiteProblem::None);
    site = ride_site({ 2, 2 }, { 3, 3 }, 0, grid);
    CHECK(ground_problem(site, grid) == SiteProblem::None); // On the ground is not in it
}

TEST_CASE("a ride needs owned land clear of paths and of solid things above its base")
{
    save::TerrainGrid grid = owned_land();
    const RideSite site = ride_site({ 2, 2 }, { 3, 3 }, 0, grid);
    CHECK(site_problem(site, grid, { }, { }) == SiteProblem::None);
    CHECK(site_problem(ride_site({ 0, 0 }, { 3, 3 }, 0, grid), grid, { }, { }) == SiteProblem::NotOwned);
    grid.at(3, 1).flags |= std::to_underlying(save::TileFlag::RightsOnly);
    CHECK(site_problem(site, grid, { }, { }) == SiteProblem::NotOwned);
    grid = owned_land();
    save::PathTile path;
    path.col = 1;
    path.row = 3;
    CHECK(site_problem(site, grid, std::span(&path, 1), { }) == SiteProblem::Path);
    SolidTiles solids;
    solids.tiles[3 << 16 | 2] = { SolidSpan { 0, 1 } }; // Under the base: no matter
    CHECK(site_problem(site, grid, { }, solids) == SiteProblem::None);
    solids.tiles[3 << 16 | 2].push_back({ 1, 4 });
    CHECK(site_problem(site, grid, { }, solids) == SiteProblem::Blocked);
}

TEST_CASE("placing a ride levels its footprint, and the tiles round it slope to it")
{
    save::TerrainGrid grid = owned_land();
    RideSite site = ride_site({ 6, 6 }, { 1, 1 }, 0, grid);
    site.height = 4;
    level_ground(grid, site);
    CHECK(grid.at(6, 6).corners == std::array<float, 4> { 4, 4, 4, 4 });
    // The tile to its left shares its two left corners; its own far ones stay.
    CHECK(grid.at(5, 6).corners == std::array<float, 4> { 2, 4, 2, 4 });
    // The tile diagonally up and left shares one corner.
    CHECK(grid.at(5, 5).corners == std::array<float, 4> { 2, 2, 2, 4 });
    CHECK(grid.at(4, 6).corners == std::array<float, 4> { 2, 2, 2, 2 });
}

TEST_CASE("shaping flattens a sloped tile to its top or bottom, then moves a flat one a unit")
{
    const save::TerrainGrid grid = owned_land();
    CHECK(shaped_height(grid.at(1, 1), true) == 3);
    CHECK(shaped_height(grid.at(1, 1), false) == 1);
    CHECK(shaped_height(grid.at(6, 6), true) == 7);
    CHECK(shaped_height(grid.at(6, 6), false) == 5);
}

TEST_CASE("an entrance goes across the footprint's rim nearest the pointer, facing the ride")
{
    const save::TerrainGrid grid = owned_land();
    const RideSite site = ride_site({ 3, 3 }, { 3, 3 }, 0, grid); // Tiles 2..4 both ways
    CHECK(door_spot(site, { 0, 3 }) == DoorSpot { 1, 3, 2 });
    CHECK(door_spot(site, { 3, 7 }) == DoorSpot { 3, 5, 3 });
    CHECK(door_spot(site, { 6, 2 }) == DoorSpot { 5, 2, 0 });
    CHECK(door_spot(site, { 4, 0 }) == DoorSpot { 4, 1, 1 });
    // Inside, the nearest side; off a corner, never the corner tile.
    CHECK(door_spot(site, { 4, 3 }) == DoorSpot { 5, 3, 0 });
    const DoorSpot corner = door_spot(site, { 0, 0 });
    CHECK(((corner.col == 1 && corner.row == 2) || (corner.col == 2 && corner.row == 1)));
    // Not on the other door's tile.
    const DoorSpot entrance = door_spot(site, { 0, 3 });
    CHECK(door_problem(site, entrance, std::nullopt, grid, { }, { }) == SiteProblem::None);
    CHECK(door_problem(site, entrance, entrance, grid, { }, { }) == SiteProblem::Blocked);
}

TEST_CASE("a tracked ride's entrance stands beside its station, across the track, facing it")
{
    save::TrackPiece along_x;
    along_x.tile_x = 4;
    along_x.tile_z = 4;
    along_x.height = 3;
    save::TrackPiece along_z = along_x;
    along_z.tile_x = 10;
    along_z.direction = 1;
    const std::vector stations { along_x, along_z };
    CHECK_FALSE(station_door_spot({ }, { 0, 0 }));
    CHECK(station_door_spot(stations, { 4, 7 }) == DoorSpot { 4, 5, 3, 3 });
    CHECK(station_door_spot(stations, { 3, 1 }) == DoorSpot { 4, 3, 1, 3 });
    // Never at the station's ends, where the track runs on.
    CHECK(station_door_spot(stations, { 1, 4 }) == DoorSpot { 4, 3, 1, 3 });
    CHECK(station_door_spot(stations, { 12, 4 }) == DoorSpot { 11, 4, 0, 3 });
    CHECK(station_door_spot(stations, { 9, 4 }) == DoorSpot { 9, 4, 2, 3 });
}

TEST_CASE("a station piece's platform is capped where the station ends and has stairs where its doors are")
{
    save::TrackPiece along_x;
    along_x.tile_x = 4;
    along_x.tile_z = 4;
    along_x.height = 3;
    CHECK(platform_symbol("deck", along_x, false, true, { }) == "deckstart");
    CHECK(platform_symbol("deck", along_x, true, false, { }) == "deckend");
    CHECK(platform_symbol("deck", along_x, false, false, { }) == "deckstartEnd");
    // Left of the way it runs is +z, right is -z; a door at another height is not beside it.
    const std::vector<DoorSpot> left { { 4, 5, 3, 3 } }, right { { 4, 3, 1, 3 } }, both { { 4, 5, 3, 3 }, { 4, 3, 1, 3 } }, above { { 4, 5, 3, 4 } };
    CHECK(platform_symbol("deck", along_x, true, true, left) == "deckmiddle1stairLeft");
    CHECK(platform_symbol("deck", along_x, true, false, right) == "deckend1stairRight");
    CHECK(platform_symbol("deck", along_x, false, false, both) == "deckstartEnd2stair");
    CHECK(platform_symbol("deck", along_x, true, true, above) == "deckmiddle");
    // Turned a quarter, its left is +x.
    save::TrackPiece along_z = along_x;
    along_z.direction = 1;
    CHECK(platform_symbol("deck", along_z, true, true, std::vector<DoorSpot> { { 5, 4, 0, 3 } }) == "deckmiddle1stairLeft");
}

TEST_CASE("a ride that stands only on water needs water under every tile, and others no water over their base")
{
    // A lake at height 5 whose triangles fill the tiles 2..4 along 2..3.
    save::WaterBody lake;
    lake.level = 5;
    for (std::int32_t r = 2; r <= 3; ++r)
        for (std::int32_t c = 2; c <= 4; ++c)
            for (std::uint8_t t = 0; t < 2; ++t)
                lake.tris.push_back({ .col = static_cast<std::uint8_t>(c), .row = static_cast<std::uint8_t>(r), .triangle = t, .mask = 7 });
    const std::vector bodies { lake };
    CHECK(water_problem(RideSite { 2, 2, 0, 2, { 3, 2 } }, true, bodies) == SiteProblem::None);
    CHECK(water_problem(RideSite { 2, 2, 0, 2, { 3, 3 } }, true, bodies) == SiteProblem::OffWater);
    CHECK(water_problem(RideSite { 2, 2, 0, 2, { 1, 1 } }, true, { }) == SiteProblem::OffWater);
    CHECK(water_problem(RideSite { 0, 0, 0, 2, { 2, 2 } }, false, bodies) == SiteProblem::None);
    CHECK(water_problem(RideSite { 1, 1, 0, 2, { 2, 2 } }, false, bodies) == SiteProblem::UnderWater);
    // Raised over the water, it stands clear of it, as it does at the surface itself: saved parks keep land rides
    // standing level with a lake over deep water.
    CHECK(water_problem(RideSite { 1, 1, 0, 6, { 2, 2 } }, false, bodies) == SiteProblem::None);
    CHECK(water_problem(RideSite { 1, 1, 0, 5, { 2, 2 } }, false, bodies) == SiteProblem::None);
}

TEST_CASE("a new ride is named after its kind with the lowest number its kind has free")
{
    std::vector<save::Ride> rides(3);
    rides[0].base.symbol = "Teacups:anr";
    rides[0].base.name = "%ME% 1";
    rides[1].base.symbol = "Teacups:anr";
    rides[1].base.name = "%ME% 3";
    rides[2].base.symbol = "Carousel:anr";
    rides[2].base.name = "%ME% 2";
    CHECK(numbered_name(rides, "Teacups:anr") == "%ME% 2");
    CHECK(numbered_name(rides, "Carousel:anr") == "%ME% 1");
    CHECK(numbered_name(rides, "Swings:anr") == "%ME% 1");
}

TEST_CASE("a new stall is numbered among the stalls of its kind")
{
    std::vector<save::Stall> stalls(2);
    stalls[0].base.symbol = "BurgerStall:sta";
    stalls[0].base.name = "%ME% 1";
    stalls[1].base.symbol = "DrinksStall:sta";
    stalls[1].base.name = "%ME% 2";
    CHECK(numbered_name(stalls, "BurgerStall:sta") == "%ME% 2");
    CHECK(numbered_name(stalls, "DrinksStall:sta") == "%ME% 1");
}

TEST_CASE("a show ride knows the tricks named for its kind, its idle first, each as long as its animation")
{
    const std::vector<std::string> names { "SealShowAnimTrick2", "SealShowAnimIdle", "SealShowAnimTrick1", "BearShowJuggle", "BearShowIdleLoop", "BigSealShowAnimTrick1" };
    // A kind whose animations are named apart from its tricks, and the loop for its idle.
    const std::vector<std::string> seal { "Trick1", "loop", "PlainSealShowTrick02" };
    const std::vector<float> seal_seconds { 10, 4, 20 };
    const auto tricks = show_tricks("PlainSealShow", names, seal, seal_seconds);
    REQUIRE(tricks.size() == 3);
    CHECK(tricks[0].name == "SealShowAnimIdle");
    CHECK(tricks[0].seconds == 4);
    CHECK(tricks[1].name == "SealShowAnimTrick1");
    CHECK(tricks[1].seconds == 10);
    CHECK(tricks[2].name == "SealShowAnimTrick2");
    CHECK(tricks[2].seconds == 20);
    CHECK((tricks[2].available && tricks[2].invented && !tricks[2].hidden));
    // Another kind's tricks are left out, and one without an animation lasts nothing.
    const std::vector<std::string> bear { "BearShowIdleLoop" };
    const std::vector<float> bear_seconds { 3 };
    const auto bears = show_tricks("BearShow", names, bear, bear_seconds);
    REQUIRE(bears.size() == 2);
    CHECK(bears[0].name == "BearShowIdleLoop");
    CHECK(bears[0].seconds == 3);
    CHECK(bears[1].seconds == 0);
    CHECK(show_tricks("Carousel", names, bear, bear_seconds).empty());
}

TEST_CASE("a synchronised stall gives its menu to the other stalls of its kind")
{
    std::vector<save::Stall> stalls(3);
    for (save::Stall& s : stalls)
    {
        s.base.symbol = "BurgerStall:sta";
        s.base.overlay_path = "Style/Plain";
        s.extras = { save::StallExtra { .kind_symbol = "Ketchup:ced", .level = 0 } };
    }
    stalls[2].base.symbol = "DrinksStall:sta";
    stalls[0].items = { save::StallItem { .kind_symbol = "Burger:cid", .price = 175 }, save::StallItem { .kind_symbol = "MedBurger:cid", .price = 260 } };
    stalls[0].extras[0].level = 3;
    stalls[1].items = { save::StallItem { .kind_symbol = "MedBurger:cid", .price = 250, .total_sold = 12 }, save::StallItem { .kind_symbol = "XLBurger:cid", .price = 450 } };
    park::copy_menu(stalls, 0);
    REQUIRE(stalls[1].items.size() == 2);
    CHECK(stalls[1].items[0].kind_symbol == "Burger:cid");
    CHECK(stalls[1].items[0].price == 175);
    CHECK(stalls[1].items[1].price == 260);
    CHECK(stalls[1].items[1].total_sold == 12);
    CHECK(stalls[1].extras[0].level == 3);
    CHECK(stalls[2].items.empty());
    CHECK(stalls[2].extras[0].level == 0);
}

TEST_CASE("an item moved on a stall's menu takes its new place, the others closing up behind it")
{
    save::Stall stall;
    for (const char* kind : { "A:cid", "B:cid", "C:cid", "D:cid" })
        stall.items.push_back({ .kind_symbol = kind });
    const auto order = [&] {
        std::string out;
        for (const save::StallItem& i : stall.items)
            out += i.kind_symbol.front();
        return out;
    };
    park::move_item(stall, 0, 2);
    CHECK(order() == "BCAD");
    park::move_item(stall, 3, 0);
    CHECK(order() == "DBCA");
    park::move_item(stall, 1, 9);
    CHECK(order() == "DCAB");
    park::move_item(stall, 9, 0);
    CHECK(order() == "DCAB");
}

TEST_CASE("a ride on ground left as it is stands clear of its footprint's highest corner")
{
    const save::TerrainGrid grid = owned_land();
    const RideSite site = ride_site({ 6, 6 }, { 3, 3 }, 0, grid);
    CHECK(clear_height(site, grid) == 7);
    CHECK(clear_height(ride_site({ 2, 2 }, { 3, 3 }, 0, grid), grid) == 2);
}

TEST_CASE("levelling sharply sets the footprint's own corners; smoothly, also the corners round it on its edge")
{
    save::TerrainGrid grid = owned_land();
    for (save::TerrainCell& c : grid.cells)
        c.corners = { 4, 4, 4, 4 };
    RideSite site = ride_site({ 3, 3 }, { 1, 1 }, 0, grid);
    CHECK(levelled_ground(site, Flatten::Sharp, grid).footprint.empty()); // Already level
    site.height = 2;
    CHECK(levelled_ground(site, Flatten::Off, grid).footprint.empty());
    const LevelledGround sharp = levelled_ground(site, Flatten::Sharp, grid);
    REQUIRE(sharp.footprint.size() == 1);
    CHECK(sharp.footprint[0].corners == std::array { 2.0f, 2.0f, 2.0f, 2.0f });
    CHECK(sharp.rim.empty());
    const LevelledGround smooth = levelled_ground(site, Flatten::Smooth, grid);
    REQUIRE(smooth.rim.size() == 8);
    for (const GroundCut& cut : smooth.rim)
    {
        if (cut.tile == save::Tile { 4, 3 }) // East of it: its west corners meet the footprint
            CHECK(cut.corners == std::array { 2.0f, 4.0f, 2.0f, 4.0f });
        if (cut.tile == save::Tile { 2, 2 }) // North-west: only the corner it shares
            CHECK(cut.corners == std::array { 4.0f, 4.0f, 4.0f, 2.0f });
    }
}
