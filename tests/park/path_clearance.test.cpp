// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/path_clearance.hpp"

#include "park/item_category.hpp"
#include "park/object_placement.hpp"
#include "park/path_drag.hpp"
#include "park/path_route.hpp"

#include <algorithm>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid owned_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (save::TerrainCell& c : g.cells)
            c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
        return g;
    }

    /// One item of `category` standing on tile (2, 1), filling levels 0 to 3 of its one square, so up to 4.
    std::pair<std::vector<save::PlacedObject>, ObjectModels> one_item(ItemCategory category)
    {
        ObjectModel model;
        model.category = std::to_underlying(category);
        model.ground_height = false;
        model.solid = { std::array<std::int32_t, 2> { 0, 4 } };
        model.levels = { { 100, { }, { } } };
        save::PlacedObject o;
        o.id = 1;
        o.symbol = "Thing";
        o.col = 2;
        o.row = 1;
        o.base_height = 0;
        ObjectModels models;
        models.emplace(model_key(o), model);
        return { { o }, models };
    }
} // namespace

TEST_CASE("a stall blocks paths through it but not over it; a bench never does")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    const auto [objects, models] = one_item(ItemCategory::Stall);
    const SolidTiles solids = solid_tiles(objects, models, g);
    CHECK(blocked(solids, { 2, 1 }, 0, 0));
    CHECK(blocked(solids, { 2, 1 }, 3, 3));
    CHECK_FALSE(blocked(solids, { 2, 1 }, 4, 4)); // On its top
    CHECK_FALSE(blocked(solids, { 1, 1 }, 0, 0));

    FootpathBuilder b;
    start_at(b, { 2, 1 });
    CHECK(lay_problem(b, g, { }, solids) == LayProblem::Blocked);
    CHECK(lay_problem(b, g, { }) == LayProblem::None);
    CHECK(plan_drag(std::vector<save::Tile> { { 1, 1 }, { 2, 1 }, { 3, 1 } }, g, { }, solids).refused == 1);

    const auto [bench, bench_models] = one_item(ItemCategory::Bench);
    CHECK(solid_tiles(bench, bench_models, g).tiles.empty());
    // Nor does a lamp, of the lights' category, for standing against a path's side.
    auto [lamp, lamp_models] = one_item(ItemCategory::Stall);
    lamp_models.begin()->second.positioning = std::to_underlying(Positioning::PathSide);
    CHECK(solid_tiles(lamp, lamp_models, g).tiles.empty());
}

TEST_CASE("a route goes round a stall in its way")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    const auto [objects, models] = one_item(ItemCategory::Stall);
    const SolidTiles solids = solid_tiles(objects, models, g);
    FootpathBuilder b;
    start_at(b, { 0, 1 });
    b.heading = 1;
    const auto route = find_route(b, { 4, 1 }, g, { }, 1000, std::nullopt, solids);
    REQUIRE(route);
    CHECK(route->back().piece.spot == save::Tile { 4, 1 });
    CHECK(std::ranges::none_of(*route, [](const RouteStep& s) { return s.piece.spot == save::Tile { 2, 1 } && s.piece.end < 4; }));
}

TEST_CASE("items in different quarters or along different edges of a tile take different room of it")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    auto [objects, models] = one_item(ItemCategory::Stall);
    models.begin()->second.positioning = std::to_underlying(Positioning::Quarter);
    objects.push_back(objects[0]);
    objects[1].quadrant = 3;
    const SolidTiles solids = solid_tiles(objects, models, g);
    const auto& spans = solids.tiles.at(1 << 16 | 2);
    REQUIRE(spans.size() == 2);
    CHECK(spans[0].room == quarter_room(0));
    CHECK((spans[0].room & spans[1].room) == 0);
    CHECK(blocked(solids, { 2, 1 }, 0, 0)); // A path still needs the whole tile

    CHECK((edge_room(0) & edge_room(1)) == 0);
    CHECK((half_room(0) & half_room(2)) == 0); // Turned apart, two halves share a tile
    CHECK((half_room(1) & half_room(3)) == 0);
    CHECK(half_room(0) == (quarter_room(0) | quarter_room(2))); // Quarters on the -X side
    CHECK((edge_room(2) & kWholeTile) == 0);
}

TEST_CASE("an item clashes with one in the same room of a tile, not with one beside or on top of it")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    const auto [objects, models] = one_item(ItemCategory::TrackPiece);
    const SolidTiles standing = solid_tiles(objects, models, g);
    CHECK(clash(standing, standing, { }, g) == Clash::Solid);

    auto [beside, beside_models] = one_item(ItemCategory::TrackPiece);
    beside[0].col = 3;
    CHECK(clash(solid_tiles(beside, beside_models, g), standing, { }, g) == Clash::None);

    auto [above, above_models] = one_item(ItemCategory::TrackPiece);
    above[0].base_height = 4;
    CHECK(clash(solid_tiles(above, above_models, g), standing, { }, g) == Clash::None);
}

TEST_CASE("pieces of track filling other thirds of a tile pass each other, turned as they stand")
{
    const save::TerrainGrid g = owned_grid(5, 3);
    const auto piece = [&](std::uint32_t cells, std::int32_t rotation) {
        auto [objects, models] = one_item(ItemCategory::TrackPiece);
        SupportFootprint f;
        f.x_squares = 1;
        f.z_squares = 1;
        f.track = true;
        f.squares = { SupportSquare { .low = 0, .high = 4, .cells = cells } };
        models.begin()->second.supports = f;
        objects[0].rotation = rotation;
        return solid_tiles(objects, models, g);
    };
    const SolidTiles corner = piece(0b011'000'000, 0); // Two thirds along one side
    const std::uint32_t round = 0b111'011'001;         // Takes the same two, unless turned half round
    CHECK(clash(piece(round, 2), corner, { }, g) == Clash::None);
    CHECK(clash(piece(round, 0), corner, { }, g) == Clash::Solid);
}

TEST_CASE("a group of items is refused whole by its first item in the way, and placed whole when none is")
{
    const save::TerrainGrid g = owned_grid(6, 3);
    const auto [standing, models] = one_item(ItemCategory::Stall); // On tile (2, 1)
    const SolidTiles solids = solid_tiles(standing, models, g);
    std::vector<save::PlacedObject> group(3, standing.front());
    for (std::size_t i = 0; i < group.size(); ++i)
        group[i].col = static_cast<std::int32_t>(i);
    const auto refused = first_refused(group, models, solids, { }, g);
    REQUIRE(refused.has_value());
    CHECK(refused->item == 2);
    CHECK_FALSE(refused->unowned);
    group[2].col = 4;
    CHECK_FALSE(first_refused(group, models, solids, { }, g).has_value());
    group[2].base_height = 4; // On the stall's top
    group[2].col = 2;
    CHECK_FALSE(first_refused(group, models, solids, { }, g).has_value());
}

TEST_CASE("a group of items is refused whole by one off land the park owns; bought rights do for one above the ground")
{
    save::TerrainGrid g = owned_grid(6, 3);
    const auto [one, models] = one_item(ItemCategory::Stall);
    std::vector<save::PlacedObject> group(2, one.front());
    group[0].col = 0;
    group[1].col = 4;
    g.at(4, 1).flags = 0;
    const auto refused = first_refused(group, models, { }, { }, g);
    REQUIRE(refused.has_value());
    CHECK(refused->item == 1);
    CHECK(refused->unowned);
    group[1].col = 9; // Off the map
    CHECK(first_refused(group, models, { }, { }, g)->unowned);
    group[1].col = 4;
    g.at(4, 1).flags = static_cast<std::uint8_t>(save::TileFlag::Owned) | static_cast<std::uint8_t>(save::TileFlag::RightsOnly);
    CHECK(first_refused(group, models, { }, { }, g).has_value()); // On the ground
    group[1].base_height = 2;
    group[1].force_absolute_height = true;
    CHECK_FALSE(first_refused(group, models, { }, { }, g).has_value());
}

TEST_CASE("fences, enclosure fences included, never stand in anything's way")
{
    CHECK_FALSE(blocks_paths(std::to_underlying(ItemCategory::Fence)));
    CHECK_FALSE(blocks_paths(std::to_underlying(ItemCategory::EnclosureFence)));
    CHECK_FALSE(blocks_paths(std::to_underlying(ItemCategory::EnclosureView)));
    CHECK(blocks_paths(std::to_underlying(ItemCategory::Stall)));
}
