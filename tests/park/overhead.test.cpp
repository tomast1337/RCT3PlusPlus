// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/overhead.hpp"

#include "park/item_category.hpp"

#include <doctest/doctest.h>
#include <string>
#include <utility>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 8;
        g.height = 8;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(64);
        return g;
    }

    /// A flat square two across at `height` above its origin, of `category`.
    ObjectModel slab(float height, ItemCategory category)
    {
        ModelPart p;
        p.vertices = { { { -1, height, -1 }, { 0, 1, 0 }, { 0, 0 } }, { { 1, height, -1 }, { 0, 1, 0 }, { 1, 0 } }, { { 1, height, 1 }, { 0, 1, 0 }, { 1, 1 } }, { { -1, height, 1 }, { 0, 1, 0 }, { 0, 1 } } };
        p.indices = { 0, 1, 2, 0, 2, 3 };
        ObjectModel m;
        m.category = std::to_underlying(category);
        m.levels = { { 100, { p }, { } } };
        return m;
    }

    save::PlacedObject object(std::uint64_t id, std::string symbol)
    {
        save::PlacedObject o;
        o.id = id;
        o.symbol = std::move(symbol);
        o.overlay = "Things/Things";
        o.base_height = 0;
        return o;
    }
} // namespace

TEST_CASE("a roof overhead keeps the rain off, trees only shade, and track and low things do neither")
{
    ObjectModels models;
    models.emplace("Things/Things|Roof", slab(4, ItemCategory::Roof));
    models.emplace("Things/Things|Low", slab(1, ItemCategory::Roof));
    models.emplace("Things/Things|Track", slab(4, ItemCategory::TrackPiece));
    ObjectModel tree = slab(6, ItemCategory::Tree);
    tree.levels.front().parts.front().vertices.front().position.y = 0; // A box from the ground up
    models.emplace("Things/Things|Tree", std::move(tree));
    const std::vector<save::PlacedObject> objects { object(1, "Roof"), object(2, "Tree"), object(3, "Track"), object(4, "Low") };
    const auto at = [](float x, float z) { return glm::translate(glm::mat4(1.0f), glm::vec3(x, 0, z)); };
    const Fitted fitted { { 1, at(2, 2) }, { 2, at(10, 10) }, { 3, at(18, 18) }, { 4, at(26, 26) } };
    const std::vector<glm::vec3> points { { 2, 0, 2 }, { 10, 0, 10 }, { 18, 0, 18 }, { 26, 0, 26 }, { 6, 0, 2 } };
    const std::vector<Overhead> over = overheads(points, objects, models, flat(), fitted);
    REQUIRE(over.size() == points.size());
    CHECK(over[0].roof);
    CHECK(over[0].shade);
    CHECK_FALSE(over[1].roof);
    CHECK(over[1].shade);
    for (std::size_t i = 2; i < points.size(); ++i)
    {
        CHECK_FALSE(over[i].roof);
        CHECK_FALSE(over[i].shade);
    }
}
