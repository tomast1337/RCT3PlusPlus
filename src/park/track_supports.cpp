// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track_supports.hpp"

#include "park/ground_queues.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace rct3::park
{

    namespace
    {

        /// How far an item must be above the ground's lowest corner for a column to fit under it.
        constexpr float kLeastGap = 0.5f;
        /// Thirds of a tile across: support points sit on these.
        constexpr int kCells = 3;
        /// How near the middle of its third a point must be, as a fraction of the third, for its column to
        /// stand right under it.
        constexpr float kStraightUnder = 0.2f;

        using Tile = std::pair<int, int>;

        /// One square of an item, on the tile it covers.
        struct Covered
        {
            const save::PlacedObject* object = nullptr;
            const SupportFootprint* footprint = nullptr;
            const SupportSquare* square = nullptr;
            std::int32_t low = 0; ///< Levels, absolute
            std::int32_t high = 0;
        };

        using Covering = std::map<Tile, std::vector<Covered>>;

        struct Piece
        {
            const SupportPiece* piece = nullptr;
            float level = 0;
            float stretch = 1; ///< Up and down, for a top piece that ends between levels
        };

        /// Middles of `set` filling `low` to `high`, from the tallest down, each lined up on whole multiples of
        /// its height so that neighbouring columns show their joints at the same levels.
        void fill_middles(const SupportSet& set, std::size_t from, std::int32_t low, std::int32_t high, std::vector<Piece>& out)
        {
            for (std::size_t i = from; i < set.middles.size() && low < high; ++i)
            {
                const std::int32_t h = set.middles[i].levels;
                const std::int32_t start = static_cast<std::int32_t>(std::ceil(static_cast<float>(low) / static_cast<float>(h)));
                const std::int32_t end = static_cast<std::int32_t>(std::floor(static_cast<float>(high) / static_cast<float>(h)));
                if (start >= end)
                    continue;
                for (std::int32_t k = start; k < end; ++k)
                    out.push_back({ &set.middles[i], static_cast<float>(k * h) });
                fill_middles(set, i + 1, low, start * h, out);
                fill_middles(set, i + 1, end * h, high, out);
                return;
            }
        }

        /// The pieces of a column of `set` from `bottom` up to `top`. A column whose top falls between levels
        /// gives it as `exact_top`; its top piece stretches to end there.
        std::vector<Piece> column(const SupportSet& set, std::int32_t bottom, std::int32_t top, bool on_ground, std::optional<float> exact_top = std::nullopt)
        {
            std::vector<Piece> out;
            const auto& first = on_ground ? set.deep_foot : set.foot;
            const auto& second = on_ground ? set.foot : set.deep_foot;
            if (const auto& foot = first ? first : second; foot && bottom + foot->levels <= top)
            {
                out.push_back({ &*foot, static_cast<float>(bottom) });
                bottom += foot->levels;
            }
            if (set.top && bottom + set.top->levels <= top)
            {
                top -= set.top->levels;
                const float end = exact_top.value_or(static_cast<float>(top + set.top->levels));
                out.push_back({ &*set.top, static_cast<float>(top), (end - static_cast<float>(top)) / static_cast<float>(set.top->levels) });
            }
            fill_middles(set, 0, bottom, top, out);
            return out;
        }

        /// The lowest and highest corner of the finest shape of `symbol` among the support pieces.
        std::optional<std::pair<glm::vec3, glm::vec3>> extent(const ObjectModels& models, std::string_view symbol)
        {
            const auto model = models.find(std::string(kSupportOverlay) + "|" + std::string(symbol));
            if (model == models.end() || model->second.levels.empty())
                return std::nullopt;
            glm::vec3 low { std::numeric_limits<float>::max() };
            glm::vec3 high { std::numeric_limits<float>::lowest() };
            for (const ModelPart& part : model->second.levels.front().parts)
            {
                for (const ModelVertex& v : part.vertices)
                {
                    low = glm::min(low, v.position);
                    high = glm::max(high, v.position);
                }
            }
            if (low.x > high.x)
                return std::nullopt;
            return std::pair { low, high };
        }

        /// Where a spot on `object` lies in the park, in tiles from the grid's corner: the footprint is laid
        /// out round its middle, its squares and thirds running along the model's -X and -Z.
        glm::vec2 spot_in_tiles(const save::PlacedObject& object, const SupportFootprint& footprint, const SupportSpot& spot, bool middle_of_third)
        {
            const glm::vec2 x = turned_x(object.rotation);
            const glm::vec2 z { -x.y, x.x };
            const auto along = [&](int i) {
                const float within = middle_of_third ? 0.5f : spot.within[static_cast<std::size_t>(i)];
                return static_cast<float>(spot.square[static_cast<std::size_t>(i)]) + (static_cast<float>(spot.cell[static_cast<std::size_t>(i)]) + within) / kCells;
            };
            const float u = along(0) - static_cast<float>(footprint.x_squares) / 2;
            const float v = along(1) - static_cast<float>(footprint.z_squares) / 2;
            return footprint_middle(object, { footprint.x_squares, footprint.z_squares }) - u * x - v * z;
        }

        glm::vec3 in_park(const save::TerrainGrid& grid, glm::vec2 tiles, float y)
        {
            return { grid.world_x(0) + tiles.x * grid.tile_size_x, y, grid.world_z(0) + tiles.y * grid.tile_size_z };
        }

        /// Where a column under `top` on `tile` starts and whether that is the ground: on the next item
        /// down that may carry it, or on the ground. `cell` limits the items in the way to those filling
        /// that third of the tile, when they say which thirds they fill. `gap_over_track` keeps a level clear
        /// over track.
        std::optional<std::pair<std::int32_t, bool>> column_base(const Covering& covering, const std::set<Tile>& ground_paths, const save::TerrainGrid& grid, const Tile& tile, const save::PlacedObject* self, float top, const SupportSet& set, std::optional<int> cell, bool gap_over_track)
        {
            if (const auto found = covering.find(tile); found != covering.end())
            {
                for (const Covered& b : found->second)
                {
                    if (b.object == self || static_cast<float>(b.high) > top)
                        continue;
                    if (cell && b.footprint->by_points && (b.square->cells & (1u << *cell)) == 0)
                        continue;
                    if (!set.stands_on_anything && !b.footprint->carries_columns)
                        return std::nullopt;
                    return std::pair { b.high + (gap_over_track && b.footprint->track && !set.stands_close ? 1 : 0), false };
                }
            }
            if (ground_paths.contains(tile) || !grid.contains(tile.first, tile.second))
                return std::nullopt;
            const float ground = grid.at(tile.first, tile.second).min_height();
            if (top - ground <= kLeastGap)
                return std::nullopt;
            return std::pair { static_cast<std::int32_t>(std::ceil(ground)), true };
        }

        Tile tile_of(glm::vec2 tiles)
        {
            return { static_cast<int>(std::floor(tiles.x)), static_cast<int>(std::floor(tiles.y)) };
        }

        /// Builds the pieces into placed objects.
        struct Builder
        {
            const save::TerrainGrid& grid;
            std::uint64_t last_id = 0;
            BuiltSupports out;

            save::PlacedObject& add(const save::PlacedObject& held, const SupportPiece& piece, const Tile& tile, std::int32_t level, std::int32_t turns)
            {
                save::PlacedObject& o = out.pieces.emplace_back();
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.symbol = piece.symbol;
                o.overlay = std::string(kSupportOverlay);
                o.col = tile.first;
                o.row = tile.second;
                o.base_height = level;
                o.height_adjust = 0.0f;
                o.force_absolute_height = true;
                o.rotation = turns;
                o.colours = held.colours;
                return o;
            }

            /// A piece stood at `at`, turned like `turns` and stretched up by `stretch`.
            void stand(const save::PlacedObject& held, const SupportPiece& piece, glm::vec2 tiles, float level, std::int32_t turns, float stretch)
            {
                save::PlacedObject& o = add(held, piece, tile_of(tiles), static_cast<std::int32_t>(std::floor(level)), turns);
                const glm::vec2 x = turned_x(turns);
                glm::mat4 m { 1.0f };
                m[0] = glm::vec4(x.x, 0, x.y, 0);
                m[1] = glm::vec4(0, stretch, 0, 0);
                m[2] = glm::vec4(-x.y, 0, x.x, 0);
                m[3] = glm::vec4(in_park(grid, tiles, level), 1);
                out.fitted.emplace(o.id, m);
            }

            /// A beam from `from` to `to`, its middle halfway, running along its length and reaching `overhang`
            /// past both ends.
            void beam(const save::PlacedObject& held, const SupportPiece& piece, float length, glm::vec3 from, glm::vec3 to, float overhang)
            {
                const glm::vec3 run = to - from;
                const float distance = glm::length(run);
                if (distance <= 0 || length <= 0)
                    return;
                const glm::vec3 along = run / distance;
                const glm::vec3 side = glm::normalize(glm::cross(along, glm::vec3 { 0, 1, 0 }));
                const glm::vec3 up = glm::cross(side, along);
                const glm::vec3 middle = (from + to) / 2.0f;
                save::PlacedObject& o = add(held, piece, { static_cast<int>(std::floor((middle.x - grid.world_x(0)) / grid.tile_size_x)), static_cast<int>(std::floor((middle.z - grid.world_z(0)) / grid.tile_size_z)) }, static_cast<std::int32_t>(std::floor(middle.y)), held.rotation);
                glm::mat4 m { 1.0f };
                m[0] = glm::vec4(along * ((distance + 2 * overhang) / length), 0);
                m[1] = glm::vec4(up, 0);
                m[2] = glm::vec4(side, 0);
                m[3] = glm::vec4(middle, 1);
                out.fitted.emplace(o.id, m);
            }
        };

    } // namespace

    std::vector<Tile> footprint_tiles(const save::PlacedObject& object, const SupportFootprint& footprint)
    {
        std::vector<Tile> out;
        for (std::int32_t lz = 0; lz < footprint.z_squares; ++lz)
        {
            for (std::int32_t lx = 0; lx < footprint.x_squares; ++lx)
                out.push_back(tile_of(spot_in_tiles(object, footprint, { { lx, lz }, { 1, 1 } }, true)));
        }
        return out;
    }

    BuiltSupports track_supports(std::span<const save::PlacedObject> objects, const ObjectModels& models, std::span<const save::PathTile> paths, const save::TerrainGrid& grid, const SupportSets& sets, std::uint64_t last_id)
    {
        std::set<Tile> ground_paths;
        for (const save::PathTile& p : paths)
        {
            if (!p.elevation || is_ground_queue(p))
                ground_paths.emplace(p.col, p.row);
        }

        // What covers each tile, highest first.
        Covering covering;
        std::vector<std::pair<const save::PlacedObject*, const ObjectModel*>> by_points;
        for (const save::PlacedObject& o : objects)
        {
            if (o.hidden || !o.base_height)
                continue;
            const auto model = models.find(model_key(o));
            if (model == models.end() || !model->second.supports)
                continue;
            const SupportFootprint& f = *model->second.supports;
            if (f.by_points && f.wants_supports)
                by_points.emplace_back(&o, &model->second);
            const auto under = footprint_tiles(o, f);
            for (std::size_t i = 0; i < under.size() && i < f.squares.size(); ++i)
            {
                const SupportSquare& s = f.squares[i];
                if (s.high <= s.low && !s.wants_column)
                    continue;
                covering[under[i]].push_back({ &o, &f, &s, *o.base_height + s.low, *o.base_height + s.high });
            }
        }
        for (auto& [tile, covered] : covering)
            std::ranges::stable_sort(covered, std::greater { }, &Covered::high);

        Builder build { grid, last_id, { } };

        // A column under each square that asks for one, under the middle of its tile.
        for (const auto& [tile, covered] : covering)
        {
            const bool odd_tile = ((tile.first ^ tile.second) & 1) != 0;
            for (const Covered& c : covered)
            {
                const SupportFootprint& f = *c.footprint;
                if (!f.wants_supports || f.by_points || !c.square->wants_column || c.square->kind == kNoSupport || (f.alternate_tiles && !odd_tile))
                    continue;
                const FoundSet found = find_support_set(sets, models.at(model_key(*c.object)).support_group, turned(c.square->kind, c.object->rotation));
                if (found.set == nullptr)
                    continue;
                const auto base = column_base(covering, ground_paths, grid, tile, c.object, static_cast<float>(c.low), *found.set, std::nullopt, true);
                if (!base || base->first >= c.low || c.low - base->first >= kTallestColumn)
                    continue;
                for (const Piece& p : column(*found.set, base->first, c.low, base->second))
                    build.add(*c.object, *p.piece, tile, static_cast<std::int32_t>(p.level), found.quarter_turns);
            }
        }

        // A column under each group of support points, at the first point of the group that can have one.
        for (const auto& [object, model] : by_points)
        {
            const SupportFootprint& f = *model->supports;
            const FoundSet found = find_support_set(sets, model->support_group, kRoundSupport);
            if (found.set == nullptr)
                continue;
            const SupportSet& set = *found.set;
            const auto cap = set.cap && f.capped ? extent(models, set.cap->symbol) : std::nullopt;
            const float cap_height = cap ? cap->second.y : 0.0f;
            std::vector<std::pair<const SupportPiece*, float>> beams;
            for (const SupportPiece& b : set.beams)
            {
                if (const auto e = extent(models, b.symbol))
                    beams.emplace_back(&b, e->second.x - e->first.x);
            }
            std::ranges::sort(beams, { }, &std::pair<const SupportPiece*, float>::second);
            const auto top_size = set.top ? extent(models, set.top->symbol) : std::nullopt;
            const float overhang = top_size ? (top_size->second.x - top_size->first.x) / 2 : 0.0f;

            std::set<std::uint32_t> groups_built;
            std::set<std::pair<Tile, int>> thirds_built;
            const auto base_level = static_cast<float>(*object->base_height);
            for (const SupportPoint& p : f.points)
            {
                if (groups_built.contains(p.group))
                    continue;
                const glm::vec2 top_spot = spot_in_tiles(*object, f, p.top, false);
                const glm::vec2 column_spot = spot_in_tiles(*object, f, p.column, true);
                const Tile top_tile = tile_of(top_spot);
                const Tile tile = tile_of(column_spot);
                if (f.alternate_tiles && ((top_tile.first ^ top_tile.second) & 1) == 0)
                    continue;
                const glm::vec2 third_in_tile = (column_spot - glm::vec2 { static_cast<float>(tile.first), static_cast<float>(tile.second) }) * static_cast<float>(kCells);
                const int third = std::clamp(static_cast<int>(third_in_tile.x), 0, kCells - 1) + kCells * std::clamp(static_cast<int>(third_in_tile.y), 0, kCells - 1);

                const glm::vec2 off_middle { p.top.within[0] - 0.5f, p.top.within[1] - 0.5f };
                const bool straight = !p.hanging && p.top.square == p.column.square && p.top.cell == p.column.cell && glm::dot(off_middle, off_middle) < kStraightUnder * kStraightUnder;
                if (!straight && p.straight_only)
                    continue;
                float exact_top = base_level + (straight ? p.top_height : p.column_height);
                if (cap)
                    exact_top -= cap_height;
                const auto top = static_cast<std::int32_t>(std::ceil(exact_top));
                const auto base = column_base(covering, ground_paths, grid, tile, object, exact_top, set, third, false);
                if (!base || static_cast<float>(base->first) >= exact_top || top - base->first >= kTallestColumn)
                    continue;
                groups_built.insert(p.group);
                if (!thirds_built.emplace(tile, third).second)
                    continue;
                for (const Piece& piece : column(set, base->first, top, base->second, exact_top))
                    build.stand(*object, *piece.piece, column_spot, piece.level, object->rotation, piece.stretch);
                if (cap)
                    build.stand(*object, *set.cap, column_spot, exact_top, object->rotation, 1);
                if (!straight && !beams.empty())
                {
                    const glm::vec3 from = in_park(grid, top_spot, base_level + p.top_height);
                    const glm::vec3 to = in_park(grid, column_spot, base_level + p.column_height);
                    const float across = glm::length(glm::vec2 { to.x - from.x, to.z - from.z });
                    const auto fits = std::ranges::find_if(beams, [&](const auto& b) { return b.second >= across; });
                    const auto& [piece, length] = fits != beams.end() ? *fits : beams.back();
                    build.beam(*object, *piece, length, from, to, overhang);
                }
            }
        }
        return std::move(build.out);
    }

} // namespace rct3::park
