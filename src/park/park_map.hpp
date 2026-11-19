// SPDX-License-Identifier: GPL-3.0-or-later
//
// The park seen from straight above, drawn as a picture for the map window. North (+Z) is up and +X is to
// the right, as the camera sees the park when it faces north. The picture is made of layers the player
// turns on and off: the ground shaded by its height and slope, contour lines, water, the park's own land,
// paths, scenery, the attractions by kind, and the people.
//
// Drawing the ground costs the most, so it is done once when the park loads (map_ground); putting the
// layers together (compose_map) only blends those pictures and draws the shapes over them, which is quick
// enough to do again whenever a layer is turned on or off. People move, so they get a picture of their own
// (people_picture) that is drawn again every so often and laid over the rest.
//
// Pictures have a fixed number of pixels per tile, so a tile is a square whatever the map's shape; the
// window fits the picture without stretching it.
#pragma once

#include "core/types.hpp"
#include "math/glm.hpp"
#include "save/model/world.hpp"

#include <array>
#include <bitset>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    enum class MapLayer : u8
    {
        Relief,   ///< The ground shaded by height and slope; without it the ground is one flat colour
        Contours, ///< Lines of equal height
        Water,
        ParkLand, ///< Land outside the park dimmed, land for sale marked, the park's edge drawn
        Paths,    ///< Paths and queues
        Scenery,  ///< Placed objects that belong to no attraction
        Rides,
        Food,
        Drinks,
        Souvenirs,
        Information,
        FirstAid,
        CashMachines,
        Toilets,
        Guests,
        Staff,
        Patrols, ///< The tiles of the staff's patrol areas
    };
    inline constexpr usize kMapLayerCount = 17;

    /// The layers shown, by MapLayer.
    using MapLayers = std::bitset<kMapLayerCount>;

    [[nodiscard]] inline bool shows(const MapLayers& layers, MapLayer layer)
    {
        return layers.test(static_cast<usize>(layer));
    }
    [[nodiscard]] MapLayers all_map_layers();

    /// A colour with straight (not premultiplied) alpha.
    struct MapRgba
    {
        u8 r = 0;
        u8 g = 0;
        u8 b = 0;
        u8 a = 255;
        friend bool operator==(const MapRgba&, const MapRgba&) = default;
    };

    /// The colour a layer's things are drawn in, for the legend too. The ground layers give a colour that
    /// stands for them.
    [[nodiscard]] MapRgba map_layer_colour(MapLayer layer);

    /// The layer of an attraction that sells or serves something, by the type its stall or facility item
    /// gives; any other type, or none, is a ride.
    [[nodiscard]] MapLayer attraction_layer(std::optional<u32> type_id);

    /// The ground the map covers and the way between the park and the map, where the map runs from 0 to 1
    /// across and down.
    struct MapFrame
    {
        f32 west_edge = 0;
        f32 south_edge = 0;
        f32 width = 1; ///< West to east
        f32 depth = 1; ///< South to north
    };
    [[nodiscard]] MapFrame map_frame(const save::model::TerrainGrid& grid);
    /// The map point of the park point (x, z).
    [[nodiscard]] glm::vec2 to_map(const MapFrame& frame, glm::vec2 park);
    /// The park point (x, z) of a map point.
    [[nodiscard]] glm::vec2 to_park(const MapFrame& frame, glm::vec2 map);

    struct MapPathTile
    {
        i32 col = 0;
        i32 row = 0;
        bool queue = false;
    };

    /// Something drawn as the ground it stands on: four corners in the park (x, z), in order round it.
    struct MapShape
    {
        MapLayer layer = MapLayer::Scenery;
        std::array<glm::vec2, 4> corners { };
    };

    /// What the map shows of a park, gathered when it loads.
    struct MapFeatures
    {
        std::vector<MapPathTile> paths;
        /// The surface height of the water on each tile, row by row as the grid's cells; none where dry.
        std::vector<std::optional<f32>> water;
        std::vector<MapShape> shapes;
        /// The colour of each ground surface, by its index in the terrain types; the land's middle tint for
        /// those without one. Set by the owner, from the surfaces' pictures.
        std::vector<std::optional<MapRgba>> surfaces;
    };

    /// The tiles of `paths` (each once) and the water of `water` over `grid`.
    [[nodiscard]] MapFeatures map_features(const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const save::model::WaterLayer& water);

    /// The shape of a box's ground: the corners of `box` (eight, the bit 1 of an index choosing the top) on
    /// the ground, in order round it.
    [[nodiscard]] std::array<glm::vec2, 4> ground_corners(std::span<const glm::vec3, 8> box);
    /// The shape of tile (col, row).
    [[nodiscard]] std::array<glm::vec2, 4> tile_corners(const save::model::TerrainGrid& grid, save::model::Tile tile);

    /// A picture: rows from the top, four bytes a pixel (red, green, blue, straight alpha).
    struct MapPicture
    {
        i32 width = 0;
        i32 height = 0;
        std::vector<u8> rgba;

        [[nodiscard]] MapRgba at(i32 x, i32 y) const;
    };

    /// Pixels per tile for the map of `grid`: enough that the picture is about a thousand pixels across,
    /// within limits that keep small parks crisp and large ones affordable.
    [[nodiscard]] i32 map_scale(const save::model::TerrainGrid& grid);
    inline constexpr i32 kMinMapScale = 2;
    inline constexpr i32 kMaxMapScale = 8;
    inline constexpr i32 kMapPixelsAcross = 1024;

    /// The ground layers of a map, `scale` pixels per tile.
    struct MapGround
    {
        i32 scale = 1;
        MapPicture relief;   ///< Opaque
        MapPicture flat;     ///< Opaque: the ground without relief
        MapPicture contours; ///< Clear but for the lines
        MapPicture water;    ///< Clear but for the water
    };
    [[nodiscard]] MapGround map_ground(const save::model::TerrainGrid& grid, const MapFeatures& features, i32 scale);

    /// The map with the layers in `layers`, people aside.
    [[nodiscard]] MapPicture compose_map(const MapGround& ground, const save::model::TerrainGrid& grid, const MapFeatures& features, const MapLayers& layers);

    struct MapPerson
    {
        glm::vec2 at { 0.0f }; ///< In the park (x, z)
        bool staff = false;
    };
    /// The people the map shows, and the tiles (col, row) of the staff's patrol areas, once for each area.
    struct MapPeople
    {
        std::vector<MapPerson> people;
        std::vector<glm::ivec2> patrol_tiles;
    };

    /// A clear picture the size of the map's with a dot for each person whose layer `layers` shows, over the
    /// patrol tiles tinted, deeper where areas overlap.
    [[nodiscard]] MapPicture people_picture(const save::model::TerrainGrid& grid, i32 scale, std::span<const MapPerson> people, const MapLayers& layers, std::span<const glm::ivec2> patrol_tiles = { });

} // namespace rct3::park
