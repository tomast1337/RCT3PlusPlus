// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_holds.hpp"

#include "park/item_category.hpp"

#include <array>
#include <doctest/doctest.h>
#include <utility>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat_grid(int width, int height)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        return g;
    }

    /// One item of `category` on tile (1, 1) at height `base`, its one square solid from level 0 to 2.
    std::pair<std::vector<save::PlacedObject>, ObjectModels> one_item(ItemCategory category, std::int32_t base)
    {
        ObjectModel model;
        model.category = std::to_underlying(category);
        model.solid = { std::array<std::int32_t, 2> { 0, 2 } };
        model.levels = { { 100, { }, { } } };
        save::PlacedObject o;
        o.id = 1;
        o.owner_ref = 1;
        o.symbol = "Thing";
        o.col = 1;
        o.row = 1;
        o.base_height = base;
        ObjectModels models;
        models.emplace(model_key(o), model);
        return { { o }, models };
    }

    constexpr std::array<float, 4> kLevel { 0, 0, 0, 0 };
    constexpr std::array<float, 4> kUp { 3, 3, 3, 3 };
    constexpr std::array<float, 4> kDown { -3, -3, -3, -3 };
} // namespace

TEST_CASE("a tree lets the ground move and is seated on it after")
{
    save::TerrainGrid g = flat_grid(3, 3);
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
    const save::TerrainGrid g = flat_grid(3, 3);
    auto [objects, models] = one_item(ItemCategory::Stall, 0);
    models.begin()->second.holds_ground = true;
    const GroundHolds holds = ground_holds(objects, models, { }, g);
    CHECK(refuses(holds, { 1, 1 }, kLevel, kDown));
    CHECK_FALSE(refuses(holds, { 0, 1 }, kLevel, kDown));
    CHECK(holds.followers.empty());
}

TEST_CASE("an item at a height of its own lets the ground rise only up to its base")
{
    const save::TerrainGrid g = flat_grid(3, 3);
    auto [objects, models] = one_item(ItemCategory::SmallScenery, 4);
    objects[0].force_absolute_height = true;
    const GroundHolds holds = ground_holds(objects, models, { }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    CHECK(refuses(holds, { 1, 1 }, kLevel, { 5, 0, 0, 0 }));
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kDown));
}

TEST_CASE("a raised path lets the ground sink under it but not rise into it")
{
    const save::TerrainGrid g = flat_grid(3, 3);
    save::PathTile bridge { .col = 1, .row = 1, .elevation = save::PathElevation { .quantised_height = 4 } };
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
    const save::TerrainGrid g = flat_grid(3, 3);
    const save::PathTile path { .kind = save::PathKind::Ground, .col = 1, .row = 1 };
    const GroundHolds holds = ground_holds({ }, { }, std::vector { path }, g);
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, kUp));
    CHECK_FALSE(refuses(holds, { 1, 1 }, kLevel, { 1, 0, 0, 0 }));
    CHECK(refuses(holds, { 1, 1 }, kLevel, { 3, 0, 0, 0 }));
    CHECK_FALSE(refuses(holds, { 1, 1 }, { 3, 0, 0, 0 }, { 2, 0, 0, 0 })); // Flatter than it was
}
