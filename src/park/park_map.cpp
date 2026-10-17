// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/park_map.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <set>
#include <utility>

namespace rct3::park
{

    namespace
    {

        // The palette is ours: soft land tints that keep the coloured things on top easy to tell apart.
        constexpr std::array<MapRgba, kMapLayerCount> kLayerColours { {
            { 0x8f, 0xae, 0x62, 255 }, // Relief: the land's middle tint
            { 0x4a, 0x3c, 0x2a, 150 }, // Contours
            { 0x4f, 0x9f, 0xd6, 255 }, // Water
            { 0x1b, 0x27, 0x33, 120 }, // ParkLand: the shade outside the park
            { 0xf6, 0xf1, 0xe6, 255 }, // Paths
            { 0x2f, 0x5d, 0x3a, 150 }, // Scenery
            { 0xe0, 0x45, 0x7b, 255 }, // Rides
            { 0xf2, 0x8c, 0x28, 255 }, // Food
            { 0x1f, 0xb5, 0xb0, 255 }, // Drinks
            { 0x9b, 0x59, 0xd0, 255 }, // Souvenirs
            { 0x3f, 0x7f, 0xe0, 255 }, // Information
            { 0xe5, 0x39, 0x35, 255 }, // FirstAid
            { 0x3c, 0xa5, 0x5c, 255 }, // CashMachines
            { 0x7d, 0x8d, 0xa8, 255 }, // Toilets
            { 0xff, 0xd6, 0x00, 255 }, // Guests
            { 0xff, 0xff, 0xff, 255 }, // Staff
            { 0x3f, 0x9f, 0xff, 90 },  // Patrols
        } };

        // Heights from the lowest to the highest ground of the park, as tints.
        struct Tint
        {
            float at;
            glm::vec3 colour;
        };
        constexpr std::array kHeightTints {
            Tint { 0.00f, { 0x5d / 255.0f, 0x8c / 255.0f, 0x4f / 255.0f } }, Tint { 0.35f, { 0x8f / 255.0f, 0xae / 255.0f, 0x62 / 255.0f } }, Tint { 0.60f, { 0xc4 / 255.0f, 0xb8 / 255.0f, 0x7c / 255.0f } }, Tint { 0.82f, { 0xb2 / 255.0f, 0x96 / 255.0f, 0x74 / 255.0f } }, Tint { 1.00f, { 0xee / 255.0f, 0xe8 / 255.0f, 0xde / 255.0f } },
        };
        /// Below this spread of heights a park counts as flat: its land gets the middle tint.
        constexpr float kFlatSpread = 1.0f;
        /// How much of the land's colour comes from its height rather than its surface.
        constexpr float kHeightTintShare = 0.35f;

        // Hill shading: light from the north-west, high up. Slopes are made steeper than they are so gentle
        // hills still show.
        const glm::vec3 kLight = glm::normalize(glm::vec3(-0.6f, 1.2f, 0.6f));
        constexpr float kSlopeExaggeration = 2.5f;
        constexpr float kShadeLow = 0.55f;
        constexpr float kShadeHigh = 1.2f;

        // Contours: as many lines as reads well over the park's spread of heights, every fifth stronger.
        constexpr std::array kContourSteps { 1.0f, 2.0f, 4.0f, 5.0f, 10.0f, 20.0f };
        constexpr float kMostContours = 24;
        constexpr int kMajorEvery = 5;
        constexpr float kContourWidth = 1.0f; ///< Pixels
        constexpr MapRgba kMajorContour { 0x3b, 0x2e, 0x1e, 200 };

        // Water: lighter where shallow. Its edge fades over this much height so shores are smooth.
        constexpr glm::vec3 kShallow { 0x7e / 255.0f, 0xc8 / 255.0f, 0xe6 / 255.0f };
        constexpr glm::vec3 kDeep { 0x2a / 255.0f, 0x6a / 255.0f, 0xa8 / 255.0f };
        constexpr float kDeepAt = 6.0f;
        constexpr float kShoreFade = 0.25f;
        constexpr float kWaterAlpha = 0.92f;

        // The park's land: outside it shaded, land for sale striped, its edge a bright line.
        constexpr MapRgba kForSale { 0xff, 0xe0, 0x8a, 90 };
        constexpr std::int32_t kStripePeriod = 6; ///< Pixels
        constexpr MapRgba kParkEdge { 0xff, 0xf6, 0xd5, 235 };
        constexpr float kParkEdgeWidth = 0.22f; ///< Of a tile, at least one pixel

        // Paths: a light line with a dark casing from each tile's middle to its neighbours'.
        constexpr float kPathRadius = 0.3f; ///< Of a tile
        constexpr float kPathCasing = 0.9f; ///< Pixels
        constexpr MapRgba kPathCase { 0x4d, 0x47, 0x3d, 200 };
        constexpr MapRgba kQueue { 0xa8, 0xd0, 0xff, 255 }; ///< Apart from the guests' yellow

        // Scenery no larger than this many tiles across is drawn as a dot, at least this big in pixels.
        constexpr float kRoundUpTo = 1.5f;
        constexpr float kSmallestDot = 1.0f;

        // Stalls and facilities: a badge this big, of a tile and at least in pixels, with a light rim.
        constexpr float kBadgeRadius = 0.6f;
        constexpr float kSmallestBadge = 3.0f;
        constexpr float kBadgeRim = 1.2f;
        constexpr MapRgba kBadgeRimColour { 0xff, 0xff, 0xff, 240 };

        // People: dots with a dark rim, at least this big whatever the scale.
        constexpr float kPersonRadius = 0.28f; ///< Of a tile
        constexpr float kSmallestPerson = 1.3f;
        constexpr float kPersonRim = 0.8f; ///< Pixels
        constexpr MapRgba kGuestRim { 0x5a, 0x40, 0x00, 230 };
        constexpr MapRgba kStaffRim { 0x15, 0x4f, 0xa8, 255 };

        // Stall and facility types, as their items store them.
        constexpr std::uint32_t kFoodType = 7;
        constexpr std::uint32_t kDrinkType = 8;
        constexpr std::uint32_t kSouvenirType = 9;
        constexpr std::uint32_t kToiletType = 11;
        constexpr std::uint32_t kCashType = 12;
        constexpr std::uint32_t kFirstAidType = 13;
        constexpr std::uint32_t kInformationType = 14;

        std::size_t pixel(const MapPicture& p, std::int32_t x, std::int32_t y)
        {
            return (static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width) + static_cast<std::size_t>(x)) * 4;
        }

        MapPicture blank(std::int32_t width, std::int32_t height)
        {
            MapPicture p;
            p.width = width;
            p.height = height;
            p.rgba.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height) * 4, 0);
            return p;
        }

        std::uint8_t byte(float v)
        {
            return static_cast<std::uint8_t>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
        }

        void put(MapPicture& p, std::int32_t x, std::int32_t y, glm::vec3 colour, float alpha)
        {
            const std::size_t i = pixel(p, x, y);
            p.rgba[i] = byte(colour.r);
            p.rgba[i + 1] = byte(colour.g);
            p.rgba[i + 2] = byte(colour.b);
            p.rgba[i + 3] = byte(alpha);
        }

        /// Lays `colour` over the pixel, `coverage` of it covered.
        void blend(MapPicture& p, std::int32_t x, std::int32_t y, MapRgba colour, float coverage)
        {
            const float a = coverage * static_cast<float>(colour.a) / 255.0f;
            if (a <= 0)
                return;
            const std::size_t i = pixel(p, x, y);
            const float below = static_cast<float>(p.rgba[i + 3]) / 255.0f;
            const float out = a + below * (1 - a);
            if (out <= 0)
                return;
            const std::array<std::uint8_t, 3> top { colour.r, colour.g, colour.b };
            for (std::size_t c = 0; c < 3; ++c)
            {
                const float mixed = (static_cast<float>(top[c]) * a + static_cast<float>(p.rgba[i + c]) * below * (1 - a)) / out;
                p.rgba[i + c] = static_cast<std::uint8_t>(std::clamp(mixed + 0.5f, 0.0f, 255.0f));
            }
            p.rgba[i + 3] = byte(out);
        }

        /// Lays all of `top` over `p`, which must be the same size.
        void blend(MapPicture& p, const MapPicture& top)
        {
            for (std::int32_t y = 0; y < p.height; ++y)
            {
                for (std::int32_t x = 0; x < p.width; ++x)
                {
                    const MapRgba c = top.at(x, y);
                    if (c.a != 0)
                        blend(p, x, y, { c.r, c.g, c.b, 255 }, static_cast<float>(c.a) / 255.0f);
                }
            }
        }

        /// The pixels a box from `low` to `high` touches, cut to the picture.
        struct Span
        {
            std::int32_t x0, y0, x1, y1;
        };
        Span pixels_of(const MapPicture& p, glm::vec2 low, glm::vec2 high)
        {
            return { std::max(0, static_cast<std::int32_t>(std::floor(low.x))), std::max(0, static_cast<std::int32_t>(std::floor(low.y))), std::min(p.width, static_cast<std::int32_t>(std::ceil(high.x)) + 1), std::min(p.height, static_cast<std::int32_t>(std::ceil(high.y)) + 1) };
        }

        /// Fills the convex shape with corners `corners` (in pixels, in order either way round), its edges
        /// smoothed over a pixel.
        void fill_convex(MapPicture& p, const std::array<glm::vec2, 4>& corners, MapRgba colour)
        {
            glm::vec2 low = corners[0];
            glm::vec2 high = corners[0];
            float twice_area = 0;
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                low = glm::min(low, corners[i]);
                high = glm::max(high, corners[i]);
                const glm::vec2 a = corners[i];
                const glm::vec2 b = corners[(i + 1) % corners.size()];
                twice_area += a.x * b.y - b.x * a.y;
            }
            const float turn = twice_area >= 0 ? 1.0f : -1.0f;
            // Each edge as a line: inside where n . q + d > 0, n of length 1.
            std::array<glm::vec3, 4> edges { };
            for (std::size_t i = 0; i < corners.size(); ++i)
            {
                const glm::vec2 a = corners[i];
                const glm::vec2 b = corners[(i + 1) % corners.size()];
                glm::vec2 n { -(b.y - a.y), b.x - a.x };
                const float length = glm::length(n);
                if (length < 1e-6f)
                {
                    edges[i] = { 0, 0, std::numeric_limits<float>::max() };
                    continue;
                }
                n = n * (turn / length);
                edges[i] = { n.x, n.y, -glm::dot(n, a) };
            }
            const Span s = pixels_of(p, low - 1.0f, high + 1.0f);
            for (std::int32_t y = s.y0; y < s.y1; ++y)
            {
                for (std::int32_t x = s.x0; x < s.x1; ++x)
                {
                    const glm::vec2 q { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f };
                    float inside = std::numeric_limits<float>::max();
                    for (const glm::vec3& e : edges)
                        inside = std::min(inside, e.x * q.x + e.y * q.y + e.z);
                    const float coverage = std::clamp(inside + 0.5f, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(p, x, y, colour, coverage);
                }
            }
        }

        /// Fills the points within `radius` pixels of the segment from `a` to `b`, its edge smoothed.
        void fill_capsule(MapPicture& p, glm::vec2 a, glm::vec2 b, float radius, MapRgba colour)
        {
            const Span s = pixels_of(p, glm::min(a, b) - radius - 1.0f, glm::max(a, b) + radius + 1.0f);
            const glm::vec2 ab = b - a;
            const float length2 = glm::dot(ab, ab);
            for (std::int32_t y = s.y0; y < s.y1; ++y)
            {
                for (std::int32_t x = s.x0; x < s.x1; ++x)
                {
                    const glm::vec2 q { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f };
                    const float t = length2 > 0 ? std::clamp(glm::dot(q - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
                    const float distance = glm::length(q - (a + ab * t));
                    const float coverage = std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(p, x, y, colour, coverage);
                }
            }
        }

        /// The pixel point of the park point (x, z) on a picture `scale` pixels per tile.
        glm::vec2 to_pixels(const save::TerrainGrid& grid, std::int32_t scale, glm::vec2 park)
        {
            const glm::vec2 m = to_map(map_frame(grid), park);
            return { m.x * static_cast<float>(grid.width * scale), m.y * static_cast<float>(grid.height * scale) };
        }

        /// The middle of tile (col, row) in pixels.
        glm::vec2 tile_middle(const save::TerrainGrid& grid, std::int32_t scale, std::int32_t col, std::int32_t row)
        {
            const float s = static_cast<float>(scale);
            return { (static_cast<float>(col) + 0.5f) * s, (static_cast<float>(grid.height - 1 - row) + 0.5f) * s };
        }

        glm::vec3 height_tint(float t)
        {
            for (std::size_t i = 1; i < kHeightTints.size(); ++i)
            {
                if (t <= kHeightTints[i].at)
                {
                    const Tint& a = kHeightTints[i - 1];
                    const Tint& b = kHeightTints[i];
                    return glm::mix(a.colour, b.colour, (t - a.at) / (b.at - a.at));
                }
            }
            return kHeightTints.back().colour;
        }

        float contour_step(float spread)
        {
            for (const float step : kContourSteps)
            {
                if (spread / step <= kMostContours)
                    return step;
            }
            return kContourSteps.back();
        }

        glm::vec3 colour_of(MapRgba c)
        {
            return { static_cast<float>(c.r) / 255.0f, static_cast<float>(c.g) / 255.0f, static_cast<float>(c.b) / 255.0f };
        }

        bool owned(save::Ownership o)
        {
            return o == save::Ownership::Owned || o == save::Ownership::OwnedRights;
        }

        bool for_sale(save::Ownership o)
        {
            return o == save::Ownership::ForSale || o == save::Ownership::ForSaleRights;
        }

        void draw_park_land(MapPicture& p, const save::TerrainGrid& grid, std::int32_t scale)
        {
            const MapRgba outside = map_layer_colour(MapLayer::ParkLand);
            for (std::int32_t row = 0; row < grid.height; ++row)
            {
                for (std::int32_t col = 0; col < grid.width; ++col)
                {
                    const save::Ownership o = grid.at(col, row).ownership();
                    if (owned(o))
                        continue;
                    const std::int32_t x0 = col * scale;
                    const std::int32_t y0 = (grid.height - 1 - row) * scale;
                    for (std::int32_t y = y0; y < y0 + scale; ++y)
                    {
                        for (std::int32_t x = x0; x < x0 + scale; ++x)
                        {
                            if (!for_sale(o))
                                blend(p, x, y, outside, 1);
                            else if ((x + y) % kStripePeriod < kStripePeriod / 2)
                                blend(p, x, y, kForSale, 1);
                        }
                    }
                }
            }
            // The edge: wherever owned land meets land that is not.
            const float s = static_cast<float>(scale);
            const float half = std::max(0.5f, kParkEdgeWidth * s / 2);
            const auto is_owned = [&](std::int32_t col, std::int32_t row) { return grid.contains(col, row) && owned(grid.at(col, row).ownership()); };
            for (std::int32_t row = 0; row < grid.height; ++row)
            {
                for (std::int32_t col = 0; col < grid.width; ++col)
                {
                    if (!is_owned(col, row))
                        continue;
                    const float left = static_cast<float>(col) * s;
                    const float top = static_cast<float>(grid.height - 1 - row) * s;
                    const std::array<std::pair<glm::vec2, glm::vec2>, 4> sides { {
                        { { left, top }, { left + s, top } },         // North: row + 1
                        { { left, top + s }, { left + s, top + s } }, // South: row - 1
                        { { left, top }, { left, top + s } },         // West: col - 1
                        { { left + s, top }, { left + s, top + s } }, // East: col + 1
                    } };
                    const std::array<std::pair<std::int32_t, std::int32_t>, 4> next { { { col, row + 1 }, { col, row - 1 }, { col - 1, row }, { col + 1, row } } };
                    for (std::size_t i = 0; i < sides.size(); ++i)
                    {
                        if (!is_owned(next[i].first, next[i].second))
                            fill_capsule(p, sides[i].first, sides[i].second, half, kParkEdge);
                    }
                }
            }
        }

        void draw_paths(MapPicture& p, const save::TerrainGrid& grid, std::int32_t scale, std::span<const MapPathTile> paths)
        {
            std::set<std::pair<std::int32_t, std::int32_t>> at;
            for (const MapPathTile& t : paths)
                at.emplace(t.col, t.row);
            const float radius = std::max(1.0f, kPathRadius * static_cast<float>(scale));
            // Casings first, so where lines join the light parts run on without a seam.
            for (int pass = 0; pass < 2; ++pass)
            {
                for (const MapPathTile& t : paths)
                {
                    const MapRgba colour = pass == 0 ? kPathCase : (t.queue ? kQueue : map_layer_colour(MapLayer::Paths));
                    const float r = pass == 0 ? radius + kPathCasing : radius;
                    const glm::vec2 middle = tile_middle(grid, scale, t.col, t.row);
                    fill_capsule(p, middle, middle, r, colour);
                    // Each join once, from the tile to its north and east neighbours.
                    for (const auto& [dc, dr] : { std::pair { 0, 1 }, std::pair { 1, 0 } })
                    {
                        if (at.contains({ t.col + dc, t.row + dr }))
                            fill_capsule(p, middle, tile_middle(grid, scale, t.col + dc, t.row + dr), r, colour);
                    }
                }
            }
        }

    } // namespace

    MapLayers all_map_layers()
    {
        MapLayers layers;
        layers.set();
        return layers;
    }

    MapRgba map_layer_colour(MapLayer layer)
    {
        return kLayerColours.at(static_cast<std::size_t>(layer));
    }

    MapLayer attraction_layer(std::optional<std::uint32_t> type_id)
    {
        switch (type_id.value_or(0))
        {
            case kFoodType:
                return MapLayer::Food;
            case kDrinkType:
                return MapLayer::Drinks;
            case kSouvenirType:
                return MapLayer::Souvenirs;
            case kToiletType:
                return MapLayer::Toilets;
            case kCashType:
                return MapLayer::CashMachines;
            case kFirstAidType:
                return MapLayer::FirstAid;
            case kInformationType:
                return MapLayer::Information;
            default:
                return MapLayer::Rides;
        }
    }

    MapFrame map_frame(const save::TerrainGrid& grid)
    {
        MapFrame f;
        f.x0 = grid.origin_x;
        f.z0 = grid.origin_z;
        f.width = std::max(1e-3f, static_cast<float>(grid.width) * grid.tile_size_x);
        f.depth = std::max(1e-3f, static_cast<float>(grid.height) * grid.tile_size_z);
        return f;
    }

    glm::vec2 to_map(const MapFrame& frame, glm::vec2 park)
    {
        return { (park.x - frame.x0) / frame.width, 1.0f - (park.y - frame.z0) / frame.depth };
    }

    glm::vec2 to_park(const MapFrame& frame, glm::vec2 map)
    {
        return { frame.x0 + map.x * frame.width, frame.z0 + (1.0f - map.y) * frame.depth };
    }

    MapFeatures map_features(const save::TerrainGrid& grid, std::span<const save::PathTile> paths, const save::WaterLayer& water)
    {
        MapFeatures f;
        std::set<std::pair<std::int32_t, std::int32_t>> seen;
        for (const save::PathTile& t : paths)
        {
            if (!grid.contains(t.col, t.row) || !seen.emplace(t.col, t.row).second)
                continue;
            f.paths.push_back({ t.col, t.row, t.kind == save::PathKind::Queue });
        }
        f.water.assign(grid.cells.size(), std::nullopt);
        for (const save::WaterBody& body : water.bodies)
        {
            for (const save::WaterTri& t : body.tris)
            {
                if (!grid.contains(t.col, t.row))
                    continue;
                auto& level = f.water[static_cast<std::size_t>(t.row) * static_cast<std::size_t>(grid.width) + t.col];
                level = std::max(level.value_or(body.level), body.level);
            }
        }
        return f;
    }

    std::array<glm::vec2, 4> ground_corners(std::span<const glm::vec3, 8> box)
    {
        // Bit 0 of an index picks the far x side, bit 2 the far z side: go round 0, 1, 5, 4.
        const auto xz = [&](std::size_t i) { return glm::vec2(box[i].x, box[i].z); };
        return { xz(0), xz(1), xz(5), xz(4) };
    }

    std::array<glm::vec2, 4> tile_corners(const save::TerrainGrid& grid, std::int32_t col, std::int32_t row)
    {
        const float x0 = grid.world_x(col);
        const float x1 = grid.world_x(col + 1);
        const float z0 = grid.world_z(row);
        const float z1 = grid.world_z(row + 1);
        return { glm::vec2 { x0, z0 }, glm::vec2 { x1, z0 }, glm::vec2 { x1, z1 }, glm::vec2 { x0, z1 } };
    }

    MapRgba MapPicture::at(std::int32_t x, std::int32_t y) const
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return { 0, 0, 0, 0 };
        const std::size_t i = pixel(*this, x, y);
        return { rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3] };
    }

    std::int32_t map_scale(const save::TerrainGrid& grid)
    {
        const std::int32_t across = std::max({ grid.width, grid.height, 1 });
        return std::clamp(kMapPixelsAcross / across, kMinMapScale, kMaxMapScale);
    }

    MapGround map_ground(const save::TerrainGrid& grid, const MapFeatures& features, std::int32_t scale)
    {
        MapGround g;
        g.scale = scale;
        const std::int32_t width = grid.width * scale;
        const std::int32_t height = grid.height * scale;
        g.relief = blank(width, height);
        g.flat = blank(width, height);
        g.contours = blank(width, height);
        g.water = blank(width, height);
        if (grid.empty())
            return g;

        // Heights at the tiles' corners, where tiles meeting at a corner disagree (at a cliff) their mean.
        const std::int32_t across = grid.width + 1;
        const std::int32_t down = grid.height + 1;
        std::vector<float> corner(static_cast<std::size_t>(across) * static_cast<std::size_t>(down), 0.0f);
        std::vector<float> count(corner.size(), 0.0f);
        const auto at = [&](std::int32_t i, std::int32_t j) { return static_cast<std::size_t>(std::clamp(j, 0, down - 1)) * static_cast<std::size_t>(across) + static_cast<std::size_t>(std::clamp(i, 0, across - 1)); };
        for (std::int32_t row = 0; row < grid.height; ++row)
        {
            for (std::int32_t col = 0; col < grid.width; ++col)
            {
                const auto& c = grid.at(col, row).corners;
                for (std::int32_t k = 0; k < 4; ++k)
                {
                    const std::size_t i = at(col + (k & 1), row + (k >> 1));
                    corner[i] += c[static_cast<std::size_t>(k)];
                    count[i] += 1;
                }
            }
        }
        for (std::size_t i = 0; i < corner.size(); ++i)
            corner[i] /= std::max(count[i], 1.0f);
        const auto [low_it, high_it] = std::ranges::minmax_element(corner);
        const float lowest = *low_it;
        const float spread = *high_it - lowest;

        // The light at each corner, from the slope round it; pixels take it smoothly from the corners, so
        // hills look round rather than cut into tiles.
        std::vector<float> light(corner.size());
        for (std::int32_t j = 0; j < down; ++j)
        {
            for (std::int32_t i = 0; i < across; ++i)
            {
                const float dx = (corner[at(i + 1, j)] - corner[at(i - 1, j)]) / (2 * grid.tile_size_x);
                const float dz = (corner[at(i, j + 1)] - corner[at(i, j - 1)]) / (2 * grid.tile_size_z);
                const glm::vec3 normal = glm::normalize(glm::vec3(-dx * kSlopeExaggeration, 1.0f, -dz * kSlopeExaggeration));
                light[at(i, j)] = std::clamp(glm::dot(normal, kLight) / kLight.y, kShadeLow, kShadeHigh);
            }
        }

        // Each tile's own colour: its surface's where known, else the land's middle tint.
        const glm::vec3 flat = colour_of(map_layer_colour(MapLayer::Relief));
        std::vector<glm::vec3> tile_colour(grid.cells.size(), flat);
        for (std::size_t i = 0; i < grid.cells.size(); ++i)
        {
            const std::size_t surface = grid.cells[i].surface;
            if (surface < features.surfaces.size() && features.surfaces[surface])
                tile_colour[i] = colour_of(*features.surfaces[surface]);
        }
        const auto tile_at = [&](std::int32_t col, std::int32_t row) { return tile_colour[static_cast<std::size_t>(std::clamp(row, 0, grid.height - 1)) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(std::clamp(col, 0, grid.width - 1))]; };

        const float step = contour_step(spread);
        const MapRgba minor = map_layer_colour(MapLayer::Contours);
        const float s = static_cast<float>(scale);
        for (std::int32_t y = 0; y < height; ++y)
        {
            // Where the pixel's middle lies in tiles, from the south-west corner of the map.
            const float fz = static_cast<float>(grid.height) - (static_cast<float>(y) + 0.5f) / s;
            const std::int32_t j = std::clamp(static_cast<std::int32_t>(fz), 0, grid.height - 1);
            const float v = fz - static_cast<float>(j);
            for (std::int32_t x = 0; x < width; ++x)
            {
                const float fx = (static_cast<float>(x) + 0.5f) / s;
                const std::int32_t i = std::clamp(static_cast<std::int32_t>(fx), 0, grid.width - 1);
                const float u = fx - static_cast<float>(i);
                const float h00 = corner[at(i, j)];
                const float h10 = corner[at(i + 1, j)];
                const float h01 = corner[at(i, j + 1)];
                const float h11 = corner[at(i + 1, j + 1)];
                const float here = glm::mix(glm::mix(h00, h10, u), glm::mix(h01, h11, u), v);
                const float shade = glm::mix(glm::mix(light[at(i, j)], light[at(i + 1, j)], u), glm::mix(light[at(i, j + 1)], light[at(i + 1, j + 1)], u), v);

                // The ground's colour, blended between the tiles' middles so surfaces meet softly, tinted
                // towards the height's colour so high and low ground tell apart.
                const float gx = fx - 0.5f;
                const float gz = fz - 0.5f;
                const std::int32_t c0 = static_cast<std::int32_t>(std::floor(gx));
                const std::int32_t r0 = static_cast<std::int32_t>(std::floor(gz));
                const float cu = gx - static_cast<float>(c0);
                const float cv = gz - static_cast<float>(r0);
                const glm::vec3 own = glm::mix(glm::mix(tile_at(c0, r0), tile_at(c0 + 1, r0), cu), glm::mix(tile_at(c0, r0 + 1), tile_at(c0 + 1, r0 + 1), cu), cv);
                const float t = spread < kFlatSpread ? kHeightTints[1].at : (here - lowest) / spread;
                put(g.relief, x, y, glm::min(glm::mix(own, height_tint(t), kHeightTintShare) * shade, glm::vec3(1.0f)), 1);
                put(g.flat, x, y, own, 1);

                // How many pixels this pixel lies from the nearest line, by how fast height changes here.
                const float east = glm::mix(h10 - h00, h11 - h01, v);
                const float north = glm::mix(h01 - h00, h11 - h10, u);
                const float per_pixel = std::sqrt(east * east + north * north) / s / step;
                if (per_pixel > 1e-4f)
                {
                    const float lines = (here - lowest) / step;
                    const float nearest = std::round(lines);
                    const float away = std::abs(lines - nearest) / per_pixel;
                    const float coverage = std::clamp(kContourWidth / 2 + 0.5f - away, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(g.contours, x, y, static_cast<int>(nearest) % kMajorEvery == 0 ? kMajorContour : minor, coverage);
                }

                const std::int32_t col = std::min(x / scale, grid.width - 1);
                const std::int32_t row = std::clamp(grid.height - 1 - y / scale, 0, grid.height - 1);
                const std::size_t tile = static_cast<std::size_t>(row) * static_cast<std::size_t>(grid.width) + static_cast<std::size_t>(col);
                const std::optional<float> level = tile < features.water.size() ? features.water[tile] : std::nullopt;
                if (level && *level > here)
                {
                    const float depth = *level - here;
                    const float alpha = kWaterAlpha * std::clamp(depth / kShoreFade, 0.0f, 1.0f);
                    put(g.water, x, y, glm::mix(kShallow, kDeep, std::clamp(depth / kDeepAt, 0.0f, 1.0f)), alpha);
                }
            }
        }
        return g;
    }

    MapPicture compose_map(const MapGround& ground, const save::TerrainGrid& grid, const MapFeatures& features, const MapLayers& layers)
    {
        MapPicture p = shows(layers, MapLayer::Relief) ? ground.relief : ground.flat;
        if (p.rgba.empty())
            return p;
        if (shows(layers, MapLayer::Water))
            blend(p, ground.water);
        if (shows(layers, MapLayer::Contours))
            blend(p, ground.contours);
        if (shows(layers, MapLayer::ParkLand))
            draw_park_land(p, grid, ground.scale);
        // Scenery under paths, attractions over both.
        const auto draw_shapes = [&](bool attractions) {
            for (const MapShape& s : features.shapes)
            {
                if ((s.layer != MapLayer::Scenery) != attractions || !shows(layers, s.layer))
                    continue;
                std::array<glm::vec2, 4> corners { };
                for (std::size_t i = 0; i < corners.size(); ++i)
                    corners[i] = to_pixels(grid, ground.scale, s.corners[i]);
                const float across = glm::length(corners[1] - corners[0]);
                const float along = glm::length(corners[2] - corners[1]);
                const glm::vec2 middle = (corners[0] + corners[1] + corners[2] + corners[3]) / 4.0f;
                if (s.layer != MapLayer::Scenery && s.layer != MapLayer::Rides)
                {
                    // Stalls and facilities are small but what players look for: a round badge with a rim.
                    const float radius = std::max(kSmallestBadge, kBadgeRadius * static_cast<float>(ground.scale));
                    fill_capsule(p, middle, middle, radius + kBadgeRim, kBadgeRimColour);
                    fill_capsule(p, middle, middle, radius, map_layer_colour(s.layer));
                }
                else if (s.layer == MapLayer::Scenery && std::max(across, along) <= kRoundUpTo * static_cast<float>(ground.scale))
                {
                    // Small things, mostly plants, as dots: a field of squares reads as noise.
                    fill_capsule(p, middle, middle, std::max(kSmallestDot, std::min(across, along) / 2), map_layer_colour(s.layer));
                }
                else
                    fill_convex(p, corners, map_layer_colour(s.layer));
            }
        };
        draw_shapes(false);
        if (shows(layers, MapLayer::Paths))
            draw_paths(p, grid, ground.scale, features.paths);
        draw_shapes(true);
        return p;
    }

    MapPicture people_picture(const save::TerrainGrid& grid, std::int32_t scale, std::span<const MapPerson> people, const MapLayers& layers, std::span<const glm::ivec2> patrol_tiles)
    {
        MapPicture p = blank(grid.width * scale, grid.height * scale);
        if (shows(layers, MapLayer::Patrols))
            for (const glm::ivec2 t : patrol_tiles)
            {
                if (!grid.contains(t.x, t.y))
                    continue;
                const std::int32_t x0 = t.x * scale;
                const std::int32_t y0 = (grid.height - 1 - t.y) * scale;
                for (std::int32_t y = y0; y < y0 + scale; ++y)
                    for (std::int32_t x = x0; x < x0 + scale; ++x)
                        blend(p, x, y, map_layer_colour(MapLayer::Patrols), 1);
            }
        const float radius = std::max(kSmallestPerson, kPersonRadius * static_cast<float>(scale));
        // Guests first: staff are fewer and should stay visible in a crowd.
        for (const bool staff : { false, true })
        {
            if (!shows(layers, staff ? MapLayer::Staff : MapLayer::Guests))
                continue;
            for (const MapPerson& person : people)
            {
                if (person.staff != staff)
                    continue;
                const glm::vec2 at = to_pixels(grid, scale, person.at);
                fill_capsule(p, at, at, radius + kPersonRim, staff ? kStaffRim : kGuestRim);
                fill_capsule(p, at, at, radius, map_layer_colour(staff ? MapLayer::Staff : MapLayer::Guests));
            }
        }
        return p;
    }

} // namespace rct3::park
