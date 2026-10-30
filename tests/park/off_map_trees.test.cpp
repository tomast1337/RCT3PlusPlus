// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/off_map_trees.hpp"

#include "park/object_placement.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat(int size, float y)
    {
        save::TerrainGrid g;
        g.width = size;
        g.height = size;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(size * size));
        for (auto& c : g.cells)
            c.corners = { y, y, y, y };
        return g;
    }

    ObjectModel plant()
    {
        ModelPart part;
        part.vertices = { { { 0, 0, 0 }, { 0, 1, 0 }, { 0, 0 } }, { { 0, 1, 0 }, { 0, 1, 0 }, { 0, 1 } }, { { 1, 0, 0 }, { 0, 1, 0 }, { 1, 0 } } };
        part.indices = { 0, 1, 2 };
        Billboard board;
        board.texture = "leaves";
        ObjectModel m;
        m.sway = 0.1f;
        m.levels = { { 50, { part }, { } }, { 300, { }, board } };
        return m;
    }

    save::PlacedObject placed(std::uint64_t id, std::string symbol)
    {
        save::PlacedObject o;
        o.id = id;
        o.owner_ref = id;
        o.symbol = std::move(symbol);
        o.overlay = "Things/Things";
        return o;
    }
} // namespace

TEST_CASE("trees beyond the edge copy the park's plants near it, on the land round it")
{
    const save::TerrainGrid g = flat(20, 3); // 80 units across: trees reach 40 from their park tree
    ObjectModels models;
    models.emplace("Things/Things|Pine", plant());
    ObjectModel bench = plant();
    bench.sway = 0; // Still in the wind: not a plant
    models.emplace("Things/Things|Bench", bench);
    std::vector<save::PlacedObject> objects;
    for (std::int32_t row = 0; row < 20; ++row)
    {
        objects.push_back(placed(static_cast<std::uint64_t>(row + 1), "Pine"));
        objects.back().row = row; // Along the west edge
        objects.push_back(placed(static_cast<std::uint64_t>(row + 30), "Bench"));
        objects.back().col = 19; // Along the east edge
        objects.back().row = row;
    }
    objects.push_back(placed(100, "Bench"));
    CHECK(is_plant(objects[0], models));
    CHECK_FALSE(is_plant(objects.back(), models));

    const LandRings land = land_rings(g);
    random::RandomState s = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    const std::vector<save::PlacedObject> trees = off_map_trees(objects, 0, models, g, land, s);
    REQUIRE(trees.size() > 20);
    CHECK(trees.size() <= 20 * kOffMapTries);
    std::uint64_t id = 100;
    for (const save::PlacedObject& t : trees)
    {
        CHECK(t.symbol == "Pine");
        CHECK(t.id == ++id);
        CHECK(t.free_standing());
        CHECK(t.rotation == 0);
        const glm::vec3 foot = object_transform(t, Positioning { }, g)[3];
        CHECK_FALSE(g.height_at(foot.x, foot.z)); // Off the map
        CHECK(foot.y == doctest::Approx(3 - kTreeSink));
        CHECK(foot.x < g.world_x(0) + 4 + 40 + 4); // By the west edge, where the pines are
    }

    // The same seed, the same trees; another seed, others.
    random::RandomState again = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    const auto same = off_map_trees(objects, 0, models, g, land, again);
    REQUIRE(same.size() == trees.size());
    CHECK(same.front().col == trees.front().col);
    CHECK(same.back().row == trees.back().row);
    random::RandomState other = random::stream(random::Seed { 2 }, random::RandomStream::Scenery);
    const auto others = off_map_trees(objects, 0, models, g, land, other);
    CHECK((others.size() != trees.size() || others.front().col != trees.front().col || others.front().row != trees.front().row));
}

TEST_CASE("a plant far from the edge seeds no trees beyond it")
{
    const save::TerrainGrid g = flat(40, 3); // 160 across: trees reach 80, the middle is 80 from every edge
    ObjectModels models;
    models.emplace("Things/Things|Pine", plant());
    std::vector<save::PlacedObject> objects { placed(1, "Pine") };
    objects[0].col = 20;
    objects[0].row = 20;
    random::RandomState s = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    CHECK(off_map_trees(objects, 0, models, g, land_rings(g), s).empty());
}

TEST_CASE("a park without plants has no trees beyond it")
{
    random::RandomState s = random::seeded(random::Seed { 1 });
    const save::TerrainGrid g = flat(4, 0);
    CHECK(off_map_trees({ }, 0, { }, g, land_rings(g), s).empty());
}

TEST_CASE("planting a tree seeds trees beyond the edge round it alone")
{
    const save::TerrainGrid g = flat(20, 3);
    ObjectModels models;
    models.emplace("Things/Things|Pine", plant());
    models.emplace("Things/Things|Palm", plant());
    std::vector<save::PlacedObject> objects;
    for (std::uint64_t i = 1; i <= 20; ++i)
        objects.push_back(placed(i, "Pine"));
    const LandRings land = land_rings(g);
    random::RandomState s = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    const auto first = off_map_trees(objects, 0, models, g, land, s);
    REQUIRE_FALSE(first.empty());
    objects.insert(objects.end(), first.begin(), first.end());
    objects.push_back(placed(5, "Palm")); // In the corner, by two edges
    std::vector<save::PlacedObject> more;
    for (int tries = 0; tries < 20 && more.empty(); ++tries)
        more = off_map_trees(objects, objects.size() - 1, models, g, land, s);
    REQUIRE_FALSE(more.empty());
    for (const save::PlacedObject& t : more)
        CHECK(t.symbol == "Palm");
    CHECK(more.front().id > first.back().id);
}

TEST_CASE("a park's own list of trees beyond the edge copies the plants it names, where it names")
{
    const save::TerrainGrid g = flat(20, 3);
    ObjectModels models;
    models.emplace("Things/Things|Pine", plant());
    ObjectModel bench = plant();
    bench.sway = 0;
    models.emplace("Things/Things|Bench", bench);
    std::vector<save::PlacedObject> objects { placed(1, "Pine"), placed(2, "Bench") };
    objects[0].rotation = 1;
    objects[0].colours = { 4, 5, 6 };
    const std::vector<save::SkirtTree> listed {
        { .scenery_ref = 1, .position = { -10, 50, 20 } }, { .scenery_ref = 2, .position = { -10, 0, 30 } }, // Not a plant
        { .scenery_ref = 9, .position = { -10, 0, 40 } },                                                    // No such item
        { .scenery_ref = 1, .position = { 40, 0, 40 } },                                                     // On the map
        { .scenery_ref = 1, .position = { 100, 0, 60 } },
    };
    const auto trees = listed_off_map_trees(listed, objects, models, g, land_rings(g));
    REQUIRE(trees.size() == 2);
    CHECK(trees[0].id == 3);
    CHECK(trees[1].id == 4);
    for (const save::PlacedObject& t : trees)
    {
        CHECK(t.symbol == "Pine");
        CHECK(t.colours == objects[0].colours);
        CHECK(t.rotation == 0);
        CHECK(t.free_standing());
    }
    const glm::vec3 first = object_transform(trees[0], Positioning { }, g)[3];
    CHECK(std::abs(first.x - -10) <= 2);
    CHECK(std::abs(first.z - 20) <= 2);
    CHECK(first.y == doctest::Approx(3 - kTreeSink)); // On the land, whatever height the list gives
    const glm::vec3 second = object_transform(trees[1], Positioning { }, g)[3];
    CHECK(std::abs(second.x - 100) <= 2);
}
