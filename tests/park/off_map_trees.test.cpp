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

TEST_CASE("trees beyond the edge are of the park's plant kinds, on the land round it")
{
    const save::TerrainGrid g = flat(20, 3);
    ObjectModels models;
    models.emplace("Things/Things|Pine", plant());
    ObjectModel bench = plant();
    bench.sway = 0; // Still in the wind: not a plant
    models.emplace("Things/Things|Bench", bench);
    std::vector<save::PlacedObject> objects;
    for (std::uint64_t i = 1; i <= 40; ++i)
        objects.push_back(placed(i, "Pine"));
    objects.push_back(placed(100, "Bench"));
    CHECK(is_plant(objects[0], models));
    CHECK_FALSE(is_plant(objects.back(), models));

    const LandRings land = land_rings(g);
    random::RandomState s = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    const std::vector<save::PlacedObject> trees = off_map_trees(objects, models, g, land, s);
    REQUIRE_FALSE(trees.empty());
    std::uint64_t id = 100;
    for (const save::PlacedObject& t : trees)
    {
        CHECK(t.symbol == "Pine");
        CHECK(t.id == ++id);
        CHECK(t.free_standing());
        const glm::vec3 foot = object_transform(t, Positioning { }, g)[3];
        CHECK_FALSE(g.height_at(foot.x, foot.z)); // Off the map
        CHECK(foot.y == doctest::Approx(3 - kTreeSink));
    }

    // The same seed, the same trees; another seed, others.
    random::RandomState again = random::stream(random::Seed { 1 }, random::RandomStream::Scenery);
    const auto same = off_map_trees(objects, models, g, land, again);
    REQUIRE(same.size() == trees.size());
    CHECK(same.front().col == trees.front().col);
    CHECK(same.back().row == trees.back().row);
    random::RandomState other = random::stream(random::Seed { 2 }, random::RandomStream::Scenery);
    const auto others = off_map_trees(objects, models, g, land, other);
    CHECK((others.size() != trees.size() || others.front().col != trees.front().col || others.front().row != trees.front().row));
}

TEST_CASE("a park without plants has no trees beyond it")
{
    random::RandomState s = random::seeded(random::Seed { 1 });
    const save::TerrainGrid g = flat(4, 0);
    CHECK(off_map_trees({ }, { }, g, land_rings(g), s).empty());
}
