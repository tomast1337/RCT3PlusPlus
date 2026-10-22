// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/world.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <string_view>
#include <unordered_map>

namespace rct3::save
{

    namespace
    {

        // The only game-derived identifiers this file keys on: class and attribute names of the save format.
        namespace names
        {
            constexpr std::string_view kTerrainClass = "RCT3Terrain";
            constexpr std::string_view kTerrainBlob = "EngineTerrain";
            constexpr std::string_view kSkirtBlob = "SkirtTrees";
            constexpr std::string_view kBlockingBlob = "BlockingScenery";
            constexpr std::string_view kWaterClass = "WaterManager";
            constexpr std::string_view kWaterBlob = "WaterManager";

            constexpr std::string_view kPathClasses[] = { "PathTile", "PathGround", "PathFlying", "PathQueue" };
            constexpr std::string_view kPathCol = "ColIndex";
            constexpr std::string_view kPathRow = "RowIndex";
            constexpr std::string_view kPathDirection = "Direction";
            constexpr std::string_view kPathType = "PathType";
            constexpr std::string_view kPathVariant = "SurfaceType";
            constexpr std::string_view kPathSurface = "Surface";
            constexpr std::string_view kPathSlope = "SlopeType";
            constexpr std::string_view kPathBaseHeight = "BaseHeight";
            constexpr std::string_view kPathQuantised = "QuantisedHeight";
            constexpr std::string_view kPathScenery = "SceneryItem";
            constexpr std::string_view kPathUnderground = "UndergroundFlag";
            constexpr std::string_view kPathStartDir = "StartDirection";
            constexpr std::string_view kPathEndDir = "EndDirection";
            constexpr std::string_view kPathFence = "FenceEntry";
            constexpr std::string_view kPathFenceColours = "FenceFlexiColours";
            constexpr std::string_view kPathQueueLine = "QueueLine";

            constexpr std::string_view kQueueLineClass = "QueueLine";
            constexpr std::string_view kQueueHead = "HeadTile";
            constexpr std::string_view kQueueSize = "QueueSize";
            constexpr std::string_view kQueueRide = "RideInstance";
            constexpr std::string_view kQueueStation = "StationIndex";

            constexpr std::string_view kSceneryClass = "SceneryItem";
            constexpr std::string_view kSceneryData = "SceneryItemDataField";
            constexpr std::string_view kDataCol = "POSX";
            constexpr std::string_view kDataRow = "POSZ";
            constexpr std::string_view kDataHeight = "HEIGHT";
            constexpr std::string_view kDataDirection = "DIRECTION";
            constexpr std::string_view kDataCorner = "CORNER";
            constexpr std::string_view kDataAdjust = "HEIGHTADJUST";
            constexpr std::string_view kSceneryDefinition = "DATABASEENTRY";
            // A saved design's scenery (its ride's doors): the scenery fields with the definition under another name.
            constexpr std::string_view kDesignSceneryClass = "SceneryItemPlacementSingle";
            constexpr std::string_view kDesignSceneryDefinition = "SIDDatabaseEntry";
            constexpr std::string_view kSceneryOwner = "Owner";
            constexpr std::string_view kSceneryColours = "FlexiColourField";
            constexpr std::string_view kSceneryHeightOffset = "HEIGHTOFFSET";
            constexpr std::string_view kSceneryFrameOffset = "FRAMEOFFSET";
            constexpr std::string_view kSceneryAbsolute = "FORCEABSOLUTEHEIGHT";
            constexpr std::string_view kSceneryHidden = "IsHidden";
            constexpr std::string_view kSceneryBreakFlags = "BREAKFLAGS";
            constexpr std::string_view kSceneryBreakTime = "BREAKTIME";
            constexpr std::string_view kSceneryBehaviours = "BehaviourArray";
            constexpr std::string_view kColour0 = "COL0";
            constexpr std::string_view kColour1 = "COL1";
            constexpr std::string_view kColour2 = "COL2";
            constexpr std::string_view kEntrySymbol = "SYMBOLNAME";
            constexpr std::string_view kEntryOverlay = "OVERLAYFILENAME";
            constexpr std::string_view kEntrySystemName = "SystemName";
            constexpr std::string_view kEntryQueueType = "QueueType";
            constexpr std::string_view kSurfaceColours = "Colours";

            constexpr std::string_view kBinClass = "LitterBinBehaviour";
            constexpr std::string_view kSignClass = "SignBehaviour";
            constexpr std::string_view kBenchClass = "ParkBenchBehaviour";
            constexpr std::string_view kBehaviourOwner = "\"SceneryItem\""; ///< The quotes are part of the stored name
            constexpr std::string_view kBinCapacity = "Capacity";
            constexpr std::string_view kBinContents = "Contents";
            constexpr std::string_view kBinEmptied = "LastEmptied";
            constexpr std::string_view kSignKind = "SignType";
            constexpr std::string_view kSignText = "UserText";

            constexpr std::string_view kWeatherClass = "Weather";
            constexpr std::string_view kSkyClass = "Sky";
            constexpr std::string_view kSkyIndex = "BackgroundSave";
            constexpr std::string_view kTimeClass = "GameTime";
            constexpr std::string_view kCameraClass = "MainCamera";

            // Pool squares. Type 0 holds water; the others are decks and steps round it. Prototypes are not in
            // the park.
            constexpr std::string_view kPoolClass = "PoolSegment";
            constexpr std::string_view kPoolCol = "Col";
            constexpr std::string_view kPoolRow = "Row";
            constexpr std::string_view kPoolType = "SegmentType";
            constexpr std::string_view kPoolSurface = "SurfaceHeight";
            constexpr std::string_view kPoolDepth = "Depth";
            constexpr std::string_view kPoolPrototype = "Prototype";
            constexpr std::string_view kCameraPos = "Pos";
            constexpr std::string_view kCameraOr = "Or";
        } // namespace names

        // ---- Byte reading ----------------------------------------------------------------------------------------

        std::uint32_t rd_u32(std::span<const std::byte> b, std::size_t at) noexcept
        {
            std::uint32_t v;
            std::memcpy(&v, b.data() + at, 4);
            return std::endian::native == std::endian::little ? v : std::byteswap(v);
        }

        float rd_f32(std::span<const std::byte> b, std::size_t at) noexcept
        {
            return std::bit_cast<float>(rd_u32(b, at));
        }

        std::uint64_t rd_u64(std::span<const std::byte> b, std::size_t at) noexcept
        {
            return static_cast<std::uint64_t>(rd_u32(b, at)) | (static_cast<std::uint64_t>(rd_u32(b, at + 4)) << 32);
        }

        std::uint8_t rd_u8(std::span<const std::byte> b, std::size_t at) noexcept
        {
            return static_cast<std::uint8_t>(b[at]);
        }

        constexpr std::size_t kTerrainHeader = 18;
        constexpr std::size_t kTerrainTailSlack = 8;

    } // namespace

    // ---- Enums and small helpers -----------------------------------------------------------------------------

    const char* to_string(Ownership o) noexcept
    {
        switch (o)
        {
            case Ownership::Unowned:
                return "unowned";
            case Ownership::ForSale:
                return "for sale";
            case Ownership::Owned:
                return "owned";
            case Ownership::ForSaleRights:
                return "for sale (rights)";
            case Ownership::OwnedRights:
                return "owned (rights)";
            case Ownership::Locked:
                return "locked";
        }
        return "?";
    }

    const char* to_string(PathKind k) noexcept
    {
        switch (k)
        {
            case PathKind::Ground:
                return "ground";
            case PathKind::Flying:
                return "flying";
            case PathKind::Queue:
                return "queue";
            case PathKind::Unknown:
                return "unknown";
        }
        return "?";
    }

    WeatherType weather_type_from(std::int32_t raw) noexcept
    {
        if (raw >= 0 && raw <= 6)
            return static_cast<WeatherType>(raw);
        return WeatherType::Unknown;
    }

    const char* to_string(WeatherType t) noexcept
    {
        switch (t)
        {
            case WeatherType::Sunny:
                return "sunny";
            case WeatherType::SunnyCloudy:
                return "sunny-cloudy";
            case WeatherType::Cloudy:
                return "cloudy";
            case WeatherType::LightRain:
                return "light rain";
            case WeatherType::HeavyRain:
                return "heavy rain";
            case WeatherType::Thunderstorm:
                return "thunderstorm";
            case WeatherType::Snowstorm:
                return "snowstorm";
            case WeatherType::Unknown:
                return "unknown";
        }
        return "?";
    }

    const char* to_string(WorldIssueKind k) noexcept
    {
        switch (k)
        {
            case WorldIssueKind::MissingTerrain:
                return "missing terrain";
            case WorldIssueKind::BadTerrainBlob:
                return "bad terrain blob";
            case WorldIssueKind::BadWaterBlob:
                return "bad water blob";
            case WorldIssueKind::BadSkirtBlob:
                return "bad skirt-tree blob";
            case WorldIssueKind::DuplicateSingleton:
                return "duplicate singleton object";
            case WorldIssueKind::NonFiniteHeight:
                return "non-finite terrain height";
            case WorldIssueKind::WaterSizeMismatch:
                return "water map size differs from terrain";
            case WorldIssueKind::WaterTileOutOfRange:
                return "water tile outside map";
            case WorldIssueKind::WaterBadTriangle:
                return "water triangle index not 0/1";
            case WorldIssueKind::WaterMaskMismatch:
                return "water mask differs from recomputed";
            case WorldIssueKind::PathTileOutOfRange:
                return "path tile outside map";
            case WorldIssueKind::PathKindMismatch:
                return "path kind disagrees with class";
            case WorldIssueKind::PathSceneryMismatch:
                return "path scenery link on another tile";
            case WorldIssueKind::PathSceneryDangling:
                return "path scenery link not found";
            case WorldIssueKind::ObjectOutOfRange:
                return "placed object outside map";
            case WorldIssueKind::Count_:
                break;
        }
        return "?";
    }

    std::size_t WorldModel::issue_count(WorldIssueKind k) const noexcept
    {
        for (const auto& i : issues)
        {
            if (i.kind == k)
                return i.count;
        }
        return 0;
    }

    Ownership TerrainCell::ownership() const noexcept
    {
        if (has(TileFlag::NeverPurchasable))
            return Ownership::Locked;
        const bool rights = has(TileFlag::RightsOnly);
        if (has(TileFlag::Owned))
            return rights ? Ownership::OwnedRights : Ownership::Owned;
        if (has(TileFlag::ForSale))
            return rights ? Ownership::ForSaleRights : Ownership::ForSale;
        return Ownership::Unowned;
    }

    float TerrainCell::min_height() const noexcept
    {
        return *std::ranges::min_element(corners);
    }
    float TerrainCell::max_height() const noexcept
    {
        return *std::ranges::max_element(corners);
    }

    std::size_t WaterLayer::triangle_count() const noexcept
    {
        std::size_t n = 0;
        for (const auto& b : bodies)
            n += b.tris.size();
        return n;
    }

    // ---- Terrain ---------------------------------------------------------------------------------------------

    namespace
    {

        // Tile and in-tile fraction for a world coordinate; false when outside the grid.
        struct TilePos
        {
            int col = 0;
            int row = 0;
            float fx = 0;
            float fz = 0;
        };

        std::optional<TilePos> locate(const TerrainGrid& g, float x, float z) noexcept
        {
            if (g.empty() || !(g.tile_size_x > 0) || !(g.tile_size_z > 0))
                return std::nullopt;
            const float tx = (x - g.origin_x) / g.tile_size_x;
            const float tz = (z - g.origin_z) / g.tile_size_z;
            if (!(tx >= 0) || !(tz >= 0) || !(tx <= static_cast<float>(g.width)) || !(tz <= static_cast<float>(g.height)))
                return std::nullopt;
            const int col = std::min(static_cast<int>(tx), g.width - 1);
            const int row = std::min(static_cast<int>(tz), g.height - 1);
            return TilePos { .col = col, .row = row, .fx = tx - static_cast<float>(col), .fz = tz - static_cast<float>(row) };
        }

    } // namespace

    std::optional<float> TerrainGrid::height_at(float x, float z) const noexcept
    {
        const auto p = locate(*this, x, z);
        if (!p)
            return std::nullopt;
        const auto& c = at(p->col, p->row).corners;
        if (p->fx + p->fz <= 1.0f)
            return c[0] + p->fx * (c[1] - c[0]) + p->fz * (c[2] - c[0]);
        return c[3] + (1.0f - p->fx) * (c[2] - c[3]) + (1.0f - p->fz) * (c[1] - c[3]);
    }

    std::optional<float> TerrainGrid::height_bilinear_at(float x, float z) const noexcept
    {
        const auto p = locate(*this, x, z);
        if (!p)
            return std::nullopt;
        const auto& c = at(p->col, p->row).corners;
        const float near_edge = c[0] + p->fx * (c[1] - c[0]);
        const float far_edge = c[2] + p->fx * (c[3] - c[2]);
        return near_edge + p->fz * (far_edge - near_edge);
    }

    std::expected<TerrainGrid, Error> decode_terrain(std::span<const std::byte> blob)
    {
        if (blob.size() < kTerrainHeader)
            return std::unexpected(Error::Truncated);
        const std::size_t w = rd_u8(blob, 0);
        const std::size_t h = rd_u8(blob, 1);
        if (w == 0 || h == 0)
            return std::unexpected(Error::ImplausibleCount);
        const float origin_x = rd_f32(blob, 2);
        const float origin_z = rd_f32(blob, 6);
        const float tile_size_x = rd_f32(blob, 10);
        const float tile_size_z = rd_f32(blob, 14);
        if (!std::isfinite(origin_x) || !std::isfinite(origin_z) || !std::isfinite(tile_size_x) || !std::isfinite(tile_size_z) || !(tile_size_x > 0) || !(tile_size_z > 0))
            return std::unexpected(Error::BadHeader);

        const std::size_t n = w * h; // at most kMaxTerrainCells
        const std::size_t avail = blob.size() - kTerrainHeader;
        const auto fits = [&](std::size_t stride) { return avail >= n * stride && avail - n * stride <= kTerrainTailSlack; };
        if (!fits(24) && !fits(20))
            return std::unexpected(avail < n * 24 ? Error::Truncated : Error::ImplausibleCount);
        const std::size_t stride = fits(24) ? 24 : 20;

        TerrainGrid g {
            .width = static_cast<int>(w),
            .height = static_cast<int>(h),
            .origin_x = origin_x,
            .origin_z = origin_z,
            .tile_size_x = tile_size_x,
            .tile_size_z = tile_size_z,
            .layout = stride == 24 ? TerrainCellLayout::Cell24 : TerrainCellLayout::Cell20,
            .trailing_bytes = avail - n * stride,
        };
        g.cells.reserve(n);
        for (std::size_t i = 0; i < n; ++i)
        {
            const std::size_t at = kTerrainHeader + i * stride;
            g.cells.push_back(
                {
                    .corners = { rd_f32(blob, at), rd_f32(blob, at + 4), rd_f32(blob, at + 8), rd_f32(blob, at + 12) },
                    .surface = rd_u8(blob, at + 16),
                    .cliff = rd_u8(blob, at + 17),
                    .flags = stride == 24 ? rd_u8(blob, at + 20) : static_cast<std::uint8_t>(rd_u8(blob, at + 19) & 0x0f),
                });
        }
        return g;
    }

    // ---- Water -----------------------------------------------------------------------------------------------

    std::expected<WaterLayer, Error> decode_water(std::span<const std::byte> blob)
    {
        if (blob.size() < 6)
            return std::unexpected(Error::Truncated);
        WaterLayer layer { .width = rd_u8(blob, 0), .height = rd_u8(blob, 1) };
        const std::uint32_t count = rd_u32(blob, 2);
        std::size_t pos = 6;
        if (count > (blob.size() - pos) / 8)
            return std::unexpected(Error::ImplausibleCount);
        layer.bodies.reserve(count);
        for (std::uint32_t b = 0; b < count; ++b)
        {
            if (blob.size() - pos < 8)
                return std::unexpected(Error::Truncated);
            const float level = rd_f32(blob, pos);
            const std::uint32_t n = rd_u32(blob, pos + 4);
            pos += 8;
            if (n > (blob.size() - pos) / 4)
                return std::unexpected(Error::ImplausibleCount);
            WaterBody body { .level = level, .tris = std::vector<WaterTri>(n) };
            for (std::uint32_t i = 0; i < n; ++i, pos += 4)
            {
                body.tris[i] = { rd_u8(blob, pos), rd_u8(blob, pos + 1), rd_u8(blob, pos + 2), rd_u8(blob, pos + 3) };
            }
            layer.bodies.push_back(std::move(body));
        }
        if (pos != blob.size())
            return std::unexpected(Error::TrailingData);
        return layer;
    }

    std::optional<std::uint8_t> expected_water_mask(const TerrainGrid& grid, float level, Tile tile, int triangle) noexcept
    {
        const auto [col, row] = tile;
        if (!grid.contains(col, row) || (triangle != 0 && triangle != 1))
            return std::nullopt;
        const auto& c = grid.at(col, row).corners;
        const std::array<float, 3> corner = triangle == 0 ? std::array { c[0], c[1], c[2] } : std::array { c[3], c[2], c[1] };
        std::uint8_t mask = 0;
        for (std::size_t i = 0; i < 3; ++i)
        {
            if (corner[i] < level)
                mask = static_cast<std::uint8_t>(mask | (1u << i));
        }
        return mask;
    }

    // ---- Skirt trees -----------------------------------------------------------------------------------------

    std::expected<std::vector<RawSkirtTree>, Error> decode_skirt_trees(std::span<const std::byte> blob)
    {
        if (blob.size() < 4)
            return std::unexpected(Error::Truncated);
        const std::uint32_t count = rd_u32(blob, 0);
        if (count > (blob.size() - 4) / 20)
            return std::unexpected(Error::ImplausibleCount);
        if (blob.size() != 4 + static_cast<std::size_t>(count) * 20)
            return std::unexpected(Error::TrailingData);
        std::vector<RawSkirtTree> out(count);
        for (std::uint32_t i = 0; i < count; ++i)
        {
            const std::size_t at = 4 + static_cast<std::size_t>(i) * 20;
            out[i].scenery_ref = rd_u64(blob, at);
            out[i].position = { rd_f32(blob, at + 8), rd_f32(blob, at + 12), rd_f32(blob, at + 16) };
        }
        return out;
    }

    // ---- Loader ----------------------------------------------------------------------------------------------

    namespace
    {

        enum class Kind : std::uint8_t
        {
            None,
            Terrain,
            Water,
            Path,
            QueueLine,
            Scenery,
            Bin,
            Sign,
            Bench,
            Weather,
            Sky,
            Time,
            Camera,
            Pool,
        };

        // Typed reads over a decoded value. Missing attributes and attributes kept raw give empty results.
        std::optional<std::int64_t> get_int(std::optional<const Value&> v) noexcept
        {
            if (v && v->kind() == ValueKind::Integer)
                return v->as_integer();
            return std::nullopt;
        }
        std::optional<float> get_float(std::optional<const Value&> v) noexcept
        {
            if (v && v->kind() == ValueKind::Float)
                return v->as_float();
            return std::nullopt;
        }
        std::optional<bool> get_bool(std::optional<const Value&> v) noexcept
        {
            if (v && v->kind() == ValueKind::Bool)
                return v->as_bool();
            return std::nullopt;
        }
        std::optional<std::uint64_t> get_ref(std::optional<const Value&> v) noexcept
        {
            if (v && v->kind() == ValueKind::Reference)
                return v->as_reference();
            return std::nullopt;
        }
        std::optional<std::span<const std::byte>> get_blob(std::optional<const Value&> v) noexcept
        {
            if (v && v->kind() == ValueKind::Raw)
                return v->raw();
            return std::nullopt;
        }

        std::int32_t int_or(std::optional<const Value&> v, std::int32_t d = 0) noexcept
        {
            const auto x = get_int(v);
            return x ? static_cast<std::int32_t>(*x) : d;
        }
        float float_or(std::optional<const Value&> v, float d = 0) noexcept
        {
            return get_float(v).value_or(d);
        }

        std::string string_or(std::optional<const Value&> v)
        {
            if (v && v->kind() == ValueKind::String)
                return std::string(v->text());
            return { };
        }

        std::uint8_t byte_or(std::optional<const Value&> v, std::uint8_t d = 0) noexcept
        {
            const auto x = get_int(v);
            return x ? static_cast<std::uint8_t>(*x) : d;
        }

        /// The three picked colours of a colour struct, or nothing when `v` is not one.
        std::optional<std::array<std::int32_t, 3>> get_colours(std::optional<const Value&> v) noexcept
        {
            if (!v || v->kind() != ValueKind::Struct)
                return std::nullopt;
            return std::array { int_or(v->field(names::kColour0)), int_or(v->field(names::kColour1)), int_or(v->field(names::kColour2)) };
        }

        WorldColour colour_of(std::optional<const Value&> v) noexcept
        {
            if (!v || v->kind() != ValueKind::Struct)
                return { };
            return WorldColour { .r = float_or(v->field("Red")), .g = float_or(v->field("Green")), .b = float_or(v->field("Blue")) };
        }

        class Issues
        {
        public:
            void add(WorldIssueKind k, std::uint64_t object = 0) noexcept
            {
                auto& i = items_[static_cast<std::size_t>(k)];
                if (i.count++ == 0)
                    i.first_object = object;
                i.kind = k;
            }
            [[nodiscard]] std::vector<WorldIssue> take() const
            {
                std::vector<WorldIssue> out;
                for (const auto& i : items_)
                {
                    if (i.count)
                        out.push_back(i);
                }
                return out;
            }

        private:
            std::array<WorldIssue, static_cast<std::size_t>(WorldIssueKind::Count_)> items_ { };
        };

        // Resolves strings out of other objects, with a cache so repeated definitions are decoded once.
        class Resolver
        {
        public:
            Resolver(const SaveFile& file, const ObjectIndex& index)
                : file_(file)
                , index_(index)
            {
            }

            const std::string& symbol(std::uint64_t ref)
            {
                return lookup(symbols_, ref, [&](const DecodedObject& o) { return text_of(o, names::kEntrySymbol); });
            }

            const std::string& overlay(std::uint64_t ref)
            {
                return lookup(overlays_, ref, [&](const DecodedObject& o) { return text_of(o, names::kEntryOverlay); });
            }

            // System name of a path surface. A queue ground surface names its queue style through one more hop.
            const std::string& surface(std::uint64_t ref)
            {
                return lookup(surfaces_, ref, [&](const DecodedObject& o) {
                    auto s = text_of(o, names::kEntrySystemName);
                    if (!s.empty())
                        return s;
                    if (const auto next = get_ref(o.find(names::kEntryQueueType)); next && *next != 0 && *next != o.id)
                    {
                        if (const auto obj = index_.find(*next))
                        {
                            if (const auto d = decode_object(file_, *obj))
                                return text_of(*d, names::kEntrySystemName);
                        }
                    }
                    return std::string();
                });
            }

            // The colours a queue ground surface picks, when the surface is one.
            std::optional<std::array<std::int32_t, 3>> surface_colours(std::uint64_t ref)
            {
                if (ref == 0)
                    return std::nullopt;
                if (const auto it = colours_.find(ref); it != colours_.end())
                    return it->second;
                std::optional<std::array<std::int32_t, 3>> out;
                if (const auto obj = index_.find(ref))
                {
                    if (const auto d = decode_object(file_, *obj))
                        out = get_colours(d->find(names::kSurfaceColours));
                }
                return colours_.emplace(ref, out).first->second;
            }

        private:
            static std::string text_of(const DecodedObject& o, std::string_view attr)
            {
                std::optional<const Value&> v = o.find(attr);
                if (v && v->kind() == ValueKind::String)
                    return std::string(v->text());
                return { };
            }

            template<typename F> const std::string& lookup(std::unordered_map<std::uint64_t, std::string>& cache, std::uint64_t ref, F&& make)
            {
                static const std::string empty;
                if (ref == 0)
                    return empty;
                if (const auto it = cache.find(ref); it != cache.end())
                    return it->second;
                std::string s;
                if (const auto obj = index_.find(ref))
                {
                    if (const auto d = decode_object(file_, *obj))
                        s = make(*d);
                }
                return cache.emplace(ref, std::move(s)).first->second;
            }

            const SaveFile& file_;
            const ObjectIndex& index_;
            std::unordered_map<std::uint64_t, std::string> symbols_;
            std::unordered_map<std::uint64_t, std::string> overlays_;
            std::unordered_map<std::uint64_t, std::string> surfaces_;
            std::unordered_map<std::uint64_t, std::optional<std::array<std::int32_t, 3>>> colours_;
        };

        PathKind path_kind_of(std::uint8_t raw) noexcept
        {
            switch (raw)
            {
                case 0:
                    return PathKind::Ground;
                case 1:
                    return PathKind::Flying;
                case 2:
                    return PathKind::Queue;
                default:
                    return PathKind::Unknown;
            }
        }

        // The kind a path class should carry, for the cross-check.
        std::optional<PathKind> class_path_kind(std::string_view cls) noexcept
        {
            if (cls == "PathGround")
                return PathKind::Ground;
            if (cls == "PathFlying")
                return PathKind::Flying;
            if (cls == "PathQueue")
                return PathKind::Queue;
            return std::nullopt; // the plain tile class carries any kind
        }

        PathTile read_path(const DecodedObject& o, Resolver& res)
        {
            const std::uint8_t raw_kind = byte_or(o.find(names::kPathType), 0);
            const std::uint64_t surface_ref = get_ref(o.find(names::kPathSurface)).value_or(0);
            PathTile t {
                .id = o.id,
                .kind = path_kind_of(raw_kind),
                .raw_kind = raw_kind,
                .col = byte_or(o.find(names::kPathCol)),
                .row = byte_or(o.find(names::kPathRow)),
                .direction = byte_or(o.find(names::kPathDirection), kNoDirection),
                .surface_variant = byte_or(o.find(names::kPathVariant)),
                .surface_ref = surface_ref,
                .surface_name = res.surface(surface_ref),
                .surface_colours = res.surface_colours(surface_ref),
            };

            std::optional<const Value&> slope = o.find(names::kPathSlope);
            std::optional<const Value&> quant = o.find(names::kPathQuantised);
            std::optional<const Value&> base = o.find(names::kPathBaseHeight);
            if (slope || quant || base)
            {
                const auto b = get_int(base);
                t.elevation = PathElevation {
                    .slope = byte_or(slope),
                    .base_height = b && *b != -1 ? std::optional(static_cast<std::int32_t>(*b)) : std::nullopt,
                    .quantised_height = int_or(quant),
                    .scenery_ref = get_ref(o.find(names::kPathScenery)).value_or(0),
                    .underground = get_bool(o.find(names::kPathUnderground)).value_or(false),
                };
            }
            std::optional<const Value&> fence = o.find(names::kPathFence);
            std::optional<const Value&> line = o.find(names::kPathQueueLine);
            if (fence || line || o.find(names::kPathStartDir))
            {
                const std::uint64_t fence_ref = get_ref(fence).value_or(0);
                t.queue = PathQueueInfo {
                    .start_direction = byte_or(o.find(names::kPathStartDir)),
                    .end_direction = byte_or(o.find(names::kPathEndDir)),
                    .fence_ref = fence_ref,
                    .fence_symbol = res.symbol(fence_ref),
                    .fence_overlay = res.overlay(fence_ref),
                    .fence_colours = get_colours(o.find(names::kPathFenceColours)).value_or(std::array<std::int32_t, 3> { }),
                    .queue_line_ref = get_ref(line).value_or(0),
                };
            }
            return t;
        }

        PlacedObject read_scenery(const DecodedObject& o)
        {
            PlacedObject p {
                .id = o.id,
                .definition_ref = get_ref(o.find(names::kSceneryDefinition)).value_or(get_ref(o.find(names::kDesignSceneryDefinition)).value_or(0)),
                .owner_ref = get_ref(o.find(names::kSceneryOwner)).value_or(0),
                .colours = get_colours(o.find(names::kSceneryColours)).value_or(std::array<std::int32_t, 3> { }),
                .height_offset = int_or(o.find(names::kSceneryHeightOffset)),
                .frame_offset = int_or(o.find(names::kSceneryFrameOffset)),
                .force_absolute_height = get_bool(o.find(names::kSceneryAbsolute)).value_or(false),
                .hidden = get_bool(o.find(names::kSceneryHidden)).value_or(false),
                .break_flags = int_or(o.find(names::kSceneryBreakFlags)),
                .break_time = float_or(o.find(names::kSceneryBreakTime)),
            };
            if (std::optional<const Value&> d = o.find(names::kSceneryData); d && d->kind() == ValueKind::Struct)
            {
                p.col = int_or(d->field(names::kDataCol));
                p.row = int_or(d->field(names::kDataRow));
                if (const auto h = get_int(d->field(names::kDataHeight)); h && *h != kUnsetBaseHeight)
                    p.base_height = static_cast<std::int32_t>(*h);
                p.rotation = int_or(d->field(names::kDataDirection));
                p.quadrant = int_or(d->field(names::kDataCorner));
                p.height_adjust = get_float(d->field(names::kDataAdjust));
            }
            if (std::optional<const Value&> b = o.find(names::kSceneryBehaviours); b && b->kind() == ValueKind::List)
                p.behaviour_count = static_cast<std::uint32_t>(b->items().size());
            return p;
        }

        Weather read_weather(const DecodedObject& o)
        {
            const std::int32_t raw_type = int_or(o.find("Type"));
            const std::int32_t raw_forecast = int_or(o.find("ForecastType"));
            Weather w {
                .type = weather_type_from(raw_type),
                .raw_type = raw_type,
                .forecast = weather_type_from(raw_forecast),
                .raw_forecast = raw_forecast,
                .climate = int_or(o.find("Climate")),
                .temperature = float_or(o.find("Temperature")),
                .forecast_temperature = float_or(o.find("ForecastTemperature")),
                .forecast_delay = float_or(o.find("ForecastDelay")),
                .sequence_delay = float_or(o.find("SequenceDelay")),
                .wind_speed = float_or(o.find("WindSpeed")),
                .wind_acceleration = float_or(o.find("WindAccel")),
                .overlay = int_or(o.find("Overlay")),
                .forecast_overlay = int_or(o.find("ForecastOverlay")),
                .colour = int_or(o.find("Colour")),
                .forecast_colour = int_or(o.find("ForecastColour")),
                .sound = int_or(o.find("SoundType")),
                .forecast_sound = int_or(o.find("ForecastSound")),
                .thunder_time = get_float(o.find("ThunderTime")),
            };
            if (std::optional<const Value&> f = o.find("FogSetting"); f && f->kind() == ValueKind::Struct)
            {
                w.fog = FogSetting {
                    .colour = colour_of(f->field("FColourValue")),
                    .far = float_or(f->field("Far")),
                    .near = float_or(f->field("Near")),
                    .shadow_damper = float_or(f->field("ShadowDamper")),
                };
            }
            if (std::optional<const Value&> c = o.find("CloudSetting"); c && c->kind() == ValueKind::Struct)
            {
                w.cloud_colours = { colour_of(c->field("ColourValue1")), colour_of(c->field("ColourValue2")) };
            }
            if (std::optional<const Value&> c = o.find("ColourSetting"); c && c->kind() == ValueKind::Struct)
            {
                w.light_colours = { colour_of(c->field("AColourValue")), colour_of(c->field("DColourValue")), colour_of(c->field("SColourValue")) };
            }
            return w;
        }

        WorldTime read_time(const DecodedObject& o)
        {
            const auto mode = get_int(o.find("DayNightMode"));
            return WorldTime {
                .day_fraction = float_or(o.find("DayNightTime")),
                .time = float_or(o.find("Time")),
                .time_origin = float_or(o.find("ZeroTime")),
                .day_night_mode = mode ? std::optional(static_cast<std::int32_t>(*mode)) : std::nullopt,
                .day_fraction_as_rendered = get_float(o.find("DayNightTimeAsRendered")),
            };
        }

        WorldPoint3 point_of(std::optional<const Value&> v) noexcept
        {
            WorldPoint3 p;
            if (v && v->floats().size() >= 3)
            {
                p = { v->floats()[0], v->floats()[1], v->floats()[2] };
            }
            return p;
        }

    } // namespace

    std::expected<WorldModel, Error> load_world(const SaveFile& file, const ObjectIndex& index)
    {
        WorldModel m;
        Issues issues;
        Resolver resolver(file, index);

        std::vector<Kind> kinds(file.classes.size(), Kind::None);
        std::vector<std::optional<PathKind>> path_class_kind(file.classes.size());
        for (std::size_t i = 0; i < file.classes.size(); ++i)
        {
            const auto name = file.classes[i].name;
            if (name == names::kTerrainClass)
                kinds[i] = Kind::Terrain;
            else if (name == names::kWaterClass)
                kinds[i] = Kind::Water;
            else if (name == names::kQueueLineClass)
                kinds[i] = Kind::QueueLine;
            else if (name == names::kSceneryClass || name == names::kDesignSceneryClass)
                kinds[i] = Kind::Scenery;
            else if (name == names::kBinClass)
                kinds[i] = Kind::Bin;
            else if (name == names::kSignClass)
                kinds[i] = Kind::Sign;
            else if (name == names::kBenchClass)
                kinds[i] = Kind::Bench;
            else if (name == names::kWeatherClass)
                kinds[i] = Kind::Weather;
            else if (name == names::kSkyClass)
                kinds[i] = Kind::Sky;
            else if (name == names::kTimeClass)
                kinds[i] = Kind::Time;
            else if (name == names::kCameraClass)
                kinds[i] = Kind::Camera;
            else if (name == names::kPoolClass)
                kinds[i] = Kind::Pool;
            else if (std::ranges::find(names::kPathClasses, name) != std::end(names::kPathClasses))
            {
                kinds[i] = Kind::Path;
                path_class_kind[i] = class_path_kind(name);
            }
        }

        bool have_terrain = false, have_water = false, have_weather = false, have_sky = false, have_time = false, have_camera = false;
        std::vector<std::uint64_t> path_scenery; // parallel to m.paths: link id or 0

        const auto singleton = [&](bool& seen, const DecodedObject& o) {
            if (seen)
            {
                issues.add(WorldIssueKind::DuplicateSingleton, o.id);
                return false;
            }
            seen = true;
            return true;
        };

        for (const auto& obj : file.objects)
        {
            const Kind kind = kinds[obj.class_index];
            if (kind == Kind::None)
                continue;
            auto decoded = decode_object(file, obj);
            if (!decoded)
                return std::unexpected(decoded.error());
            const DecodedObject& o = *decoded;

            switch (kind)
            {
                case Kind::None:
                    break;
                case Kind::Terrain:
                {
                    if (!singleton(have_terrain, o))
                        break;
                    if (const auto blob = get_blob(o.find(names::kTerrainBlob)))
                    {
                        if (auto g = decode_terrain(*blob))
                            m.terrain = std::move(*g);
                        else
                            issues.add(WorldIssueKind::BadTerrainBlob, o.id);
                    }
                    else
                    {
                        issues.add(WorldIssueKind::BadTerrainBlob, o.id);
                    }
                    if (const auto blob = get_blob(o.find(names::kSkirtBlob)))
                    {
                        if (auto trees = decode_skirt_trees(*blob))
                        {
                            m.skirt_trees.reserve(trees->size());
                            for (const auto& t : *trees)
                            {
                                m.skirt_trees.push_back({ t.scenery_ref, t.position, { } });
                            }
                        }
                        else
                        {
                            issues.add(WorldIssueKind::BadSkirtBlob, o.id);
                        }
                    }
                    if (const auto blob = get_blob(o.find(names::kBlockingBlob)))
                    {
                        m.blocking_scenery.assign(blob->begin(), blob->end());
                    }
                    break;
                }
                case Kind::Water:
                {
                    if (!singleton(have_water, o))
                        break;
                    if (const auto blob = get_blob(o.find(names::kWaterBlob)))
                    {
                        if (auto w = decode_water(*blob))
                            m.water = std::move(*w);
                        else
                            issues.add(WorldIssueKind::BadWaterBlob, o.id);
                    }
                    else
                    {
                        issues.add(WorldIssueKind::BadWaterBlob, o.id);
                    }
                    break;
                }
                case Kind::Path:
                {
                    PathTile t = read_path(o, resolver);
                    const auto expected_kind = path_class_kind[obj.class_index];
                    if (expected_kind && *expected_kind != t.kind)
                        issues.add(WorldIssueKind::PathKindMismatch, o.id);
                    path_scenery.push_back(t.elevation ? t.elevation->scenery_ref : 0);
                    m.paths.push_back(std::move(t));
                    break;
                }
                case Kind::QueueLine:
                {
                    m.queue_lines.push_back(
                        {
                            .id = o.id,
                            .head_ref = get_ref(o.find(names::kQueueHead)).value_or(0),
                            .length = int_or(o.find(names::kQueueSize)),
                            .ride_ref = get_ref(o.find(names::kQueueRide)).value_or(0),
                            .station_index = int_or(o.find(names::kQueueStation)),
                        });
                    break;
                }
                case Kind::Scenery:
                    m.objects.push_back(read_scenery(o));
                    break;
                case Kind::Bin:
                {
                    // Never emptied is stored as a time before the park began.
                    const auto emptied = get_float(o.find(names::kBinEmptied));
                    m.bins.push_back(
                        {
                            .object_ref = get_ref(o.find(names::kBehaviourOwner)).value_or(0),
                            .capacity = float_or(o.find(names::kBinCapacity)),
                            .contents = float_or(o.find(names::kBinContents)),
                            .last_emptied = emptied && *emptied >= 0 ? emptied : std::nullopt,
                        });
                    break;
                }
                case Kind::Sign:
                {
                    m.signs.push_back(
                        {
                            .object_ref = get_ref(o.find(names::kBehaviourOwner)).value_or(0),
                            .kind = int_or(o.find(names::kSignKind)),
                            .text = string_or(o.find(names::kSignText)),
                        });
                    break;
                }
                case Kind::Bench:
                    if (const auto owner = get_ref(o.find(names::kBehaviourOwner)))
                        m.benches.push_back(*owner);
                    break;
                case Kind::Weather:
                    if (singleton(have_weather, o))
                        m.weather = read_weather(o);
                    break;
                case Kind::Sky:
                    if (singleton(have_sky, o))
                    {
                        if (const auto v = get_int(o.find(names::kSkyIndex)))
                            m.sky_background = static_cast<std::int32_t>(*v);
                    }
                    break;
                case Kind::Time:
                    if (singleton(have_time, o))
                        m.time = read_time(o);
                    break;
                case Kind::Camera:
                    if (singleton(have_camera, o))
                    {
                        m.camera = Camera { point_of(o.find(names::kCameraPos)), point_of(o.find(names::kCameraOr)) };
                    }
                    break;
                case Kind::Pool:
                    if (int_or(o.find(names::kPoolType), -1) == 0 && !get_bool(o.find(names::kPoolPrototype)).value_or(false))
                        m.pool_water.push_back({ int_or(o.find(names::kPoolCol)), int_or(o.find(names::kPoolRow)), int_or(o.find(names::kPoolSurface)), int_or(o.find(names::kPoolDepth)) });
                    break;
            }
        }

        if (!have_terrain)
            issues.add(WorldIssueKind::MissingTerrain);

        // Asset symbols.
        for (auto& p : m.objects)
        {
            p.symbol = resolver.symbol(p.definition_ref);
            p.overlay = resolver.overlay(p.definition_ref);
        }
        for (auto& t : m.skirt_trees)
        {
            // A skirt tree refers to a placed object, whose own definition names the asset.
            if (const auto obj = index.find(t.scenery_ref))
            {
                if (const auto d = decode_object(file, *obj))
                {
                    t.symbol = resolver.symbol(get_ref(d->find(names::kSceneryDefinition)).value_or(0));
                }
            }
        }

        // Validation against the terrain grid.
        const TerrainGrid& g = m.terrain;
        for (const auto& c : g.cells)
        {
            for (const float h : c.corners)
            {
                if (!std::isfinite(h))
                {
                    issues.add(WorldIssueKind::NonFiniteHeight);
                    break;
                }
            }
        }
        if (have_water && !g.empty() && (m.water.width != g.width || m.water.height != g.height))
            issues.add(WorldIssueKind::WaterSizeMismatch);
        for (const auto& body : m.water.bodies)
        {
            for (const auto& t : body.tris)
            {
                if (t.triangle > 1)
                {
                    issues.add(WorldIssueKind::WaterBadTriangle);
                    continue;
                }
                if (g.empty())
                    continue;
                if (!g.contains(t.col, t.row))
                {
                    issues.add(WorldIssueKind::WaterTileOutOfRange);
                    continue;
                }
                const auto mask = expected_water_mask(g, body.level, { t.col, t.row }, t.triangle);
                if (mask && *mask != t.mask)
                    issues.add(WorldIssueKind::WaterMaskMismatch);
            }
        }

        std::unordered_map<std::uint64_t, std::pair<std::int32_t, std::int32_t>> placed;
        placed.reserve(m.objects.size());
        for (const auto& p : m.objects)
            placed.emplace(p.id, std::pair { p.col, p.row });
        for (const auto& p : m.objects)
        {
            if (!g.empty() && !g.contains(p.col, p.row))
                issues.add(WorldIssueKind::ObjectOutOfRange, p.id);
        }
        for (std::size_t i = 0; i < m.paths.size(); ++i)
        {
            const auto& t = m.paths[i];
            if (!g.empty() && !g.contains(t.col, t.row))
                issues.add(WorldIssueKind::PathTileOutOfRange, t.id);
            // Only elevated paths are checked: queue pieces carry link ids that mostly do not resolve.
            if (t.kind == PathKind::Flying && path_scenery[i] != 0)
            {
                const auto it = placed.find(path_scenery[i]);
                if (it == placed.end())
                    issues.add(WorldIssueKind::PathSceneryDangling, t.id);
                else if (it->second.first != t.col || it->second.second != t.row)
                    issues.add(WorldIssueKind::PathSceneryMismatch, t.id);
            }
        }

        m.issues = issues.take();
        return m;
    }

} // namespace rct3::save
