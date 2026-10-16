// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/object_placement.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

namespace
{
    save::TerrainGrid slope()
    {
        // Two by two tiles from (0, 0), rising one unit per tile along X.
        save::TerrainGrid g;
        g.width = 2;
        g.height = 2;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        for (int row = 0; row < 2; ++row)
        {
            for (int col = 0; col < 2; ++col)
            {
                save::TerrainCell cell;
                const auto c = static_cast<float>(col);
                cell.corners = { c, c + 1, c, c + 1 };
                g.cells.push_back(cell);
            }
        }
        return g;
    }

    save::PlacedObject at(int col, int row, int turns, std::optional<std::int32_t> height = 3)
    {
        save::PlacedObject o;
        o.id = 1;
        o.owner_ref = 1;
        o.col = col;
        o.row = row;
        o.rotation = turns;
        o.base_height = height;
        return o;
    }

    glm::vec3 place(const glm::mat4& m, glm::vec3 p)
    {
        return glm::vec3(m * glm::vec4(p, 1.0f));
    }
} // namespace

TEST_CASE("quarter turns go round clockwise from -X")
{
    CHECK(turned_x(0) == glm::vec2(-1, 0));
    CHECK(turned_x(1) == glm::vec2(0, 1));
    CHECK(turned_x(2) == glm::vec2(1, 0));
    CHECK(turned_x(3) == glm::vec2(0, -1));
    CHECK(turned_x(4) == turned_x(0));
    CHECK(turned_x(-1) == turned_x(3));
}

TEST_CASE("a tile item stands on the middle of its tile, on the ground")
{
    const save::TerrainGrid g = slope();
    const glm::mat4 m = object_transform(at(1, 0, 0), Positioning::Tile, g);
    const glm::vec3 p = place(m, { 0, 0, 0 });
    CHECK(p.x == doctest::Approx(6));
    CHECK(p.z == doctest::Approx(2));
    CHECK(p.y == doctest::Approx(1.5f)); // Ground height there, not the stored 3
    // Unturned, the model's +X points along -X; up stays up.
    const glm::vec3 q = place(m, { 1, 1, 0 });
    CHECK(q.x == doctest::Approx(5));
    CHECK(q.y == doctest::Approx(2.5f));
}

TEST_CASE("a larger item covers the tiles from its own onwards, its model round their middle")
{
    const save::TerrainGrid g = slope();
    // Three tiles along its X, two along its Z: across the park unturned, along it a quarter turn on.
    CHECK(footprint_middle(at(1, 0, 0), { 3, 2 }) == glm::vec2(2.5f, 1));
    CHECK(footprint_middle(at(1, 0, 1), { 3, 2 }) == glm::vec2(2, 1.5f));
    CHECK(footprint_middle(at(1, 0, 2), { 3, 2 }) == glm::vec2(2.5f, 1));
    const glm::vec3 p = place(object_transform(at(1, 0, 1), Positioning::Tile, g, { 3, 2 }), { });
    CHECK(p.x == doctest::Approx(8));
    CHECK(p.z == doctest::Approx(6));
}

TEST_CASE("an item raised above the ground or pinned keeps its stored height")
{
    const save::TerrainGrid g = slope();
    save::PlacedObject raised = at(0, 0, 0);
    raised.height_offset = 2;
    CHECK(place(object_transform(raised, Positioning::Tile, g), { }).y == doctest::Approx(3));
    save::PlacedObject pinned = at(0, 0, 0);
    pinned.force_absolute_height = true;
    CHECK(place(object_transform(pinned, Positioning::Tile, g), { }).y == doctest::Approx(3));
}

TEST_CASE("turns keep a model's shape")
{
    const save::TerrainGrid g = slope();
    for (int turns = 0; turns < 4; ++turns)
    {
        const glm::mat4 m = object_transform(at(0, 1, turns), Positioning::Tile, g);
        CHECK(glm::determinant(glm::mat3(m)) == doctest::Approx(1));
        const glm::vec3 x = place(m, { 1, 0, 0 }) - place(m, { 0, 0, 0 });
        CHECK(glm::vec2(x.x, x.z) == turned_x(turns));
    }
}

TEST_CASE("an edge item goes to the edge its turn faces")
{
    const save::TerrainGrid g = slope();
    const glm::vec3 west = place(object_transform(at(0, 1, 0), Positioning::Edge, g), { });
    CHECK(west.x == doctest::Approx(0));
    CHECK(west.z == doctest::Approx(6));
    const glm::vec3 south = place(object_transform(at(0, 1, 1), Positioning::Edge, g), { });
    CHECK(south.x == doctest::Approx(2));
    CHECK(south.z == doctest::Approx(8));
}

TEST_CASE("a quarter-tile item goes to its quarter")
{
    const save::TerrainGrid g = slope();
    save::PlacedObject o = at(0, 0, 0);
    o.quadrant = 3;
    const glm::vec3 p = place(object_transform(o, Positioning::Quarter, g), { });
    CHECK(p.x == doctest::Approx(3));
    CHECK(p.z == doctest::Approx(3));
}

TEST_CASE("an item is on the park's land when every tile it covers is owned")
{
    save::TerrainGrid g = slope();
    const auto own = [&](int col, int row, save::TileFlag f) { g.cells[static_cast<std::size_t>(row * g.width + col)].flags = static_cast<std::uint8_t>(f); };
    own(0, 0, save::TileFlag::Owned);
    own(1, 0, save::TileFlag::Owned);
    CHECK(on_owned_land(at(0, 0, 0), { 1, 1 }, g));
    CHECK(on_owned_land(at(0, 0, 0), { 2, 1 }, g));
    // Turned a quarter, the same item lies along the rows, onto land not owned.
    CHECK_FALSE(on_owned_land(at(0, 0, 1), { 2, 1 }, g));
    // Off the map.
    CHECK_FALSE(on_owned_land(at(1, 1, 0), { 2, 2 }, g));

    // Land bought only to build over takes things above the ground but not on it.
    g.cells[2].flags = static_cast<std::uint8_t>(save::TileFlag::Owned) | static_cast<std::uint8_t>(save::TileFlag::RightsOnly);
    save::PlacedObject raised = at(0, 1, 0);
    CHECK_FALSE(on_owned_land(raised, { 1, 1 }, g));
    raised.height_offset = 4;
    CHECK(on_owned_land(raised, { 1, 1 }, g));
}

TEST_CASE("an item that does not stand at the ground's height keeps its stored height on the ground")
{
    const save::TerrainGrid g = slope();
    const save::PlacedObject wall = at(1, 0, 0);
    CHECK(on_ground(wall));
    CHECK(place(object_transform(wall, Positioning::Tile, g, { 1, 1 }, false), { }).y == doctest::Approx(3));
}
