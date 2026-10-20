// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/park_map.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3;
using namespace rct3::park;

namespace
{
    /// A park `width` by `height` tiles of 4 units, all owned, every corner at `height_at(col, row)`.
    template<class Height> save::TerrainGrid grid(int width, int height, Height height_at)
    {
        save::TerrainGrid g;
        g.width = width;
        g.height = height;
        g.origin_x = -10;
        g.origin_z = 20;
        g.tile_size_x = 4;
        g.tile_size_z = 4;
        g.cells.resize(static_cast<std::size_t>(width * height));
        for (int row = 0; row < height; ++row)
        {
            for (int col = 0; col < width; ++col)
            {
                save::TerrainCell& c = g.cells[static_cast<std::size_t>(row * width + col)];
                c.corners = { height_at(col, row), height_at(col + 1, row), height_at(col, row + 1), height_at(col + 1, row + 1) };
                c.flags = static_cast<std::uint8_t>(save::TileFlag::Owned);
            }
        }
        return g;
    }

    save::TerrainGrid flat(int width, int height)
    {
        return grid(width, height, [](int, int) { return 0.0f; });
    }

    MapLayers only(std::initializer_list<MapLayer> layers)
    {
        MapLayers out;
        for (const MapLayer l : layers)
            out.set(static_cast<std::size_t>(l));
        return out;
    }

    /// The pixel in the middle of tile (col, row).
    MapRgba middle_of(const MapPicture& p, const save::TerrainGrid& g, std::int32_t scale, int col, int row)
    {
        return p.at(col * scale + scale / 2, (g.height - 1 - row) * scale + scale / 2);
    }

    bool near(MapRgba a, MapRgba b, int by = 3)
    {
        return std::abs(a.r - b.r) <= by && std::abs(a.g - b.g) <= by && std::abs(a.b - b.b) <= by;
    }
} // namespace

TEST_CASE("the map has north up and east to the right")
{
    const save::TerrainGrid g = flat(10, 5);
    const MapFrame f = map_frame(g);
    CHECK(f.width == 40);
    CHECK(f.depth == 20);
    const glm::vec2 south_west = to_map(f, { -10, 20 });
    CHECK(south_west.x == doctest::Approx(0));
    CHECK(south_west.y == doctest::Approx(1));
    const glm::vec2 north_east = to_map(f, { 30, 40 });
    CHECK(north_east.x == doctest::Approx(1));
    CHECK(north_east.y == doctest::Approx(0));
    const glm::vec2 back = to_park(f, to_map(f, { 3.5f, 27.25f }));
    CHECK(back.x == doctest::Approx(3.5f));
    CHECK(back.y == doctest::Approx(27.25f));
}

TEST_CASE("stalls and facilities fall in the layer of what they serve, the rest are rides")
{
    CHECK(attraction_layer(7) == MapLayer::Food);
    CHECK(attraction_layer(8) == MapLayer::Drinks);
    CHECK(attraction_layer(9) == MapLayer::Souvenirs);
    CHECK(attraction_layer(11) == MapLayer::Toilets);
    CHECK(attraction_layer(12) == MapLayer::CashMachines);
    CHECK(attraction_layer(13) == MapLayer::FirstAid);
    CHECK(attraction_layer(14) == MapLayer::Information);
    CHECK(attraction_layer(3) == MapLayer::Rides);
    CHECK(attraction_layer(std::nullopt) == MapLayer::Rides);
}

TEST_CASE("the map is about a thousand pixels across within its limits")
{
    CHECK(map_scale(flat(10, 10)) == kMaxMapScale);
    CHECK(map_scale(flat(200, 100)) == 5);
    save::TerrainGrid huge;
    huge.width = 1000;
    huge.height = 10;
    CHECK(map_scale(huge) == kMinMapScale);
}

TEST_CASE("map features keep each path tile once and the highest water on a tile")
{
    const save::TerrainGrid g = flat(4, 4);
    std::vector<save::PathTile> paths(3);
    paths[0].col = 1;
    paths[0].row = 2;
    paths[1].col = 1;
    paths[1].row = 2; // A path above another one
    paths[2].col = 3;
    paths[2].row = 3;
    paths[2].kind = save::PathKind::Queue;
    save::WaterLayer water;
    water.bodies = { { 1.0f, { { 2, 1, 0, 7 } } }, { 2.5f, { { 2, 1, 1, 7 } } } };
    const MapFeatures f = map_features(g, paths, water);
    REQUIRE(f.paths.size() == 2);
    CHECK(!f.paths[0].queue);
    CHECK(f.paths[1].queue);
    REQUIRE(f.water.size() == 16);
    CHECK(f.water[1 * 4 + 2] == 2.5f);
    CHECK(!f.water[0].has_value());
}

TEST_CASE("a box's ground goes round it")
{
    std::array<glm::vec3, 8> box { };
    for (std::size_t i = 0; i < box.size(); ++i)
        box[i] = { (i & 1) != 0 ? 2.0f : 0.0f, (i & 2) != 0 ? 5.0f : 0.0f, (i & 4) != 0 ? 3.0f : 0.0f };
    const auto c = ground_corners(box);
    CHECK(c[0] == glm::vec2(0, 0));
    CHECK(c[1] == glm::vec2(2, 0));
    CHECK(c[2] == glm::vec2(2, 3));
    CHECK(c[3] == glm::vec2(0, 3));
}

TEST_CASE("paths, attractions and water show only with their layers")
{
    const save::TerrainGrid g = flat(6, 6);
    MapFeatures f;
    f.paths = { { 1, 1, false } };
    f.shapes = { { MapLayer::Food, tile_corners(g, { 4, 4 }) }, { MapLayer::Rides, tile_corners(g, { 4, 1 }) } };
    f.water.assign(36, std::nullopt);
    f.water[2 * 6 + 2] = 1.0f; // A metre deep over flat ground
    const std::int32_t scale = 8;
    const MapGround ground = map_ground(g, f, scale);
    const MapPicture none = compose_map(ground, g, f, only({ MapLayer::Relief }));
    const MapPicture all = compose_map(ground, g, f, all_map_layers());
    REQUIRE(all.width == 48);
    REQUIRE(all.height == 48);

    const MapRgba land = middle_of(none, g, scale, 0, 0);
    CHECK(land.a == 255);
    for (const auto& [col, row] : { std::pair { 1, 1 }, std::pair { 4, 4 }, std::pair { 4, 1 }, std::pair { 2, 2 } })
    {
        CHECK(near(middle_of(none, g, scale, col, row), land));
        CHECK(!near(middle_of(all, g, scale, col, row), land, 20));
    }
    CHECK(near(middle_of(all, g, scale, 1, 1), map_layer_colour(MapLayer::Paths)));
    CHECK(near(middle_of(all, g, scale, 4, 4), map_layer_colour(MapLayer::Food)));
    CHECK(near(middle_of(all, g, scale, 4, 1), map_layer_colour(MapLayer::Rides)));
    // Water is blue, wherever the ground is.
    const MapRgba water = middle_of(all, g, scale, 2, 2);
    CHECK(water.b > water.r + 40);
}

TEST_CASE("land outside the park is shaded and its edge drawn")
{
    save::TerrainGrid g = flat(6, 6);
    for (int col = 3; col < 6; ++col)
    {
        for (int row = 0; row < 6; ++row)
            g.cells[static_cast<std::size_t>(row * 6 + col)].flags = 0;
    }
    const MapFeatures f;
    const MapGround ground = map_ground(g, f, 8);
    const MapPicture shaded = compose_map(ground, g, f, only({ MapLayer::Relief, MapLayer::ParkLand }));
    const MapRgba inside = middle_of(shaded, g, 8, 1, 3);
    const MapRgba outside = middle_of(shaded, g, 8, 4, 3);
    CHECK(outside.r + outside.g + outside.b < inside.r + inside.g + inside.b - 30);
    // On the edge between columns 2 and 3: a light line.
    const MapRgba edge = shaded.at(24, 3 * 8 + 4);
    CHECK(edge.r + edge.g + edge.b > inside.r + inside.g + inside.b);
}

TEST_CASE("contour lines cross slopes and leave flat ground alone")
{
    const save::TerrainGrid slope = grid(8, 8, [](int col, int) { return static_cast<float>(col) * 2.0f; });
    const MapFeatures f;
    const MapGround on_slope = map_ground(slope, f, 8);
    int lines = 0;
    for (std::int32_t x = 0; x < on_slope.contours.width; ++x)
        lines += on_slope.contours.at(x, 20).a > 40 ? 1 : 0;
    CHECK(lines >= 4);

    const MapGround on_flat = map_ground(flat(8, 8), f, 8);
    for (std::int32_t x = 0; x < on_flat.contours.width; ++x)
        CHECK(on_flat.contours.at(x, 20).a == 0);
}

TEST_CASE("surfaces colour the ground and relief lights slopes that face the north-west")
{
    save::TerrainGrid g = flat(4, 4);
    MapFeatures f;
    f.surfaces = { MapRgba { 200, 50, 50, 255 } };
    const MapGround ground = map_ground(g, f, 4);
    CHECK(near(ground.flat.at(8, 8), { 200, 50, 50, 255 }));

    // Ground rising to the east faces west, towards the light; ground falling to the east faces away.
    const save::TerrainGrid rising = grid(6, 6, [](int col, int) { return static_cast<float>(col) * 3.0f; });
    const save::TerrainGrid falling = grid(6, 6, [](int col, int) { return static_cast<float>(6 - col) * 3.0f; });
    const MapRgba facing = map_ground(rising, { }, 4).relief.at(12, 12);
    const MapRgba away = map_ground(falling, { }, 4).relief.at(12, 12);
    CHECK(facing.r + facing.g + facing.b > away.r + away.g + away.b);
}

TEST_CASE("people are dots on a clear picture, guests and staff by their layers")
{
    const save::TerrainGrid g = flat(4, 4);
    const MapFrame f = map_frame(g);
    const std::vector<MapPerson> people { { to_park(f, { 0.125f, 0.125f }), false }, { to_park(f, { 0.875f, 0.875f }), true } };
    const MapPicture both = people_picture(g, 8, people, all_map_layers());
    CHECK(both.at(0, 0).a == 0);
    CHECK(near(both.at(4, 4), map_layer_colour(MapLayer::Guests)));
    CHECK(near(both.at(28, 28), map_layer_colour(MapLayer::Staff)));

    const MapPicture guests = people_picture(g, 8, people, only({ MapLayer::Guests }));
    CHECK(guests.at(4, 4).a == 255);
    CHECK(guests.at(28, 28).a == 0);
}

TEST_CASE("the staff's patrol areas are tinted under the people, deeper where they overlap")
{
    const save::TerrainGrid g = flat(4, 4);
    const std::vector<glm::ivec2> tiles { { 0, 0 }, { 0, 0 }, { 3, 3 } };
    const MapPicture p = people_picture(g, 8, { }, all_map_layers(), tiles);
    // Row 0 is at the bottom of the picture.
    CHECK(p.at(4, 28).a > p.at(28, 4).a);
    CHECK(p.at(28, 4).a > 0);
    CHECK(p.at(12, 12).a == 0);
    CHECK(people_picture(g, 8, { }, only({ MapLayer::Guests }), tiles).at(4, 28).a == 0);
}
