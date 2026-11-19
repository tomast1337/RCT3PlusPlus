// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/support/columns.hpp"

#include "core/types.hpp"
#include "park/ground/paths.hpp"
#include "park/ground/queues.hpp"
#include "park/object/placement.hpp"
#include "park/queue_fences.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <format>
#include <limits>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace rct3::park::support
{

    namespace
    {

        /// How far an item must be above the ground's lowest corner for a column to fit under it.
        constexpr f32 kLeastGap = 0.5f;
        /// Thirds of a tile across: support points sit on these.
        constexpr i32 kCells = 3;
        /// How near the middle of its third a point must be, as a fraction of the third, for its column to
        /// stand right under it.
        constexpr f32 kStraightUnder = 0.2f;

        using save::model::Tile;

        /// One square of an item, on the tile it covers.
        struct Covered
        {
            const save::model::PlacedObject* object = nullptr;
            const SupportFootprint* footprint = nullptr;
            const SupportSquare* square = nullptr;
            i32 low = 0; ///< Levels, absolute
            i32 high = 0;
        };

        using Covering = std::map<Tile, std::vector<Covered>>;

        struct Piece
        {
            const SupportPiece* piece = nullptr;
            f32 level = 0;
            f32 stretch = 1; ///< Up and down, for a top piece that ends between levels
        };

        /// Middles of `set` filling `low` to `high`, from the tallest down, each lined up on whole multiples of
        /// its height so that neighbouring columns show their joints at the same levels.
        void fill_middles(const SupportSet& set, usize from, i32 low, i32 high, std::vector<Piece>& out)
        {
            for (usize i = from; i < set.middles.size() && low < high; ++i)
            {
                const i32 h = set.middles[i].levels;
                const i32 start = static_cast<i32>(std::ceil(static_cast<f32>(low) / static_cast<f32>(h)));
                const i32 end = static_cast<i32>(std::floor(static_cast<f32>(high) / static_cast<f32>(h)));
                if (start >= end)
                    continue;
                for (i32 k = start; k < end; ++k)
                    out.push_back({ &set.middles[i], static_cast<f32>(k * h) });
                fill_middles(set, i + 1, low, start * h, out);
                fill_middles(set, i + 1, end * h, high, out);
                return;
            }
        }

        /// The pieces of a column of `set` from `bottom` up to `top`. A column whose top falls between levels
        /// gives it as `exact_top`; its top piece stretches to end there.
        std::vector<Piece> column(const SupportSet& set, i32 bottom, i32 top, bool on_ground, std::optional<f32> exact_top = std::nullopt)
        {
            std::vector<Piece> out;
            const auto& first = on_ground ? set.deep_foot : set.foot;
            const auto& second = on_ground ? set.foot : set.deep_foot;
            if (const auto& foot = first ? first : second; foot && bottom + foot->levels <= top)
            {
                out.push_back({ &*foot, static_cast<f32>(bottom) });
                bottom += foot->levels;
            }
            if (set.top && bottom + set.top->levels <= top)
            {
                top -= set.top->levels;
                const f32 end = exact_top.value_or(static_cast<f32>(top + set.top->levels));
                out.push_back({ &*set.top, static_cast<f32>(top), (end - static_cast<f32>(top)) / static_cast<f32>(set.top->levels) });
            }
            fill_middles(set, 0, bottom, top, out);
            return out;
        }

        /// The lowest and highest corner of the finest shape of `symbol` among the support pieces.
        std::optional<std::pair<glm::vec3, glm::vec3>> extent(const object::ObjectModels& models, std::string_view symbol)
        {
            const auto model = models.find(std::format("{}|{}", kSupportOverlay, symbol));
            if (model == models.end() || model->second.levels.empty())
                return std::nullopt;
            glm::vec3 low { std::numeric_limits<f32>::max() };
            glm::vec3 high { std::numeric_limits<f32>::lowest() };
            for (const object::ModelPart& part : model->second.levels.front().parts)
            {
                for (const object::ModelVertex& v : part.vertices)
                {
                    low = glm::min(low, v.position);
                    high = glm::max(high, v.position);
                }
            }
            if (low.x > high.x)
                return std::nullopt;
            return std::pair { low, high };
        }

        glm::vec3 in_park(const save::model::TerrainGrid& grid, glm::vec2 tiles, f32 y)
        {
            return { grid.world_x(0) + tiles.x * grid.tile_size_x, y, grid.world_z(0) + tiles.y * grid.tile_size_z };
        }

        /// Where a column of `set` on `tile` starts, and whether on the ground, when it is under the items before
        /// `below` there (their undersides higher than its top). Of the items from `below` on, the first in the way
        /// decides: one that carries columns, or any when the set stands on anything, carries it, a level clear of
        /// track when `gap_over_track` and the set does not stand close; others leave no column. `cell` limits the
        /// items in the way to those that fill that third of the tile, when they say which thirds they fill. With
        /// nothing in the way the column stands on the ground, if no ground path is there and there is room.
        std::optional<std::pair<i32, bool>> column_base(std::span<const Covered> below, const std::set<Tile>& ground_paths, const save::model::TerrainGrid& grid, const Tile& tile, const save::model::PlacedObject* self, f32 top, const SupportSet& set, std::optional<i32> cell, bool gap_over_track)
        {
            const bool known = grid.contains(tile.col, tile.row);
            const f32 ground = known ? grid.at(tile.col, tile.row).min_height() : 0.0f;
            for (const Covered& b : below)
            {
                if (b.object == self)
                    continue;
                if (cell && b.footprint->by_points && (b.square->cells & (1u << *cell)) == 0)
                    continue;
                // Buried deeper than a level, as track in a tunnel: the column stands on the ground over it.
                if (known && static_cast<f32>(b.high) < ground - 1)
                    return std::pair { static_cast<i32>(std::ceil(ground)), false };
                if (!set.stands_on_anything && !b.footprint->carries_columns)
                    return std::nullopt;
                return std::pair { b.high + (gap_over_track && b.footprint->track && !set.stands_close ? 1 : 0), false };
            }
            if (ground_paths.contains(tile) || !known || top - ground <= kLeastGap)
                return std::nullopt;
            return std::pair { static_cast<i32>(std::ceil(ground)), true };
        }

        Tile tile_of(glm::vec2 tiles)
        {
            return { static_cast<i32>(std::floor(tiles.x)), static_cast<i32>(std::floor(tiles.y)) };
        }

        /// Builds the pieces into placed objects.
        struct Builder
        {
            const save::model::TerrainGrid& grid;
            u64 last_id = 0;
            BuiltSupports out;

            save::model::PlacedObject& add(const save::model::PlacedObject& held, const SupportPiece& piece, const Tile& tile, i32 level, ModelTurn turn)
            {
                save::model::PlacedObject& o = out.pieces.emplace_back();
                o.id = ++last_id;
                o.owner_ref = o.id;
                o.symbol = piece.symbol;
                o.overlay = std::string(kSupportOverlay);
                o.col = tile.col;
                o.row = tile.row;
                o.base_height = level;
                o.height_adjust = 0.0f;
                o.force_absolute_height = true;
                o.rotation = turn.value;
                o.colours = held.colours;
                return o;
            }

            /// A piece stood at `at`, turned `turn` and stretched up by `stretch`.
            void stand(const save::model::PlacedObject& held, const SupportPiece& piece, glm::vec2 tiles, f32 level, ModelTurn turn, f32 stretch)
            {
                save::model::PlacedObject& o = add(held, piece, tile_of(tiles), static_cast<i32>(std::floor(level)), turn);
                const glm::vec2 x = object::turned_x(turn);
                glm::mat4 m { 1.0f };
                m[0] = glm::vec4(x.x, 0, x.y, 0);
                m[1] = glm::vec4(0, stretch, 0, 0);
                m[2] = glm::vec4(-x.y, 0, x.x, 0);
                m[3] = glm::vec4(in_park(grid, tiles, level), 1);
                out.fitted.emplace(o.id, m);
            }

            /// A beam from `from` to `to`, its middle halfway, running along its length and reaching `overhang`
            /// past both ends.
            void beam(const save::model::PlacedObject& held, const SupportPiece& piece, f32 length, glm::vec3 from, glm::vec3 to, f32 overhang)
            {
                const glm::vec3 run = to - from;
                const f32 distance = glm::length(run);
                if (distance <= 0 || length <= 0)
                    return;
                const glm::vec3 along = run / distance;
                const glm::vec3 side = glm::normalize(glm::cross(along, glm::vec3 { 0, 1, 0 }));
                const glm::vec3 up = glm::cross(side, along);
                const glm::vec3 middle = (from + to) / 2.0f;
                save::model::PlacedObject& o = add(held, piece, { static_cast<i32>(std::floor((middle.x - grid.world_x(0)) / grid.tile_size_x)), static_cast<i32>(std::floor((middle.z - grid.world_z(0)) / grid.tile_size_z)) }, static_cast<i32>(std::floor(middle.y)), ModelTurn::from(held.rotation));
                glm::mat4 m { 1.0f };
                m[0] = glm::vec4(along * ((distance + 2 * overhang) / length), 0);
                m[1] = glm::vec4(up, 0);
                m[2] = glm::vec4(side, 0);
                m[3] = glm::vec4(middle, 1);
                out.fitted.emplace(o.id, m);
            }
        };

    } // namespace

    glm::vec2 spot_in_tiles(const save::model::PlacedObject& object, const SupportFootprint& footprint, const SupportSpot& spot, SpotFit fit)
    {
        const glm::vec2 x = object::turned_x(ModelTurn::from(object.rotation));
        const glm::vec2 z { -x.y, x.x };
        const auto along = [&](i32 i) {
            const f32 within = fit == SpotFit::MiddleOfThird ? 0.5f : spot.within[static_cast<usize>(i)];
            return static_cast<f32>(spot.square[static_cast<usize>(i)]) + (static_cast<f32>(spot.cell[static_cast<usize>(i)]) + within) / kCells;
        };
        const f32 u = along(0) - static_cast<f32>(footprint.x_squares) / 2;
        const f32 v = along(1) - static_cast<f32>(footprint.z_squares) / 2;
        return object::footprint_middle(object, { footprint.x_squares, footprint.z_squares }) - u * x - v * z;
    }

    std::vector<Tile> footprint_tiles(const save::model::PlacedObject& object, const SupportFootprint& footprint)
    {
        std::vector<Tile> out;
        for (i32 lz = 0; lz < footprint.z_squares; ++lz)
        {
            for (i32 lx = 0; lx < footprint.x_squares; ++lx)
                out.push_back(tile_of(spot_in_tiles(object, footprint, { { lx, lz }, { 1, 1 } }, SpotFit::MiddleOfThird)));
        }
        return out;
    }

    std::vector<SupportTile> support_tiles(const save::model::PlacedObject& object, const object::ObjectModels& models)
    {
        const auto model = models.find(object::model_ref(object));
        if (model == models.end() || !model->second.supports)
            return { };
        const SupportFootprint& f = *model->second.supports;
        std::vector<Tile> out = footprint_tiles(object, f);
        for (const SupportPoint& p : f.points)
            out.push_back(tile_of(spot_in_tiles(object, f, p.column, SpotFit::MiddleOfThird)));
        std::ranges::sort(out);
        out.erase(std::ranges::unique(out).begin(), out.end());
        return out;
    }

    bool path_at_ground(const save::model::PathTile& path, const save::model::TerrainGrid& grid)
    {
        if (!path.elevation || ground::is_ground_queue(path))
            return true;
        if (!grid.contains(path.col, path.row))
            return false;
        const save::model::TerrainCell& cell = grid.at(path.col, path.row);
        const auto height = static_cast<f32>(path.elevation->quantised_height);
        const f32 rise = ground::path_rise(path.elevation->slope);
        const f32 low = slope_falls(path.elevation->slope) ? height - rise : height;
        return low <= cell.max_height() && low + rise >= cell.min_height();
    }

    BuiltSupports support_columns(std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid, const SupportSets& sets, u64 last_id, const std::unordered_set<u64>& passed, std::optional<const std::set<SupportTile>&> tiles, std::span<const object::ObjectModel* const> found)
    {
        std::set<Tile> ground_paths;
        for (const save::model::PathTile& p : paths)
        {
            if (path_at_ground(p, grid))
                ground_paths.emplace(p.col, p.row);
        }
        const auto wanted = [&](const Tile& t) { return !tiles || tiles->contains(t); };

        // Items with support points whose columns stand on those tiles; which point of a group gets one depends
        // on what is under each, so the tiles under all their points are looked at.
        std::vector<std::pair<const save::model::PlacedObject*, const object::ObjectModel*>> by_points;
        std::set<Tile> needed;
        const auto model_of = [&](usize i) -> const object::ObjectModel* {
            const save::model::PlacedObject& o = objects[i];
            if (o.hidden || passed.contains(o.id) || o.overlay == kSupportOverlay)
                return nullptr;
            if (i < found.size())
                return found[i] != nullptr && found[i]->supports ? found[i] : nullptr;
            const auto model = models.find(object::model_ref(o));
            return model != models.end() && model->second.supports ? &model->second : nullptr;
        };
        for (usize i = 0; i < objects.size(); ++i)
        {
            const save::model::PlacedObject& o = objects[i];
            const object::ObjectModel* m = model_of(i);
            if (m == nullptr || !m->supports->by_points || !m->supports->wants_supports)
                continue;
            const SupportFootprint& f = *m->supports;
            std::vector<Tile> under;
            for (const SupportPoint& p : f.points)
                under.push_back(tile_of(spot_in_tiles(o, f, p.column, SpotFit::MiddleOfThird)));
            if (!std::ranges::any_of(under, wanted))
                continue;
            by_points.emplace_back(&o, m);
            needed.insert(under.begin(), under.end());
        }

        // What covers each tile, by underside, highest first.
        Covering covering;
        for (usize n = 0; n < objects.size(); ++n)
        {
            const save::model::PlacedObject& o = objects[n];
            const object::ObjectModel* m = model_of(n);
            if (m == nullptr)
                continue;
            const SupportFootprint& f = *m->supports;
            const auto under = footprint_tiles(o, f);
            // Items standing on the ground say so by having no height of their own.
            std::optional<i32> base = o.base_height;
            for (usize i = 0; i < under.size() && i < f.squares.size(); ++i)
            {
                if (!wanted(under[i]) && !needed.contains(under[i]))
                    continue;
                if (!base)
                    base = static_cast<i32>(std::lround(object::object_transform(o, static_cast<object::Positioning>(m->positioning), grid, m->squares, m->ground_height)[3].y));
                covering[under[i]].push_back({ &o, &f, &f.squares[i], *base + f.squares[i].low, *base + f.squares[i].high });
            }
        }
        for (auto& [_, covered] : covering)
            std::ranges::stable_sort(covered, std::greater { }, &Covered::low);

        Builder build { grid, last_id, { } };

        // A column under each square that asks for one, under the middle of its tile.
        for (const auto& [tile, covered] : covering)
        {
            if (!wanted(tile))
                continue;
            const bool odd_tile = ((tile.col ^ tile.row) & 1) != 0;
            for (usize i = 0; i < covered.size(); ++i)
            {
                const Covered& c = covered[i];
                const SupportFootprint& f = *c.footprint;
                if (!f.wants_supports || f.by_points || !c.square->wants_column || c.square->kind == kNoSupport || (f.alternate_tiles && !odd_tile) || !c.object->base_height)
                    continue;
                const std::string& group = c.object->support_set.empty() ? models.at(object::model_key(*c.object)).support_group : c.object->support_set;
                const FoundSet found = find_support_set(sets, group, turned(c.square->kind, ModelTurn::from(c.object->rotation)));
                if (found.set == nullptr)
                    continue;
                const auto base = column_base(std::span(covered).subspan(i + 1), ground_paths, grid, tile, c.object, static_cast<f32>(c.low), *found.set, std::nullopt, true);
                if (!base || base->first >= c.low || c.low - base->first >= kTallestColumn)
                    continue;
                for (const Piece& p : column(*found.set, base->first, c.low, base->second))
                {
                    build.add(*c.object, *p.piece, tile, static_cast<i32>(p.level), found.turn);
                    build.out.of.push_back({ c.object->id, tile });
                }
            }
        }

        // A column under each group of support points, at the first point of the group that can have one.
        const std::vector<Covered> none;
        for (const auto& [object, model] : by_points)
        {
            const SupportFootprint& f = *model->supports;
            const FoundSet found = find_support_set(sets, object->support_set.empty() ? model->support_group : object->support_set, kRoundSupport);
            if (found.set == nullptr || !object->base_height)
                continue;
            const SupportSet& set = *found.set;
            const auto cap = set.cap && f.capped ? extent(models, set.cap->symbol) : std::nullopt;
            const f32 cap_height = cap ? cap->second.y : 0.0f;
            std::vector<std::pair<const SupportPiece*, f32>> beams;
            for (const SupportPiece& b : set.beams)
            {
                if (const auto e = extent(models, b.symbol))
                    beams.emplace_back(&b, e->second.x - e->first.x);
            }
            std::ranges::sort(beams, { }, &std::pair<const SupportPiece*, f32>::second);
            const auto top_size = set.top ? extent(models, set.top->symbol) : std::nullopt;
            const f32 overhang = top_size ? (top_size->second.x - top_size->first.x) / 2 : 0.0f;

            std::set<u32> groups_built;
            std::set<std::pair<Tile, i32>> thirds_built;
            const auto base_level = static_cast<f32>(*object->base_height);
            for (const SupportPoint& p : f.points)
            {
                if (groups_built.contains(p.group))
                    continue;
                const glm::vec2 top_spot = spot_in_tiles(*object, f, p.top, SpotFit::Exact);
                const glm::vec2 column_spot = spot_in_tiles(*object, f, p.column, SpotFit::MiddleOfThird);
                const Tile top_tile = tile_of(top_spot);
                const Tile tile = tile_of(column_spot);
                if (f.alternate_tiles && ((top_tile.col ^ top_tile.row) & 1) == 0)
                    continue;
                const glm::vec2 third_in_tile = (column_spot - glm::vec2 { static_cast<f32>(tile.col), static_cast<f32>(tile.row) }) * static_cast<f32>(kCells);
                const i32 third = std::clamp(static_cast<i32>(third_in_tile.x), 0, kCells - 1) + kCells * std::clamp(static_cast<i32>(third_in_tile.y), 0, kCells - 1);

                const glm::vec2 off_middle { p.top.within[0] - 0.5f, p.top.within[1] - 0.5f };
                const bool straight = !p.hanging && p.top.square == p.column.square && p.top.cell == p.column.cell && glm::dot(off_middle, off_middle) < kStraightUnder * kStraightUnder;
                if (!straight && p.straight_only)
                    continue;
                f32 exact_top = base_level + (straight ? p.top_height : p.column_height);
                if (cap)
                    exact_top -= cap_height;
                const auto top = static_cast<i32>(std::ceil(exact_top));
                // In the way: what is on the column's tile with its underside under the column's top.
                const auto here = covering.find(tile);
                std::span<const Covered> below = here != covering.end() ? std::span<const Covered>(here->second) : std::span<const Covered>(none);
                const auto first_below = std::ranges::find_if(below, [&](const Covered& b) { return static_cast<f32>(b.low) < exact_top; });
                const auto base = column_base(below.subspan(static_cast<usize>(first_below - below.begin())), ground_paths, grid, tile, object, exact_top, set, third, false);
                if (!base || static_cast<f32>(base->first) >= exact_top || top - base->first >= kTallestColumn)
                    continue;
                groups_built.insert(p.group);
                if (!thirds_built.emplace(tile, third).second || !wanted(tile))
                    continue;
                for (const Piece& piece : column(set, base->first, top, base->second, exact_top))
                    build.stand(*object, *piece.piece, column_spot, piece.level, ModelTurn::from(object->rotation), piece.stretch);
                if (cap)
                    build.stand(*object, *set.cap, column_spot, exact_top, ModelTurn::from(object->rotation), 1);
                if (!straight && !beams.empty())
                {
                    const glm::vec3 from = in_park(grid, top_spot, base_level + p.top_height);
                    const glm::vec3 to = in_park(grid, column_spot, base_level + p.column_height);
                    const f32 across = glm::length(glm::vec2 { to.x - from.x, to.z - from.z });
                    const auto fits = std::ranges::find_if(beams, [&](const auto& b) { return b.second >= across; });
                    const auto& [piece, length] = fits != beams.end() ? *fits : beams.back();
                    build.beam(*object, *piece, length, from, to, overhang);
                }
                build.out.of.resize(build.out.pieces.size(), { object->id, tile });
            }
        }
        return std::move(build.out);
    }

} // namespace rct3::park::support
