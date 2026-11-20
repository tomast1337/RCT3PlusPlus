// SPDX-License-Identifier: GPL-3.0-or-later
//
// In-memory model of the world held by a park or scenario save: the terrain grid, water bodies, path tiles,
// placed scenery, weather, sky, time of day and the saved camera.
//
// The model is plain data. Loading is a free function over a decoded save; the compound blob attributes
// (terrain, water, skirt trees) are parsed by pure functions over bytes so they can be tested and fuzzed on
// their own.
//
// Conventions of the stored data that matter to users of this model:
//  - Tiles are addressed by (col, row). Column grows with world X, row with world Z. A tile has four corner
//    heights in the order (col,row), (col+1,row), (col,row+1), (col+1,row+1). Neighbouring tiles share
//    corner heights except across a cliff, where they may differ.
//  - A water triangle uses corners 0,1,2 of its tile (triangle 0) or 3,2,1 (triangle 1).
//  - Object references are ids of objects in the same save; 0 or an id that is not found means "none".
//  - Values whose meaning is not confirmed (slope ids, break flags, camera orientation) are kept
//    as the stored integers or floats.
#pragma once

#include "core/id.hpp"
#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::save::model
{

    struct WorldPoint3
    {
        f32 x = 0;
        f32 y = 0;
        f32 z = 0;
    };

    struct WorldColour
    {
        f32 r = 0;
        f32 g = 0;
        f32 b = 0;
    };

    // ---- Terrain -------------------------------------------------------------------------------------------

    /// Bits of a terrain cell's flag byte.
    enum class TileFlag : u8
    {
        ForSale = 0x01,
        Owned = 0x02,
        RightsOnly = 0x04,    ///< Combined with ForSale or Owned: the rights-only variant
        EditorMarkA = 0x08,   ///< Transient, editor only
        EditorMarkB = 0x10,   ///< Transient, editor only
        EntranceFront = 0x20, ///< Transient: tile in front of the park entrance
        HeldBack = 0x40,      ///< Not for sale until the construction rights prize puts it on sale
    };

    /// A coarse reading of the ownership bits.
    enum class Ownership : u8
    {
        Unowned,
        ForSale,
        Owned,
        ForSaleRights,
        OwnedRights,
        Locked
    };

    [[nodiscard]] const char* to_string(Ownership o) noexcept;

    struct TerrainCell
    {
        std::array<f32, 4> corners { }; ///< Heights in the corner order described at the top of this file
        u8 surface = 0;                 ///< Index into the terrain surface table
        u8 cliff_style = 0;
        u8 flags = 0; ///< TileFlag bits, kept raw so unknown bits survive

        [[nodiscard]] bool has(TileFlag f) const noexcept
        {
            return (flags & static_cast<u8>(f)) != 0;
        }
        [[nodiscard]] Ownership ownership() const noexcept;
        [[nodiscard]] f32 min_height() const noexcept;
        [[nodiscard]] f32 max_height() const noexcept;
    };

    /// Which cell record size a blob used. Both decode to the same TerrainCell.
    enum class TerrainCellLayout : u8
    {
        Cell24,
        Cell20
    };

    /// A tile of the park, by column and row: its own type, so a column and a row travel together and in order.
    struct Tile
    {
        i32 col = 0;
        i32 row = 0;
        friend auto operator<=>(const Tile&, const Tile&) = default;
    };

    struct TerrainGrid
    {
        i32 width = 0;  ///< Tile columns, 0 when the save has no terrain
        i32 height = 0; ///< Tile rows
        f32 origin_x = 0;
        f32 origin_z = 0;
        f32 tile_size_x = 0;
        f32 tile_size_z = 0;
        TerrainCellLayout layout = TerrainCellLayout::Cell24;
        usize trailing_bytes = 0;           ///< Unused bytes after the last cell in the blob (0 in well-formed data)
        std::vector<TerrainCell> cells { }; ///< Row-major, index = row * width + col

        [[nodiscard]] bool empty() const noexcept
        {
            return cells.empty();
        }
        [[nodiscard]] bool contains(i32 col, i32 row) const noexcept
        {
            return col >= 0 && row >= 0 && col < width && row < height;
        }
        [[nodiscard]] bool contains(Tile tile) const noexcept
        {
            return contains(tile.col, tile.row);
        }
        /// Precondition: contains(col, row).
        [[nodiscard]] const TerrainCell& at(i32 col, i32 row) const noexcept
        {
            return cells[static_cast<usize>(row) * static_cast<usize>(width) + static_cast<usize>(col)];
        }
        [[nodiscard]] TerrainCell& at(i32 col, i32 row) noexcept
        {
            return cells[static_cast<usize>(row) * static_cast<usize>(width) + static_cast<usize>(col)];
        }
        [[nodiscard]] const TerrainCell& at(Tile tile) const noexcept
        {
            return at(tile.col, tile.row);
        }
        [[nodiscard]] TerrainCell& at(Tile tile) noexcept
        {
            return at(tile.col, tile.row);
        }
        /// World X of the left edge of column `col`, and Z of the near edge of row `row`.
        [[nodiscard]] f32 world_x(i32 col) const noexcept
        {
            return origin_x + static_cast<f32>(col) * tile_size_x;
        }
        [[nodiscard]] f32 world_z(i32 row) const noexcept
        {
            return origin_z + static_cast<f32>(row) * tile_size_z;
        }

        /// Surface height at a world position, following the two-triangle split of a tile (corners 0,1,2 and
        /// 3,2,1). Empty outside the map.
        [[nodiscard]] std::optional<f32> height_at(f32 x, f32 z) const noexcept;
        /// Same query using plain bilinear interpolation of the four corners of the tile.
        [[nodiscard]] std::optional<f32> height_bilinear_at(f32 x, f32 z) const noexcept;
    };

    /// Largest number of cells a terrain blob may declare (both dimensions are one byte).
    inline constexpr usize kMaxTerrainCells = 255u * 255u;

    /// Parses the terrain blob. Accepts 24-byte and 20-byte cell records; the byte length must equal the header
    /// plus width * height cells (up to a few unused trailing bytes).
    [[nodiscard]] std::expected<TerrainGrid, Error> decode_terrain(std::span<const std::byte> blob);

    // ---- Water ---------------------------------------------------------------------------------------------

    struct WaterTri
    {
        u8 col = 0;
        u8 row = 0;
        u8 triangle = 0; ///< 0 or 1
        u8 mask = 0;     ///< Bit i set when corner i of the triangle is below the surface
    };

    struct WaterBody
    {
        f32 level = 0; ///< Surface height in world units
        std::vector<WaterTri> tris { };
    };

    struct WaterLayer
    {
        i32 width = 0; ///< Map size the layer was written for
        i32 height = 0;
        std::vector<WaterBody> bodies { };

        [[nodiscard]] usize triangle_count() const noexcept;
    };

    [[nodiscard]] std::expected<WaterLayer, Error> decode_water(std::span<const std::byte> blob);

    /// The submerged mask a triangle should have for a surface `level`: bit i set when the i-th corner of the
    /// triangle is strictly below it. Empty when the tile is outside the grid or `triangle` is not 0 or 1.
    [[nodiscard]] std::optional<u8> expected_water_mask(const TerrainGrid& grid, f32 level, Tile tile, i32 triangle) noexcept;

    // ---- Skirt trees ---------------------------------------------------------------------------------------

    /// Scenery placed outside the map to dress the horizon.
    struct SkirtTree
    {
        u64 scenery_ref = 0;
        WorldPoint3 position { };
        std::string symbol { }; ///< Asset symbol of the referenced scenery, empty when it does not resolve
    };

    struct RawSkirtTree
    {
        u64 scenery_ref = 0;
        WorldPoint3 position { };
    };

    [[nodiscard]] std::expected<std::vector<RawSkirtTree>, Error> decode_skirt_trees(std::span<const std::byte> blob);

    // ---- Paths ---------------------------------------------------------------------------------------------

    enum class PathKind : u8
    {
        Ground,
        Flying,
        Queue,
        Unknown
    };

    [[nodiscard]] const char* to_string(PathKind k) noexcept;

    inline constexpr u8 kNoDirection = 255;

    /// The stored slope of a queue laid on the ground, following the terrain instead of a height of its own.
    inline constexpr u8 kSlopeOnGround = 3;

    /// Extra data stored by elevated paths and queues.
    struct PathElevation
    {
        u8 slope = 0;                       ///< Slope id as stored
        std::optional<i32> base_height { }; ///< Empty when the stored value is the unset marker
        i32 quantised_height = 0;           ///< Height in steps; the step size is not confirmed
        u64 scenery_ref = 0;                ///< Placed object that draws the piece
        bool underground = false;
    };

    struct PathQueueInfo
    {
        u8 start_direction = 0;
        u8 end_direction = 0;
        u64 fence_ref = 0;
        std::string fence_symbol { };  ///< Asset symbol of the fence along the queue's sides, empty when it does not resolve
        std::string fence_overlay { }; ///< Overlay holding it, as PlacedObject::overlay
        std::array<i32, 3> fence_colours { };
        u64 queue_line_ref = 0;
    };

    struct PathTile
    {
        u64 id = 0;
        PathKind kind = PathKind::Unknown;
        u8 raw_kind = 0; ///< The stored kind byte
        u8 col = 0;
        u8 row = 0;
        u8 direction = kNoDirection; ///< Quarter turns 0..3, or kNoDirection
        u8 surface_variant = 0;      ///< Stored as 0, 1 or 255
        u64 surface_ref = 0;
        std::string surface_name { };                          ///< System name of the surface style, empty when unresolved
        std::optional<std::array<i32, 3>> surface_colours { }; ///< Picked colours of a queue's ground surface
        std::optional<PathElevation> elevation { };            ///< Present for elevated paths and queues
        std::optional<PathQueueInfo> queue { };                ///< Present for queues
    };

    struct QueueLine
    {
        u64 id = 0;
        u64 head_ref = 0;
        i32 length = 0;
        u64 ride_ref = 0;
        i32 station_index = 0;
    };

    // ---- Placed scenery ------------------------------------------------------------------------------------

    inline constexpr i32 kUnsetBaseHeight = -32768;

    struct PlacedObject
    {
        u64 id = 0;
        u64 definition_ref = 0; ///< Database entry that names the asset
        std::string symbol { }; ///< Asset symbol from that entry, empty when it does not resolve
        /// The overlay that holds the asset, from the same entry: a path relative to the game's folder, without
        /// the ".common.ovl" ending, with either kind of slash; empty when it does not resolve.
        std::string overlay { };
        i32 col = 0;
        i32 row = 0;
        std::optional<i32> base_height { };   ///< Whole-unit base height; empty when stored as unset
        i32 rotation = 0;                     ///< Quarter turns 0..3
        i32 quadrant = 0;                     ///< Sub-tile corner 0..3
        std::optional<f32> height_adjust { }; ///< Only in newer layouts
        u64 owner_ref = 0;                    ///< Own id when free-standing, else the parent piece
        std::array<i32, 3> colours { };
        i32 height_offset = 0; ///< Unit not confirmed
        i32 frame_offset = 0;
        bool force_absolute_height = false;
        bool hidden = false;
        bool fixture = false;        ///< Built by the park itself (fences round rides, queues and the park), not stored; not clicked
        std::string support_set { }; ///< Support set its columns are built from in place of its model's; not stored
        i32 break_flags = 0;         ///< Raw bit set; individual bits not confirmed
        f32 break_time = 0;
        u32 behaviour_count = 0;
        units::Seconds motion_time { }; ///< Into the motion its model plays, so like objects do not move in step

        [[nodiscard]] bool free_standing() const noexcept
        {
            return owner_ref == id;
        }
    };

    /// What a litter bin placed in the park holds.
    struct BinState
    {
        u64 object_ref = 0; ///< The placed object that is the bin
        f32 capacity = 0;
        f32 contents = 0;                    ///< In the same measure as `capacity`
        std::optional<f32> last_emptied { }; ///< Park time, as the clock counts it; empty when never emptied
    };

    /// Somewhere guests stop to use a placed object (a bench, a bin, a view, a water cannon, a passport post), as the
    /// object a saved guest names while using it or heading for it.
    struct UsePoint
    {
        u64 id = 0;                          ///< What a guest's saved state names
        u64 object_ref = 0;                  ///< The placed object it belongs to
        std::optional<i32> stamp_colour { }; ///< A passport post's, as it keeps it
    };

    /// How much water a flower bed placed in the park holds.
    struct FlowerBedState
    {
        u64 object_ref = 0; ///< The placed object that is the bed
        f32 water = 0;      ///< In its scenery data's measure; below 0 for a bed never watered, which starts full
    };

    /// A square of a pool that holds water. Its pieces stand in a stack from the floor up to the rim: the rim
    /// piece at the surface height, the floor `depth` below it.
    struct PoolWater
    {
        i32 col = 0;
        i32 row = 0;
        i32 surface = 0; ///< Whole units, as placed objects' heights
        i32 depth = 0;   ///< Whole units
    };

    /// A square of a pool complex, of any kind, as the game keeps it.
    struct PoolSegmentState
    {
        u64 id = 0;
        i32 col = 0;
        i32 row = 0;
        i32 type = 0;    ///< 0 water, 1 paved, 2 stairs, 3 high diving board, 4 spa pool, 5 to 7 paths
        i32 surface = 0; ///< Whole units, as placed objects' heights
        i32 depth = 0;
        i32 special = 0; ///< A high diving board's height over the surface
        i32 side = 0;    ///< Bits of the sides: 1 the column before, 2 the next row, 4 the next column, 8 the row before; a high diving board's one, the side it faces
        i32 flags = 0;   ///< Its low two bits the edge setting it was laid with: 0 straight, 1 tight curves, 2 cut across, 3 loose curves
        i32 complex = 0; ///< Its pool complex, by index
        /// The ground's corner heights, whole units, from before the square was laid, when the park keeps them.
        /// shortcut: taken in the order the park lists them; only level ground under pools was seen, check the order
        /// on a pool laid on a slope.
        std::optional<std::array<f32, 4>> ground { };
    };

    /// A pool complex's own settings.
    struct PoolComplexState
    {
        i32 index = 0;
        units::Money ticket { };
        std::string name { }; ///< As stored; "%ME%" and a number for the default
        // How its wave machines run, as stored: what parks saved with a pool just built show, when missing.
        f32 wave_height = 8;
        units::TurnRate wave_frequency { 6 };
        units::Seconds wave_on { 30 };
        units::Seconds wave_every { 60 };
        f32 dirt = 0; ///< As stored: swimmers add to it, cleaning takes it away; the more water, the more it takes to show
    };

    /// A changing room: the placed object it is, the pool square it opens on, and its settings.
    struct ChangingRoomState
    {
        u64 object_ref = 0;
        u64 segment = 0; ///< A PoolSegmentState's id
        bool open = false;
        i32 fluid = 0; ///< The cleaning fluid picked, as stored
    };

    /// A waterfall: water pouring from a higher lake over a cliff edge into a lower one.
    /// A tile's corners as they were before something cut into the ground there.
    struct CarvedTile
    {
        Tile tile { };
        std::array<f32, 4> corners { };
    };

    struct Waterfall
    {
        WorldPoint3 lip_left { };  ///< One end of the edge the water pours over, at the upper lake's level
        WorldPoint3 lip_right { }; ///< The other end
        WorldPoint3 top { };       ///< A point of the upper lake behind the edge; its height is that lake's level
        WorldPoint3 bottom { };    ///< Where the water lands; its height is the lower lake's level
        WorldPoint3 flow { };      ///< Unit direction the water runs across the ground, from `top` to `bottom`
        /// Of one the player made: the ground its spillway was cut from, as it was, put back when it goes.
        std::vector<CarvedTile> carved { };
    };

    /// The park's animal enclosures: which enclosure each tile belongs to, and what fence each is built with.
    /// The fence's panels are not kept; they are built again from the tiles.
    struct EnclosureMap
    {
        i32 cols = 0;
        i32 rows = 0;
        std::vector<std::optional<i32>> tiles { }; ///< Row-major, the place in `fences` of the tile's enclosure; none outside one
        std::vector<std::string> fences { };       ///< Of each enclosure, the kind of fence it has; empty for a place unused

        [[nodiscard]] std::optional<i32> at(i32 col, i32 row) const noexcept
        {
            return col >= 0 && row >= 0 && col < cols && row < rows ? tiles[static_cast<usize>(row * cols + col)] : std::nullopt;
        }
    };

    /// What a sign placed in the park says.
    struct SignState
    {
        u64 object_ref = 0;   ///< The placed object that is the sign
        i32 kind = 0;         ///< 0 shows `text`; 1 forbids entry
        std::string text { }; ///< Written by the player
    };

    // ---- Environment ---------------------------------------------------------------------------------------

    enum class WeatherType : i32
    {
        Sunny = 0,
        SunnyCloudy = 1,
        Cloudy = 2,
        LightRain = 3,
        HeavyRain = 4,
        Thunderstorm = 5,
        Snowstorm = 6,
        Unknown = -1, ///< The stored value is outside the known range; see the raw field
    };

    [[nodiscard]] WeatherType weather_type_from(i32 raw) noexcept;
    [[nodiscard]] const char* to_string(WeatherType t) noexcept;

    struct FogSetting
    {
        WorldColour colour { };
        f32 far = 0;
        f32 near = 0;
        f32 shadow_damper = 0;
    };

    struct Weather
    {
        WeatherType type = WeatherType::Unknown;
        i32 raw_type = 0;
        WeatherType forecast = WeatherType::Unknown;
        i32 raw_forecast = 0;
        i32 climate = 0; ///< The scenario editor's climate, by its place in the editor's list
        units::Degrees temperature { };
        f32 forecast_temperature = 0;
        f32 forecast_delay = 0;
        f32 sequence_delay = 0;
        f32 wind_speed = 0;
        f32 wind_acceleration = 0;
        i32 overlay = 0;
        i32 forecast_overlay = 0;
        i32 colour = 0;
        i32 forecast_colour = 0;
        i32 sound = 0;
        i32 forecast_sound = 0;
        std::optional<f32> thunder_time { };
        std::optional<FogSetting> fog { };
        std::optional<std::array<WorldColour, 2>> cloud_colours { };
        std::optional<std::array<WorldColour, 3>> light_colours { };
    };

    struct WorldTime
    {
        f32 day_fraction = 0;           ///< 0..1 through the day
        units::Seconds time { };        ///< Accumulated
        units::Seconds time_origin { }; ///< Reference stamp for `time`
        std::optional<i32> day_night_mode { };
        std::optional<f32> day_fraction_as_rendered { };
    };

    struct Camera
    {
        WorldPoint3 position { };
        WorldPoint3 orientation { }; ///< Three stored floats; likely Euler angles in radians, not confirmed
    };

    // ---- Model and loader ----------------------------------------------------------------------------------

    enum class WorldIssueKind : u8
    {
        MissingTerrain,
        BadTerrainBlob,
        BadWaterBlob,
        BadSkirtBlob,
        DuplicateSingleton, ///< More than one object of a class that should have exactly one
        NonFiniteHeight,    ///< A terrain corner is NaN or infinite
        WaterSizeMismatch,  ///< Water layer written for another map size
        WaterTileOutOfRange,
        WaterBadTriangle,  ///< Triangle index other than 0 or 1
        WaterMaskMismatch, ///< Stored mask differs from the one recomputed from heights
        PathTileOutOfRange,
        PathKindMismatch,    ///< Kind byte disagrees with the object's class
        PathSceneryMismatch, ///< Linked placed object sits on another tile
        PathSceneryDangling, ///< Scenery link set but not found
        ObjectOutOfRange,    ///< Placed object outside the map
        Count_
    };

    [[nodiscard]] const char* to_string(WorldIssueKind k) noexcept;

    /// Findings of load_world about the data it read, aggregated per kind.
    struct WorldIssue
    {
        WorldIssueKind kind = WorldIssueKind::MissingTerrain;
        usize count = 0;
        u64 first_object = 0; ///< Id of the first object affected, 0 when not tied to one
    };

    struct WorldModel
    {
        TerrainGrid terrain { };
        std::vector<std::byte> blocking_scenery { }; ///< Raw blob, kept as stored
        std::vector<SkirtTree> skirt_trees { };
        WaterLayer water { };
        std::vector<PathTile> paths { };
        std::vector<QueueLine> queue_lines { };
        std::vector<PlacedObject> objects { };
        std::vector<BinState> bins { };
        std::vector<SignState> signs { };
        std::vector<FlowerBedState> flower_beds { };
        std::vector<u64> benches { }; ///< The placed objects that are benches guests can sit on
        std::vector<UsePoint> use_points { };
        std::vector<PoolWater> pool_water { };
        std::vector<PoolSegmentState> pool_segments { }; ///< Every square of the pools, water and the rest
        std::vector<PoolComplexState> pool_complexes { };
        std::vector<ChangingRoomState> changing_rooms { };
        std::vector<Waterfall> waterfalls { };
        EnclosureMap enclosures { };
        std::optional<Weather> weather { };
        std::optional<i32> sky_background { }; ///< Background (sky box) index
        std::optional<WorldTime> time { };
        std::optional<Camera> camera { };
        std::vector<WorldIssue> issues { };

        [[nodiscard]] usize issue_count(WorldIssueKind k) const noexcept;
    };

    /// An object's place in WorldModel::objects, and in the copies of that list the park keeps and builds on.
    using PlacedObjectIndex = core::Id<struct PlacedObjectIndexTag>;

    /// Builds the world model from a read save. Fails only when an object cannot be decoded. Malformed blobs and
    /// inconsistent data are reported in `issues` and leave the affected part empty.
    [[nodiscard]] std::expected<WorldModel, Error> load_world(const SaveFile& file, const ObjectIndex& index);

} // namespace rct3::save::model
