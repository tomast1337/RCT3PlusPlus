// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/map_shapes.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid flat()
    {
        save::TerrainGrid g;
        g.width = 4;
        g.height = 4;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(16);
        return g;
    }

    save::PlacedObject object(std::uint64_t id, int col, int row)
    {
        save::PlacedObject o;
        o.id = id;
        o.symbol = "Unknown";
        o.overlay = "Nowhere/Nothing";
        o.col = col;
        o.row = row;
        o.base_height = 0;
        return o;
    }
} // namespace

TEST_CASE("objects without a model cover their tile, in the layer they are given")
{
    const save::TerrainGrid g = flat();
    std::vector<save::PlacedObject> objects { object(1, 2, 3), object(2, 0, 0), object(3, 1, 1), object(4, 9, 9) };
    objects[1].hidden = true;
    const auto layer_of = [](const save::PlacedObject& o) -> std::optional<MapLayer> {
        if (o.id == 3)
            return std::nullopt;
        return o.id == 1 ? MapLayer::Toilets : MapLayer::Scenery;
    };
    const std::vector<MapShape> shapes = map_shapes(objects, { }, g, { }, layer_of);
    // The hidden one, the one left off and the one off the map are not drawn.
    REQUIRE(shapes.size() == 1);
    CHECK(shapes[0].layer == MapLayer::Toilets);
    CHECK(shapes[0].corners[0] == glm::vec2(8, 12));
    CHECK(shapes[0].corners[2] == glm::vec2(12, 16));
}
