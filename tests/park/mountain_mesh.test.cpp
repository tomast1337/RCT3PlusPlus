// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/mountain_mesh.hpp"

#include <cmath>
#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::park;

TEST_CASE("the stored background number picks a theme and its pictures")
{
    CHECK(background_theme(3) == "Hill");
    CHECK(background_theme(7) == "City");
    CHECK(background_theme(std::nullopt) == kBackgroundThemes[kDefaultBackground]);
    CHECK(background_theme(-1) == kBackgroundThemes[kDefaultBackground]);
    CHECK(background_theme(8) == kBackgroundThemes[kDefaultBackground]);
    CHECK(mountain_pictures("Snow") == std::array<std::string, 2> { "BackgroundSnow1", "BackgroundSnow2" });
}

TEST_CASE("the ring stands round the middle of the map, dropped by its theme")
{
    save::TerrainGrid g;
    g.width = 30;
    g.height = 40;
    g.tile_size_x = 4;
    g.tile_size_z = 4;
    g.origin_x = 10;
    const MountainPlace place = mountain_place(g, "Desert");
    CHECK(place.centre == glm::vec3(70, -kMountainDrop[1], 80));
    CHECK(place.scale == kMountainScale);
    CHECK(mountain_place(g, "Nowhere").centre.y == 0);
}

TEST_CASE("the ring is scaled round its centre, each mesh with its picture")
{
    MountainShapes shapes;
    shapes[0].texture = "a";
    shapes[0].vertices = { { { 2, 0, 0 }, { }, { 0, 1 } }, { { 0, 1, 2 }, { }, { 1, 0 } }, { { -2, 0, 0 }, { }, { 1, 1 } } };
    shapes[0].indices = { 0, 1, 2 };
    shapes[1].texture = "b";
    shapes[1].vertices = { { { 0, 0, -2 }, { }, { 0, 0 } } };
    shapes[1].indices = { 0, 0, 0, 5, 5, 5 }; // Out of range indices are left out
    const ObjectMesh m = mountain_mesh(shapes, { { 10, 1, 10 }, 50 });
    REQUIRE(m.batches.size() == 2);
    CHECK(m.batches[0].texture == "a");
    CHECK(m.batches[0].blend == PartBlend::Cutout);
    CHECK(m.batches[1].count == 3);
    CHECK(m.vertices[0].position == glm::vec3(110, 1, 10));
    CHECK(m.vertices[1].position == glm::vec3(10, 51, 110));
    CHECK(m.vertices[1].uv == glm::vec2(1, 0));
    CHECK(mountain_mesh({ }, { }).vertices.empty());
}
