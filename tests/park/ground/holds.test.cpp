// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/holds.hpp"

#include "park/item_category.hpp"

#include <array>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{
    save::model::TerrainGrid flat_grid(int width, int height)
    {
        save::model::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        return g;
    }

    /// One item of `category` on tile (1, 1) at height `base`, its one square solid from level 0 to 2.
    std::pair<std::vector<save::model::PlacedObject>, object::ObjectModels> one_item(ItemCategory category, std::int32_t base)
    {
        object::ObjectModel model;
        model.category = std::to_underlying(category);
        model.solid = { std::array<std::int32_t, 2> { 0, 2 } };
        model.levels = { { 100, { }, { } } };
        save::model::PlacedObject o;
        o.id = 1;
        o.owner_ref = 1;
        o.symbol = "Thing";
        o.col = 1;
        o.row = 1;
        o.base_height = base;
        object::ObjectModels models;
        models.emplace(object::model_key(o), model);
        return { { o }, models };
    }

    constexpr std::array<float, 4> kLevel { 0, 0, 0, 0 };
    constexpr std::array<float, 4> kUp { 3, 3, 3, 3 };
    constexpr std::array<float, 4> kDown { -3, -3, -3, -3 };
} // namespace

TEST_CASE("a tree lets the ground move and is seated on it after")
{
    save::model::TerrainGrid g = flat_grid(3, 3);
    auto [objects, models] = one_item(ItemCategory::Tree, 0);
    const GroundHolds holds = ground_holds(objects, models, { }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    g.at(1, 1).corners = { 2.4f, 2.4f, 2.4f, 2.4f };
    CHECK_FALSE(seat_followers(objects, holds, models, g, { 0, 0 }, { 0, 0 })); // Not under it
    CHECK(seat_followers(objects, holds, models, g, { 1, 1 }, { 1, 1 }));
    CHECK(objects[0].base_height == 3); // Rounded up onto the ground
}

TEST_CASE("an item that holds the ground keeps its tile still")
{
    const save::model::TerrainGrid g = flat_grid(3, 3);
    auto [objects, models] = one_item(ItemCategory::Stall, 0);
    models.begin()->second.holds_ground = true;
    const GroundHolds holds = ground_holds(objects, models, { }, g);
    CHECK(refuses(holds, { 1, 1 }, kLevel, kDown));
    CHECK_FALSE(refuses(holds, { 0, 1 }, kLevel, kDown));
    CHECK(holds.followers.empty());
}

TEST_CASE("an item at a height of its own lets the ground rise only up to its base")
{
    const save::model::TerrainGrid g = flat_grid(3, 3);
    auto [objects, models] = one_item(ItemCategory::SmallScenery, 4);
    objects[0].force_absolute_height = true;
    const GroundHolds holds = ground_holds(objects, models, { }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    CHECK(refuses(holds, { 1, 1 }, kLevel, { 5, 0, 0, 0 }));
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kDown));
}

TEST_CASE("a raised path lets the ground sink under it but not rise into it")
{
    const save::model::TerrainGrid g = flat_grid(3, 3);
    save::model::PathTile bridge { .col = 1, .row = 1, .elevation = save::model::PathElevation { .quantised_height = 4 } };
    GroundHolds holds = ground_holds({ }, { }, std::vector { bridge }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kDown));
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    CHECK(refuses(holds, { 1, 1 }, kLevel, { 4, 4, 4, 4 }));

    bridge.elevation->underground = true;
    holds = ground_holds({ }, { }, std::vector { bridge }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, { 4, 4, 4, 4 }));
}

TEST_CASE("a path on the ground follows it while its tile stays near flat")
{
    const save::model::TerrainGrid g = flat_grid(3, 3);
    const save::model::PathTile path { .kind = save::model::PathKind::Ground, .col = 1, .row = 1 };
    const GroundHolds holds = ground_holds({ }, { }, std::vector { path }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, { 1, 0, 0, 0 }));
    CHECK(refuses(holds, { 1, 1 }, kLevel, { 3, 0, 0, 0 }));
    CHECK_FALSE(refuses(holds, { 1, 1 }, { 3, 0, 0, 0 }, { 2, 0, 0, 0 })); // Flatter than it was
}

TEST_CASE("limit_ground: corners stop at the lowest and highest ground, and ground past them only comes back")
{
    const auto was = [] {
        auto g = flat_grid(2, 1);
        g.at(1, 0).corners = { 160, -80, 0, 0 };
        return g;
    }();
    auto grid = was;
    grid.at(0, 0).corners = { 200, -100, 20, -20 };
    grid.at(1, 0).corners = { 170, -90, 155, -78 };
    CHECK(limit_ground(grid, was) == doctest::Approx(50 + 25 + 10 + 10 + 5 + 3));
    CHECK(grid.at(0, 0).corners == std::array<float, 4> { kHighestGround, kLowestGround, 20, -20 });
    CHECK(grid.at(1, 0).corners == std::array<float, 4> { 160, -80, kHighestGround, kLowestGround });

    grid.at(1, 0).corners = { 152, -76, 0, 0 }; // Past the limits still, but nearer than it was
    CHECK(limit_ground(grid, was) == 0);
    CHECK(grid.at(1, 0).corners == std::array<float, 4> { 152, -76, 0, 0 });
}
