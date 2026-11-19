// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/park_map.hpp"

#include "core/types.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
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
            f32 at;
            glm::vec3 colour;
        };
        constexpr std::array kHeightTints {
            Tint { 0.00f, { 0x5d / 255.0f, 0x8c / 255.0f, 0x4f / 255.0f } }, Tint { 0.35f, { 0x8f / 255.0f, 0xae / 255.0f, 0x62 / 255.0f } }, Tint { 0.60f, { 0xc4 / 255.0f, 0xb8 / 255.0f, 0x7c / 255.0f } }, Tint { 0.82f, { 0xb2 / 255.0f, 0x96 / 255.0f, 0x74 / 255.0f } }, Tint { 1.00f, { 0xee / 255.0f, 0xe8 / 255.0f, 0xde / 255.0f } },
        };
        /// Below this spread of heights a park counts as flat: its land gets the middle tint.
        constexpr f32 kFlatSpread = 1.0f;
        /// How much of the land's colour comes from its height rather than its surface.
        constexpr f32 kHeightTintShare = 0.35f;

        // Hill shading: light from the north-west, high up. Slopes are made steeper than they are so gentle
        // hills still show.
        const glm::vec3 kLight = glm::normalize(glm::vec3(-0.6f, 1.2f, 0.6f));
        constexpr f32 kSlopeExaggeration = 2.5f;
        constexpr f32 kShadeLow = 0.55f;
        constexpr f32 kShadeHigh = 1.2f;

        // Contours: as many lines as reads well over the park's spread of heights, every fifth stronger.
        constexpr std::array kContourSteps { 1.0f, 2.0f, 4.0f, 5.0f, 10.0f, 20.0f };
        constexpr f32 kMostContours = 24;
        constexpr i32 kMajorEvery = 5;
        constexpr units::Pixels kContourWidth { 1.0f };
        constexpr MapRgba kMajorContour { 0x3b, 0x2e, 0x1e, 200 };

        // Water: lighter where shallow. Its edge fades over this much height so shores are smooth.
        constexpr glm::vec3 kShallow { 0x7e / 255.0f, 0xc8 / 255.0f, 0xe6 / 255.0f };
        constexpr glm::vec3 kDeep { 0x2a / 255.0f, 0x6a / 255.0f, 0xa8 / 255.0f };
        constexpr f32 kDeepAt = 6.0f;
        constexpr f32 kShoreFade = 0.25f;
        constexpr f32 kWaterAlpha = 0.92f;

        // The park's land: outside it shaded, land for sale striped, its edge a bright line.
        constexpr MapRgba kForSale { 0xff, 0xe0, 0x8a, 90 };
        constexpr i32 kStripePeriod = 6; ///< Pixels
        constexpr MapRgba kParkEdge { 0xff, 0xf6, 0xd5, 235 };
        constexpr f32 kParkEdgeWidth = 0.22f; ///< Of a tile, at least one pixel

        // Paths: a light line with a dark casing from each tile's middle to its neighbours'.
        constexpr f32 kPathRadius = 0.3f; ///< Of a tile
        constexpr units::Pixels kPathCasing { 0.9f };
        constexpr MapRgba kPathCase { 0x4d, 0x47, 0x3d, 200 };
        constexpr MapRgba kQueue { 0xa8, 0xd0, 0xff, 255 }; ///< Apart from the guests' yellow

        // Scenery no larger than this many tiles across is drawn as a dot, at least this big in pixels.
        constexpr f32 kRoundUpTo = 1.5f;
        constexpr f32 kSmallestDot = 1.0f;

        // Stalls and facilities: a badge this big, of a tile and at least in pixels, with a light rim.
        constexpr f32 kBadgeRadius = 0.6f;
        constexpr f32 kSmallestBadge = 3.0f;
        constexpr f32 kBadgeRim = 1.2f;
        constexpr MapRgba kBadgeRimColour { 0xff, 0xff, 0xff, 240 };

        // People: dots with a dark rim, at least this big whatever the scale.
        constexpr f32 kPersonRadius = 0.28f; ///< Of a tile
        constexpr f32 kSmallestPerson = 1.3f;
        constexpr units::Pixels kPersonRim { 0.8f };
        constexpr MapRgba kGuestRim { 0x5a, 0x40, 0x00, 230 };
        constexpr MapRgba kStaffRim { 0x15, 0x4f, 0xa8, 255 };

        // Stall and facility types, as their items store them.
        constexpr u32 kFoodType = 7;
        constexpr u32 kDrinkType = 8;
        constexpr u32 kSouvenirType = 9;
        constexpr u32 kToiletType = 11;
        constexpr u32 kCashType = 12;
        constexpr u32 kFirstAidType = 13;
        constexpr u32 kInformationType = 14;

        usize pixel(const MapPicture& p, i32 x, i32 y)
        {
            return (static_cast<usize>(y) * static_cast<usize>(p.width) + static_cast<usize>(x)) * 4;
        }

        MapPicture blank(i32 width, i32 height)
        {
            MapPicture p;
            p.width = width;
            p.height = height;
            p.rgba.assign(static_cast<usize>(width) * static_cast<usize>(height) * 4, 0);
            return p;
        }

        u8 byte(f32 v)
        {
            return static_cast<u8>(std::clamp(v * 255.0f + 0.5f, 0.0f, 255.0f));
        }

        void put(MapPicture& p, i32 x, i32 y, glm::vec3 colour, f32 alpha)
        {
            const usize i = pixel(p, x, y);
            p.rgba[i] = byte(colour.r);
            p.rgba[i + 1] = byte(colour.g);
            p.rgba[i + 2] = byte(colour.b);
            p.rgba[i + 3] = byte(alpha);
        }

        /// Lays `colour` over the pixel, `coverage` of it covered.
        void blend(MapPicture& p, i32 x, i32 y, MapRgba colour, f32 coverage)
        {
            const f32 a = coverage * static_cast<f32>(colour.a) / 255.0f;
            if (a <= 0)
                return;
            const usize i = pixel(p, x, y);
            const f32 below = static_cast<f32>(p.rgba[i + 3]) / 255.0f;
            const f32 out = a + below * (1 - a);
            if (out <= 0)
                return;
            const std::array<u8, 3> top { colour.r, colour.g, colour.b };
            for (usize c = 0; c < 3; ++c)
            {
                const f32 mixed = (static_cast<f32>(top[c]) * a + static_cast<f32>(p.rgba[i + c]) * below * (1 - a)) / out;
                p.rgba[i + c] = static_cast<u8>(std::clamp(mixed + 0.5f, 0.0f, 255.0f));
            }
            p.rgba[i + 3] = byte(out);
        }

        /// Lays all of `top` over `p`, which must be the same size.
        void blend(MapPicture& p, const MapPicture& top)
        {
            for (i32 y = 0; y < p.height; ++y)
            {
                for (i32 x = 0; x < p.width; ++x)
                {
                    const MapRgba c = top.at(x, y);
                    if (c.a != 0)
                        blend(p, x, y, { c.r, c.g, c.b, 255 }, static_cast<f32>(c.a) / 255.0f);
                }
            }
        }

        /// The pixels a box from `low` to `high` touches, cut to the picture.
        struct Span
        {
            i32 x0, y0, x1, y1;
        };
        Span pixels_of(const MapPicture& p, glm::vec2 low, glm::vec2 high)
        {
            return { std::max(0, static_cast<i32>(std::floor(low.x))), std::max(0, static_cast<i32>(std::floor(low.y))), std::min(p.width, static_cast<i32>(std::ceil(high.x)) + 1), std::min(p.height, static_cast<i32>(std::ceil(high.y)) + 1) };
        }

        /// Fills the convex shape with corners `corners` (in pixels, in order either way round), its edges
        /// smoothed over a pixel.
        void fill_convex(MapPicture& p, const std::array<glm::vec2, 4>& corners, MapRgba colour)
        {
            glm::vec2 low = corners[0];
            glm::vec2 high = corners[0];
            f32 twice_area = 0;
            for (usize i = 0; i < corners.size(); ++i)
            {
                low = glm::min(low, corners[i]);
                high = glm::max(high, corners[i]);
                const glm::vec2 a = corners[i];
                const glm::vec2 b = corners[(i + 1) % corners.size()];
                twice_area += a.x * b.y - b.x * a.y;
            }
            const f32 turn = twice_area >= 0 ? 1.0f : -1.0f;
            // Each edge as a line: inside where n . q + d > 0, n of length 1.
            std::array<glm::vec3, 4> edges { };
            for (usize i = 0; i < corners.size(); ++i)
            {
                const glm::vec2 a = corners[i];
                const glm::vec2 b = corners[(i + 1) % corners.size()];
                glm::vec2 n { -(b.y - a.y), b.x - a.x };
                const f32 length = glm::length(n);
                if (length < 1e-6f)
                {
                    edges[i] = { 0, 0, std::numeric_limits<f32>::max() };
                    continue;
                }
                n = n * (turn / length);
                edges[i] = { n.x, n.y, -glm::dot(n, a) };
            }
            const Span s = pixels_of(p, low - 1.0f, high + 1.0f);
            for (i32 y = s.y0; y < s.y1; ++y)
            {
                for (i32 x = s.x0; x < s.x1; ++x)
                {
                    const glm::vec2 q { static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f };
                    f32 inside = std::numeric_limits<f32>::max();
                    for (const glm::vec3& e : edges)
                        inside = std::min(inside, e.x * q.x + e.y * q.y + e.z);
                    const f32 coverage = std::clamp(inside + 0.5f, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(p, x, y, colour, coverage);
                }
            }
        }

        /// Fills the points within `radius` pixels of the segment from `a` to `b`, its edge smoothed.
        void fill_capsule(MapPicture& p, glm::vec2 a, glm::vec2 b, f32 radius, MapRgba colour)
        {
            const Span s = pixels_of(p, glm::min(a, b) - radius - 1.0f, glm::max(a, b) + radius + 1.0f);
            const glm::vec2 ab = b - a;
            const f32 length2 = glm::dot(ab, ab);
            for (i32 y = s.y0; y < s.y1; ++y)
            {
                for (i32 x = s.x0; x < s.x1; ++x)
                {
                    const glm::vec2 q { static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f };
                    const f32 t = length2 > 0 ? std::clamp(glm::dot(q - a, ab) / length2, 0.0f, 1.0f) : 0.0f;
                    const f32 distance = glm::length(q - (a + ab * t));
                    const f32 coverage = std::clamp(radius + 0.5f - distance, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(p, x, y, colour, coverage);
                }
            }
        }

        /// The pixel point of the park point (x, z) on a picture `scale` pixels per tile.
        glm::vec2 to_pixels(const save::model::TerrainGrid& grid, i32 scale, glm::vec2 park)
        {
            const glm::vec2 m = to_map(map_frame(grid), park);
            return { m.x * static_cast<f32>(grid.width * scale), m.y * static_cast<f32>(grid.height * scale) };
        }

        /// The middle of tile (col, row) in pixels.
        glm::vec2 tile_middle(const save::model::TerrainGrid& grid, i32 scale, i32 col, i32 row)
        {
            const f32 s = static_cast<f32>(scale);
            return { (static_cast<f32>(col) + 0.5f) * s, (static_cast<f32>(grid.height - 1 - row) + 0.5f) * s };
        }

        glm::vec3 height_tint(f32 t)
        {
            for (usize i = 1; i < kHeightTints.size(); ++i)
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

        f32 contour_step(f32 spread)
        {
            for (const f32 step : kContourSteps)
            {
                if (spread / step <= kMostContours)
                    return step;
            }
            return kContourSteps.back();
        }

        glm::vec3 colour_of(MapRgba c)
        {
            return { static_cast<f32>(c.r) / 255.0f, static_cast<f32>(c.g) / 255.0f, static_cast<f32>(c.b) / 255.0f };
        }

        bool owned(save::model::Ownership o)
        {
            return o == save::model::Ownership::Owned || o == save::model::Ownership::OwnedRights;
        }

        bool for_sale(save::model::Ownership o)
        {
            return o == save::model::Ownership::ForSale || o == save::model::Ownership::ForSaleRights;
        }

        void draw_park_land(MapPicture& p, const save::model::TerrainGrid& grid, i32 scale)
        {
            const MapRgba outside = map_layer_colour(MapLayer::ParkLand);
            for (i32 row = 0; row < grid.height; ++row)
            {
                for (i32 col = 0; col < grid.width; ++col)
                {
                    const save::model::Ownership o = grid.at(col, row).ownership();
                    if (owned(o))
                        continue;
                    const i32 x0 = col * scale;
                    const i32 y0 = (grid.height - 1 - row) * scale;
                    for (i32 y = y0; y < y0 + scale; ++y)
                    {
                        for (i32 x = x0; x < x0 + scale; ++x)
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
            const f32 s = static_cast<f32>(scale);
            const f32 half = std::max(0.5f, kParkEdgeWidth * s / 2);
            const auto is_owned = [&](i32 col, i32 row) { return grid.contains(col, row) && owned(grid.at(col, row).ownership()); };
            for (i32 row = 0; row < grid.height; ++row)
            {
                for (i32 col = 0; col < grid.width; ++col)
                {
                    if (!is_owned(col, row))
                        continue;
                    const f32 left = static_cast<f32>(col) * s;
                    const f32 top = static_cast<f32>(grid.height - 1 - row) * s;
                    const std::array<std::pair<glm::vec2, glm::vec2>, 4> sides { {
                        { { left, top }, { left + s, top } },         // North: row + 1
                        { { left, top + s }, { left + s, top + s } }, // South: row - 1
                        { { left, top }, { left, top + s } },         // West: col - 1
                        { { left + s, top }, { left + s, top + s } }, // East: col + 1
                    } };
                    const std::array<std::pair<i32, i32>, 4> next { { { col, row + 1 }, { col, row - 1 }, { col - 1, row }, { col + 1, row } } };
                    for (usize i = 0; i < sides.size(); ++i)
                    {
                        if (!is_owned(next[i].first, next[i].second))
                            fill_capsule(p, sides[i].first, sides[i].second, half, kParkEdge);
                    }
                }
            }
        }

        void draw_paths(MapPicture& p, const save::model::TerrainGrid& grid, i32 scale, std::span<const MapPathTile> paths)
        {
            std::set<std::pair<i32, i32>> at;
            for (const MapPathTile& t : paths)
                at.emplace(t.col, t.row);
            const f32 radius = std::max(1.0f, kPathRadius * static_cast<f32>(scale));
            // Casings first, so where lines join the light parts run on without a seam.
            for (i32 pass = 0; pass < 2; ++pass)
            {
                for (const MapPathTile& t : paths)
                {
                    const MapRgba colour = pass == 0 ? kPathCase : (t.queue ? kQueue : map_layer_colour(MapLayer::Paths));
                    const f32 r = pass == 0 ? radius + kPathCasing.value : radius;
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
        return kLayerColours.at(static_cast<usize>(layer));
    }

    MapLayer attraction_layer(std::optional<u32> type_id)
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

    MapFrame map_frame(const save::model::TerrainGrid& grid)
    {
        MapFrame f;
        f.west_edge = grid.origin_x;
        f.south_edge = grid.origin_z;
        f.width = std::max(1e-3f, static_cast<f32>(grid.width) * grid.tile_size_x);
        f.depth = std::max(1e-3f, static_cast<f32>(grid.height) * grid.tile_size_z);
        return f;
    }

    glm::vec2 to_map(const MapFrame& frame, glm::vec2 park)
    {
        return { (park.x - frame.west_edge) / frame.width, 1.0f - (park.y - frame.south_edge) / frame.depth };
    }

    glm::vec2 to_park(const MapFrame& frame, glm::vec2 map)
    {
        return { frame.west_edge + map.x * frame.width, frame.south_edge + (1.0f - map.y) * frame.depth };
    }

    MapFeatures map_features(const save::model::TerrainGrid& grid, std::span<const save::model::PathTile> paths, const save::model::WaterLayer& water)
    {
        MapFeatures f;
        std::set<std::pair<i32, i32>> seen;
        for (const save::model::PathTile& t : paths)
        {
            if (!grid.contains(t.col, t.row) || !seen.emplace(t.col, t.row).second)
                continue;
            f.paths.push_back({ t.col, t.row, t.kind == save::model::PathKind::Queue });
        }
        f.water.assign(grid.cells.size(), std::nullopt);
        for (const save::model::WaterBody& body : water.bodies)
        {
            for (const save::model::WaterTri& t : body.tris)
            {
                if (!grid.contains(t.col, t.row))
                    continue;
                auto& level = f.water[static_cast<usize>(t.row) * static_cast<usize>(grid.width) + t.col];
                level = std::max(level.value_or(body.level), body.level);
            }
        }
        return f;
    }

    std::array<glm::vec2, 4> ground_corners(std::span<const glm::vec3, 8> box)
    {
        // Bit 0 of an index picks the far x side, bit 2 the far z side: go round 0, 1, 5, 4.
        const auto xz = [&](usize i) { return glm::vec2(box[i].x, box[i].z); };
        return { xz(0), xz(1), xz(5), xz(4) };
    }

    std::array<glm::vec2, 4> tile_corners(const save::model::TerrainGrid& grid, save::model::Tile tile)
    {
        const auto [col, row] = tile;
        const f32 x0 = grid.world_x(col);
        const f32 x1 = grid.world_x(col + 1);
        const f32 z0 = grid.world_z(row);
        const f32 z1 = grid.world_z(row + 1);
        return { glm::vec2 { x0, z0 }, glm::vec2 { x1, z0 }, glm::vec2 { x1, z1 }, glm::vec2 { x0, z1 } };
    }

    MapRgba MapPicture::at(i32 x, i32 y) const
    {
        if (x < 0 || y < 0 || x >= width || y >= height)
            return { 0, 0, 0, 0 };
        const usize i = pixel(*this, x, y);
        return { rgba[i], rgba[i + 1], rgba[i + 2], rgba[i + 3] };
    }

    i32 map_scale(const save::model::TerrainGrid& grid)
    {
        const i32 across = std::max({ grid.width, grid.height, 1 });
        return std::clamp(kMapPixelsAcross / across, kMinMapScale, kMaxMapScale);
    }

    MapGround map_ground(const save::model::TerrainGrid& grid, const MapFeatures& features, i32 scale)
    {
        MapGround g;
        g.scale = scale;
        const i32 width = grid.width * scale;
        const i32 height = grid.height * scale;
        g.relief = blank(width, height);
        g.flat = blank(width, height);
        g.contours = blank(width, height);
        g.water = blank(width, height);
        if (grid.empty())
            return g;

        // Heights at the tiles' corners, where tiles meeting at a corner disagree (at a cliff) their mean.
        const i32 across = grid.width + 1;
        const i32 down = grid.height + 1;
        std::vector<f32> corner(static_cast<usize>(across) * static_cast<usize>(down), 0.0f);
        std::vector<f32> count(corner.size(), 0.0f);
        const auto at = [&](i32 i, i32 j) { return static_cast<usize>(std::clamp(j, 0, down - 1)) * static_cast<usize>(across) + static_cast<usize>(std::clamp(i, 0, across - 1)); };
        for (i32 row = 0; row < grid.height; ++row)
        {
            for (i32 col = 0; col < grid.width; ++col)
            {
                const auto& c = grid.at(col, row).corners;
                for (i32 k = 0; k < 4; ++k)
                {
                    const usize i = at(col + (k & 1), row + (k >> 1));
                    corner[i] += c[static_cast<usize>(k)];
                    count[i] += 1;
                }
            }
        }
        for (usize i = 0; i < corner.size(); ++i)
            corner[i] /= std::max(count[i], 1.0f);
        const auto [low_it, high_it] = std::ranges::minmax_element(corner);
        const f32 lowest = *low_it;
        const f32 spread = *high_it - lowest;

        // The light at each corner, from the slope round it; pixels take it smoothly from the corners, so
        // hills look round rather than cut into tiles.
        std::vector<f32> light(corner.size());
        for (i32 j = 0; j < down; ++j)
        {
            for (i32 i = 0; i < across; ++i)
            {
                const f32 dx = (corner[at(i + 1, j)] - corner[at(i - 1, j)]) / (2 * grid.tile_size_x);
                const f32 dz = (corner[at(i, j + 1)] - corner[at(i, j - 1)]) / (2 * grid.tile_size_z);
                const glm::vec3 normal = glm::normalize(glm::vec3(-dx * kSlopeExaggeration, 1.0f, -dz * kSlopeExaggeration));
                light[at(i, j)] = std::clamp(glm::dot(normal, kLight) / kLight.y, kShadeLow, kShadeHigh);
            }
        }

        // Each tile's own colour: its surface's where known, else the land's middle tint.
        const glm::vec3 flat = colour_of(map_layer_colour(MapLayer::Relief));
        std::vector<glm::vec3> tile_colour(grid.cells.size(), flat);
        for (usize i = 0; i < grid.cells.size(); ++i)
        {
            const usize surface = grid.cells[i].surface;
            if (surface < features.surfaces.size() && features.surfaces[surface])
                tile_colour[i] = colour_of(*features.surfaces[surface]);
        }
        const auto tile_at = [&](i32 col, i32 row) { return tile_colour[static_cast<usize>(std::clamp(row, 0, grid.height - 1)) * static_cast<usize>(grid.width) + static_cast<usize>(std::clamp(col, 0, grid.width - 1))]; };

        const f32 step = contour_step(spread);
        const MapRgba minor = map_layer_colour(MapLayer::Contours);
        const f32 s = static_cast<f32>(scale);
        for (i32 y = 0; y < height; ++y)
        {
            // Where the pixel's middle lies in tiles, from the south-west corner of the map.
            const f32 fz = static_cast<f32>(grid.height) - (static_cast<f32>(y) + 0.5f) / s;
            const i32 j = std::clamp(static_cast<i32>(fz), 0, grid.height - 1);
            const f32 v = fz - static_cast<f32>(j);
            for (i32 x = 0; x < width; ++x)
            {
                const f32 fx = (static_cast<f32>(x) + 0.5f) / s;
                const i32 i = std::clamp(static_cast<i32>(fx), 0, grid.width - 1);
                const f32 u = fx - static_cast<f32>(i);
                const f32 h00 = corner[at(i, j)];
                const f32 h10 = corner[at(i + 1, j)];
                const f32 h01 = corner[at(i, j + 1)];
                const f32 h11 = corner[at(i + 1, j + 1)];
                const f32 here = glm::mix(glm::mix(h00, h10, u), glm::mix(h01, h11, u), v);
                const f32 shade = glm::mix(glm::mix(light[at(i, j)], light[at(i + 1, j)], u), glm::mix(light[at(i, j + 1)], light[at(i + 1, j + 1)], u), v);

                // The ground's colour, blended between the tiles' middles so surfaces meet softly, tinted
                // towards the height's colour so high and low ground tell apart.
                const f32 gx = fx - 0.5f;
                const f32 gz = fz - 0.5f;
                const i32 c0 = static_cast<i32>(std::floor(gx));
                const i32 r0 = static_cast<i32>(std::floor(gz));
                const f32 cu = gx - static_cast<f32>(c0);
                const f32 cv = gz - static_cast<f32>(r0);
                const glm::vec3 own = glm::mix(glm::mix(tile_at(c0, r0), tile_at(c0 + 1, r0), cu), glm::mix(tile_at(c0, r0 + 1), tile_at(c0 + 1, r0 + 1), cu), cv);
                const f32 t = spread < kFlatSpread ? kHeightTints[1].at : (here - lowest) / spread;
                put(g.relief, x, y, glm::min(glm::mix(own, height_tint(t), kHeightTintShare) * shade, glm::vec3(1.0f)), 1);
                put(g.flat, x, y, own, 1);

                // How many pixels this pixel lies from the nearest line, by how fast height changes here.
                const f32 east = glm::mix(h10 - h00, h11 - h01, v);
                const f32 north = glm::mix(h01 - h00, h11 - h10, u);
                const f32 per_pixel = std::sqrt(east * east + north * north) / s / step;
                if (per_pixel > 1e-4f)
                {
                    const f32 lines = (here - lowest) / step;
                    const f32 nearest = std::round(lines);
                    const f32 away = std::abs(lines - nearest) / per_pixel;
                    const f32 coverage = std::clamp(kContourWidth.value / 2 + 0.5f - away, 0.0f, 1.0f);
                    if (coverage > 0)
                        blend(g.contours, x, y, static_cast<i32>(nearest) % kMajorEvery == 0 ? kMajorContour : minor, coverage);
                }

                const i32 col = std::min(x / scale, grid.width - 1);
                const i32 row = std::clamp(grid.height - 1 - y / scale, 0, grid.height - 1);
                const usize tile = static_cast<usize>(row) * static_cast<usize>(grid.width) + static_cast<usize>(col);
                const std::optional<f32> level = tile < features.water.size() ? features.water[tile] : std::nullopt;
                if (level && *level > here)
                {
                    const f32 depth = *level - here;
                    const f32 alpha = kWaterAlpha * std::clamp(depth / kShoreFade, 0.0f, 1.0f);
                    put(g.water, x, y, glm::mix(kShallow, kDeep, std::clamp(depth / kDeepAt, 0.0f, 1.0f)), alpha);
                }
            }
        }
        return g;
    }

    MapPicture compose_map(const MapGround& ground, const save::model::TerrainGrid& grid, const MapFeatures& features, const MapLayers& layers)
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
                for (usize i = 0; i < corners.size(); ++i)
                    corners[i] = to_pixels(grid, ground.scale, s.corners[i]);
                const f32 across = glm::length(corners[1] - corners[0]);
                const f32 along = glm::length(corners[2] - corners[1]);
                const glm::vec2 middle = (corners[0] + corners[1] + corners[2] + corners[3]) / 4.0f;
                if (s.layer != MapLayer::Scenery && s.layer != MapLayer::Rides)
                {
                    // Stalls and facilities are small but what players look for: a round badge with a rim.
                    const f32 radius = std::max(kSmallestBadge, kBadgeRadius * static_cast<f32>(ground.scale));
                    fill_capsule(p, middle, middle, radius + kBadgeRim, kBadgeRimColour);
                    fill_capsule(p, middle, middle, radius, map_layer_colour(s.layer));
                }
                else if (s.layer == MapLayer::Scenery && std::max(across, along) <= kRoundUpTo * static_cast<f32>(ground.scale))
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

    MapPicture people_picture(const save::model::TerrainGrid& grid, i32 scale, std::span<const MapPerson> people, const MapLayers& layers, std::span<const glm::ivec2> patrol_tiles)
    {
        MapPicture p = blank(grid.width * scale, grid.height * scale);
        if (shows(layers, MapLayer::Patrols))
            for (const glm::ivec2 t : patrol_tiles)
            {
                if (!grid.contains(t.x, t.y))
                    continue;
                const i32 x0 = t.x * scale;
                const i32 y0 = (grid.height - 1 - t.y) * scale;
                for (i32 y = y0; y < y0 + scale; ++y)
                    for (i32 x = x0; x < x0 + scale; ++x)
                        blend(p, x, y, map_layer_colour(MapLayer::Patrols), 1);
            }
        const f32 radius = std::max(kSmallestPerson, kPersonRadius * static_cast<f32>(scale));
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
                fill_capsule(p, at, at, radius + kPersonRim.value, staff ? kStaffRim : kGuestRim);
                fill_capsule(p, at, at, radius, map_layer_colour(staff ? MapLayer::Staff : MapLayer::Guests));
            }
        }
        return p;
    }

} // namespace rct3::park
