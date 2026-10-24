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
//  - Values whose meaning is not confirmed (slope ids, break flags, climate id, camera orientation) are kept
//    as the stored integers or floats.
#pragma once

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace rct3::save
{

    struct WorldPoint3
    {
        float x = 0;
        float y = 0;
        float z = 0;
    };

    struct WorldColour
    {
        float r = 0;
        float g = 0;
        float b = 0;
    };

    // ---- Terrain -------------------------------------------------------------------------------------------

    /// Bits of a terrain cell's flag byte.
    enum class TileFlag : std::uint8_t
    {
        ForSale = 0x01,
        Owned = 0x02,
        RightsOnly = 0x04,       ///< Combined with ForSale or Owned: the rights-only variant
        EditorMarkA = 0x08,      ///< Transient, editor only
        EditorMarkB = 0x10,      ///< Transient, editor only
        EntranceFront = 0x20,    ///< Transient: tile in front of the park entrance
        NeverPurchasable = 0x40, ///< Edge of the world, cannot be bought
    };

    /// A coarse reading of the ownership bits.
    enum class Ownership : std::uint8_t
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
        std::array<float, 4> corners { }; ///< Heights in the corner order described at the top of this file
        std::uint8_t surface = 0;         ///< Index into the terrain surface table
        std::uint8_t cliff = 0;           ///< Cliff style index
        std::uint8_t flags = 0;           ///< TileFlag bits, kept raw so unknown bits survive

        [[nodiscard]] bool has(TileFlag f) const noexcept
        {
            return (flags & static_cast<std::uint8_t>(f)) != 0;
        }
        [[nodiscard]] Ownership ownership() const noexcept;
        [[nodiscard]] float min_height() const noexcept;
        [[nodiscard]] float max_height() const noexcept;
    };

    /// Which cell record size a blob used. Both decode to the same TerrainCell.
    enum class TerrainCellLayout : std::uint8_t
    {
        Cell24,
        Cell20
    };

    /// A tile of the park, by column and row: its own type, so a column and a row travel together and in order.
    struct Tile
    {
        int col = 0;
        int row = 0;
        friend bool operator==(const Tile&, const Tile&) = default;
    };

    struct TerrainGrid
    {
        int width = 0;  ///< Tile columns, 0 when the save has no terrain
        int height = 0; ///< Tile rows
        float origin_x = 0;
        float origin_z = 0;
        float tile_size_x = 0;
        float tile_size_z = 0;
        TerrainCellLayout layout = TerrainCellLayout::Cell24;
        std::size_t trailing_bytes = 0;     ///< Unused bytes after the last cell in the blob (0 in well-formed data)
        std::vector<TerrainCell> cells { }; ///< Row-major, index = row * width + col

        [[nodiscard]] bool empty() const noexcept
        {
            return cells.empty();
        }
        [[nodiscard]] bool contains(int col, int row) const noexcept
        {
            return col >= 0 && row >= 0 && col < width && row < height;
        }
        [[nodiscard]] bool contains(Tile tile) const noexcept
        {
            return contains(tile.col, tile.row);
        }
        /// Precondition: contains(col, row).
        [[nodiscard]] const TerrainCell& at(int col, int row) const noexcept
        {
            return cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col)];
        }
        [[nodiscard]] TerrainCell& at(int col, int row) noexcept
        {
            return cells[static_cast<std::size_t>(row) * static_cast<std::size_t>(width) + static_cast<std::size_t>(col)];
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
        [[nodiscard]] float world_x(int col) const noexcept
        {
            return origin_x + static_cast<float>(col) * tile_size_x;
        }
        [[nodiscard]] float world_z(int row) const noexcept
        {
            return origin_z + static_cast<float>(row) * tile_size_z;
        }

        /// Surface height at a world position, following the two-triangle split of a tile (corners 0,1,2 and
        /// 3,2,1). Empty outside the map.
        [[nodiscard]] std::optional<float> height_at(float x, float z) const noexcept;
        /// Same query using plain bilinear interpolation of the four corners of the tile.
        [[nodiscard]] std::optional<float> height_bilinear_at(float x, float z) const noexcept;
    };

    /// Largest number of cells a terrain blob may declare (both dimensions are one byte).
    inline constexpr std::size_t kMaxTerrainCells = 255u * 255u;

    /// Parses the terrain blob. Accepts 24-byte and 20-byte cell records; the byte length must equal the header
    /// plus width * height cells (up to a few unused trailing bytes).
    [[nodiscard]] std::expected<TerrainGrid, Error> decode_terrain(std::span<const std::byte> blob);

    // ---- Water ---------------------------------------------------------------------------------------------

    struct WaterTri
    {
        std::uint8_t col = 0;
        std::uint8_t row = 0;
        std::uint8_t triangle = 0; ///< 0 or 1
        std::uint8_t mask = 0;     ///< Bit i set when corner i of the triangle is below the surface
    };

    struct WaterBody
    {
        float level = 0; ///< Surface height in world units
        std::vector<WaterTri> tris { };
    };

    struct WaterLayer
    {
        int width = 0; ///< Map size the layer was written for
        int height = 0;
        std::vector<WaterBody> bodies { };

        [[nodiscard]] std::size_t triangle_count() const noexcept;
    };

    [[nodiscard]] std::expected<WaterLayer, Error> decode_water(std::span<const std::byte> blob);

    /// The submerged mask a triangle should have for a surface `level`: bit i set when the i-th corner of the
    /// triangle is strictly below it. Empty when the tile is outside the grid or `triangle` is not 0 or 1.
    [[nodiscard]] std::optional<std::uint8_t> expected_water_mask(const TerrainGrid& grid, float level, Tile tile, int triangle) noexcept;

    // ---- Skirt trees ---------------------------------------------------------------------------------------

    /// Scenery placed outside the map to dress the horizon.
    struct SkirtTree
    {
        std::uint64_t scenery_ref = 0;
        WorldPoint3 position { };
        std::string symbol { }; ///< Asset symbol of the referenced scenery, empty when it does not resolve
    };

    struct RawSkirtTree
    {
        std::uint64_t scenery_ref = 0;
        WorldPoint3 position { };
    };

    [[nodiscard]] std::expected<std::vector<RawSkirtTree>, Error> decode_skirt_trees(std::span<const std::byte> blob);

    // ---- Paths ---------------------------------------------------------------------------------------------

    enum class PathKind : std::uint8_t
    {
        Ground,
        Flying,
        Queue,
        Unknown
    };

    [[nodiscard]] const char* to_string(PathKind k) noexcept;

    inline constexpr std::uint8_t kNoDirection = 255;

    /// The stored slope of a queue laid on the ground, following the terrain instead of a height of its own.
    inline constexpr std::uint8_t kSlopeOnGround = 3;

    /// Extra data stored by elevated paths and queues.
    struct PathElevation
    {
        std::uint8_t slope = 0;                      ///< Slope id as stored
        std::optional<std::int32_t> base_height { }; ///< Empty when the stored value is the unset marker
        std::int32_t quantised_height = 0;           ///< Height in steps; the step size is not confirmed
        std::uint64_t scenery_ref = 0;               ///< Placed object that draws the piece
        bool underground = false;
    };

    struct PathQueueInfo
    {
        std::uint8_t start_direction = 0;
        std::uint8_t end_direction = 0;
        std::uint64_t fence_ref = 0;
        std::string fence_symbol { };  ///< Asset symbol of the fence along the queue's sides, empty when it does not resolve
        std::string fence_overlay { }; ///< Overlay holding it, as PlacedObject::overlay
        std::array<std::int32_t, 3> fence_colours { };
        std::uint64_t queue_line_ref = 0;
    };

    struct PathTile
    {
        std::uint64_t id = 0;
        PathKind kind = PathKind::Unknown;
        std::uint8_t raw_kind = 0; ///< The stored kind byte
        std::uint8_t col = 0;
        std::uint8_t row = 0;
        std::uint8_t direction = kNoDirection; ///< Quarter turns 0..3, or kNoDirection
        std::uint8_t surface_variant = 0;      ///< Stored as 0, 1 or 255
        std::uint64_t surface_ref = 0;
        std::string surface_name { };                                   ///< System name of the surface style, empty when unresolved
        std::optional<std::array<std::int32_t, 3>> surface_colours { }; ///< Picked colours of a queue's ground surface
        std::optional<PathElevation> elevation { };                     ///< Present for elevated paths and queues
        std::optional<PathQueueInfo> queue { };                         ///< Present for queues
    };

    struct QueueLine
    {
        std::uint64_t id = 0;
        std::uint64_t head_ref = 0;
        std::int32_t length = 0;
        std::uint64_t ride_ref = 0;
        std::int32_t station_index = 0;
    };

    // ---- Placed scenery ------------------------------------------------------------------------------------

    inline constexpr std::int32_t kUnsetBaseHeight = -32768;

    struct PlacedObject
    {
        std::uint64_t id = 0;
        std::uint64_t definition_ref = 0; ///< Database entry that names the asset
        std::string symbol { };           ///< Asset symbol from that entry, empty when it does not resolve
        /// The overlay that holds the asset, from the same entry: a path relative to the game's folder, without
        /// the ".common.ovl" ending, with either kind of slash; empty when it does not resolve.
        std::string overlay { };
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::optional<std::int32_t> base_height { }; ///< Whole-unit base height; empty when stored as unset
        std::int32_t rotation = 0;                   ///< Quarter turns 0..3
        std::int32_t quadrant = 0;                   ///< Sub-tile corner 0..3
        std::optional<float> height_adjust { };      ///< Only in newer layouts
        std::uint64_t owner_ref = 0;                 ///< Own id when free-standing, else the parent piece
        std::array<std::int32_t, 3> colours { };
        std::int32_t height_offset = 0; ///< Unit not confirmed
        std::int32_t frame_offset = 0;
        bool force_absolute_height = false;
        bool hidden = false;
        bool fixture = false;         ///< Built by the park itself (fences round rides, queues and the park), not stored; not clicked
        std::int32_t break_flags = 0; ///< Raw bit set; individual bits not confirmed
        float break_time = 0;
        std::uint32_t behaviour_count = 0;

        [[nodiscard]] bool free_standing() const noexcept
        {
            return owner_ref == id;
        }
    };

    /// What a litter bin placed in the park holds.
    struct BinState
    {
        std::uint64_t object_ref = 0; ///< The placed object that is the bin
        float capacity = 0;
        float contents = 0;                    ///< In the same measure as `capacity`
        std::optional<float> last_emptied { }; ///< Park time, as the clock counts it; empty when never emptied
    };

    /// A square of a pool that holds water. Its pieces stand in a stack from the floor up to the rim: the rim
    /// piece at the surface height, the floor `depth` below it.
    struct PoolWater
    {
        std::int32_t col = 0;
        std::int32_t row = 0;
        std::int32_t surface = 0; ///< Whole units, as placed objects' heights
        std::int32_t depth = 0;   ///< Whole units
    };

    /// A waterfall: water pouring from a higher lake over a cliff edge into a lower one.
    struct Waterfall
    {
        WorldPoint3 lip_left { };  ///< One end of the edge the water pours over, at the upper lake's level
        WorldPoint3 lip_right { }; ///< The other end
        WorldPoint3 top { };       ///< A point of the upper lake behind the edge; its height is that lake's level
        WorldPoint3 bottom { };    ///< Where the water lands; its height is the lower lake's level
        WorldPoint3 flow { };      ///< Unit direction the water runs across the ground, from `top` to `bottom`
    };

    /// What a sign placed in the park says.
    struct SignState
    {
        std::uint64_t object_ref = 0; ///< The placed object that is the sign
        std::int32_t kind = 0;        ///< 0 shows `text`; 1 forbids entry
        std::string text { };         ///< Written by the player
    };

    // ---- Environment ---------------------------------------------------------------------------------------

    enum class WeatherType : std::int32_t
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

    [[nodiscard]] WeatherType weather_type_from(std::int32_t raw) noexcept;
    [[nodiscard]] const char* to_string(WeatherType t) noexcept;

    struct FogSetting
    {
        WorldColour colour { };
        float far = 0;
        float near = 0;
        float shadow_damper = 0;
    };

    struct Weather
    {
        WeatherType type = WeatherType::Unknown;
        std::int32_t raw_type = 0;
        WeatherType forecast = WeatherType::Unknown;
        std::int32_t raw_forecast = 0;
        std::int32_t climate = 0; ///< Opaque id, meaning not confirmed
        float temperature = 0;    ///< Degrees Celsius
        float forecast_temperature = 0;
        float forecast_delay = 0;
        float sequence_delay = 0;
        float wind_speed = 0;
        float wind_acceleration = 0;
        std::int32_t overlay = 0;
        std::int32_t forecast_overlay = 0;
        std::int32_t colour = 0;
        std::int32_t forecast_colour = 0;
        std::int32_t sound = 0;
        std::int32_t forecast_sound = 0;
        std::optional<float> thunder_time { };
        std::optional<FogSetting> fog { };
        std::optional<std::array<WorldColour, 2>> cloud_colours { };
        std::optional<std::array<WorldColour, 3>> light_colours { };
    };

    struct WorldTime
    {
        float day_fraction = 0; ///< 0..1 through the day
        float time = 0;         ///< Seconds accumulated
        float time_origin = 0;  ///< Reference stamp for `time`
        std::optional<std::int32_t> day_night_mode { };
        std::optional<float> day_fraction_as_rendered { };
    };

    struct Camera
    {
        WorldPoint3 position { };
        WorldPoint3 orientation { }; ///< Three stored floats; likely Euler angles in radians, not confirmed
    };

    // ---- Model and loader ----------------------------------------------------------------------------------

    enum class WorldIssueKind : std::uint8_t
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
        std::size_t count = 0;
        std::uint64_t first_object = 0; ///< Id of the first object affected, 0 when not tied to one
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
        std::vector<std::uint64_t> benches { }; ///< The placed objects that are benches guests can sit on
        std::vector<PoolWater> pool_water { };
        std::vector<Waterfall> waterfalls { };
        std::optional<Weather> weather { };
        std::optional<std::int32_t> sky_background { }; ///< Background (sky box) index
        std::optional<WorldTime> time { };
        std::optional<Camera> camera { };
        std::vector<WorldIssue> issues { };

        [[nodiscard]] std::size_t issue_count(WorldIssueKind k) const noexcept;
    };

    /// Builds the world model from a read save. Fails only when an object cannot be decoded. Malformed blobs and
    /// inconsistent data are reported in `issues` and leave the affected part empty.
    [[nodiscard]] std::expected<WorldModel, Error> load_world(const SaveFile& file, const ObjectIndex& index);

} // namespace rct3::save
