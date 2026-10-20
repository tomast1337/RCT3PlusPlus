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

    /// One item of `category` standing on tile (2, 1), filling levels 0 to 3 of its one square.
    std::pair<std::vector<save::PlacedObject>, ObjectModels> one_item(ItemCategory category)
    {
        ObjectModel model;
        model.category = std::to_underlying(category);
        model.ground_height = false;
        model.solid = { std::array<std::int32_t, 2> { 0, 3 } };
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
    CHECK(plan_drag(std::vector<PathSpot> { { 1, 1 }, { 2, 1 }, { 3, 1 } }, g, { }, solids).refused == 1);

    const auto [bench, bench_models] = one_item(ItemCategory::Bench);
    CHECK(solid_tiles(bench, bench_models, g).tiles.empty());
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
    CHECK(route->back().piece.spot == PathSpot { 4, 1 });
    CHECK(std::ranges::none_of(*route, [](const RouteStep& s) { return s.piece.spot == PathSpot { 2, 1 } && s.piece.end < 4; }));
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
