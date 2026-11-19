// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/structure_placement.hpp"

#include "core/types.hpp"
#include "park/item_category.hpp"
#include "park/object/placement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>

namespace rct3::park
{

    namespace
    {

        const object::ObjectModel* model_of(const save::model::PlacedObject& o, const object::ObjectModels& models)
        {
            const auto it = models.find(object::model_key(o));
            return it == models.end() ? nullptr : &it->second;
        }

        /// The tiles an item covers along the park's columns and rows, as it is turned.
        std::array<i32, 2> span_of(i32 rotation, const object::ObjectModel* model)
        {
            const std::array<i32, 2> squares = model ? model->squares : std::array<i32, 2> { 1, 1 };
            return (rotation & 1) != 0 ? std::array { squares[1], squares[0] } : squares;
        }

        /// A point of the box, in tiles from its first corner, turned with it: where it lies in the turned box.
        glm::vec2 turned(glm::vec2 p, ModelTurn turn, const save::model::Structure& s)
        {
            const auto cols = static_cast<f32>(s.cols);
            const auto rows = static_cast<f32>(s.rows);
            switch (turn.value)
            {
                case 1:
                    return { p.y, cols - p.x };
                case 2:
                    return { cols - p.x, rows - p.y };
                case 3:
                    return { rows - p.y, p.x };
                default:
                    return p;
            }
        }

        /// The first corner of the turned box centred on `at`: the box's middle tile, rounded down before it
        /// turns, stays on `at`.
        glm::vec2 corner(const save::model::Structure& s, save::model::Tile at, ModelTurn turn)
        {
            const i32 c = s.cols / 2;
            const i32 r = s.rows / 2;
            switch (turn.value)
            {
                case 1:
                    return { static_cast<f32>(at.col - r), static_cast<f32>(at.row + c + 1 - s.cols) };
                case 2:
                    return { static_cast<f32>(at.col + c + 1 - s.cols), static_cast<f32>(at.row + r + 1 - s.rows) };
                case 3:
                    return { static_cast<f32>(at.col + r + 1 - s.rows), static_cast<f32>(at.row - c) };
                default:
                    return { static_cast<f32>(at.col - c), static_cast<f32>(at.row - r) };
            }
        }

    } // namespace

    bool goes_in_structure(u32 category) noexcept
    {
        constexpr std::array kLeft { 6u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 21u, 23u, 24u, 25u, 26u, 27u, 30u, 31u, 32u, 33u, 37u, 38u, 39u, 40u };
        return !std::ranges::contains(kLeft, category);
    }

    save::model::Structure pick_structure(std::span<const save::model::Tile> marked, std::span<const save::model::PlacedObject> objects, const object::ObjectModels& models, const save::model::TerrainGrid& grid)
    {
        save::model::Structure s;
        if (marked.empty())
            return s;
        const auto [low_col, high_col] = std::ranges::minmax(marked, { }, &save::model::Tile::col);
        const auto [low_row, high_row] = std::ranges::minmax(marked, { }, &save::model::Tile::row);
        s.cols = high_col.col - low_col.col + 1;
        s.rows = high_row.row - low_row.row + 1;
        s.tiles.assign(static_cast<usize>(s.cols) * static_cast<usize>(s.rows), 0);
        for (const save::model::Tile t : marked)
            s.tiles[static_cast<usize>(t.row - low_row.row) * static_cast<usize>(s.cols) + static_cast<usize>(t.col - low_col.col)] = 1;
        for (const save::model::PlacedObject& o : objects)
        {
            if (o.fixture || o.hidden || !o.free_standing() || o.symbol.empty())
                continue;
            const object::ObjectModel* model = model_of(o, models);
            if (!model || !goes_in_structure(model->category))
                continue;
            const auto span = span_of(o.rotation, model);
            bool on_mark = false;
            for (i32 c = 0; c < span[0] && !on_mark; ++c)
                for (i32 r = 0; r < span[1] && !on_mark; ++r)
                    on_mark = s.has_tile(o.col + c - low_col.col, o.row + r - low_row.row);
            if (!on_mark)
                continue;
            // The height it stands at, less the nudge its own model adds.
            const f32 y = object::object_transform(o, static_cast<object::Positioning>(model->positioning), grid, model->squares)[3].y - o.height_adjust.value_or(0);
            s.pieces.push_back(
                {
                    .symbol = o.symbol,
                    .overlay = o.overlay,
                    .col = o.col - low_col.col,
                    .row = o.row - low_row.row,
                    .height = static_cast<i32>(std::lround(y)),
                    .rotation = o.rotation & 3,
                    .quadrant = o.quadrant & 3,
                    .colours = o.colours,
                });
        }
        save::model::rebase_heights(s);
        return s;
    }

    save::model::Tile structure_tile(const save::model::Structure& structure, save::model::Tile at, ModelTurn turn, i32 col, i32 row)
    {
        const glm::vec2 p = corner(structure, at, turn) + turned({ static_cast<f32>(col) + 0.5f, static_cast<f32>(row) + 0.5f }, turn, structure);
        return { static_cast<i32>(std::floor(p.x)), static_cast<i32>(std::floor(p.y)) };
    }

    std::vector<save::model::PlacedObject> structure_objects(const save::model::Structure& structure, save::model::Tile at, ModelTurn turn, i32 height, const object::ObjectModels& models)
    {
        std::vector<save::model::PlacedObject> out;
        out.reserve(structure.pieces.size());
        const glm::vec2 origin = corner(structure, at, turn);
        for (const save::model::StructurePiece& p : structure.pieces)
        {
            save::model::PlacedObject o;
            o.symbol = p.symbol;
            o.overlay = p.overlay;
            o.rotation = ModelTurn::from(p.rotation).turned(turn.value).value;
            o.colours = p.colours;
            o.base_height = height + p.height;
            o.height_offset = p.height;
            o.force_absolute_height = true;
            // Its tiles turn with the box; it stands from the first of them.
            const auto span = span_of(p.rotation, model_of(o, models));
            const glm::vec2 a = turned({ static_cast<f32>(p.col), static_cast<f32>(p.row) }, turn, structure);
            const glm::vec2 b = turned({ static_cast<f32>(p.col + span[0]), static_cast<f32>(p.row + span[1]) }, turn, structure);
            o.col = static_cast<i32>(origin.x + std::min(a.x, b.x));
            o.row = static_cast<i32>(origin.y + std::min(a.y, b.y));
            // A quarter of a tile turns with it too.
            const glm::vec2 q = turned({ static_cast<f32>(p.col) + ((p.quadrant & 1) != 0 ? 0.75f : 0.25f), static_cast<f32>(p.row) + ((p.quadrant & 2) != 0 ? 0.75f : 0.25f) }, turn, structure);
            o.quadrant = (q.x - std::floor(q.x) >= 0.5f ? 1 : 0) + (q.y - std::floor(q.y) >= 0.5f ? 2 : 0);
            out.push_back(std::move(o));
        }
        return out;
    }

    StructureProblem structure_problem(const save::model::Structure& structure, save::model::Tile at, ModelTurn turn, std::span<const save::model::PlacedObject> placed, const object::ObjectModels& models, const path::SolidTiles& solids, std::span<const save::model::PathTile> paths, const save::model::TerrainGrid& grid)
    {
        for (i32 row = 0; row < structure.rows; ++row)
            for (i32 col = 0; col < structure.cols; ++col)
            {
                if (!structure.has_tile(col, row))
                    continue;
                const save::model::Tile t = structure_tile(structure, at, turn, col, row);
                if (!grid.contains(t) || grid.at(t.col, t.row).ownership() != save::model::Ownership::Owned)
                    return StructureProblem::NotOwned;
            }
        const path::Clash c = path::clash(path::solid_tiles(placed, models, grid), solids, paths, grid);
        return c == path::Clash::None ? StructureProblem::None : c == path::Clash::Path ? StructureProblem::Path : StructureProblem::Blocked;
    }

} // namespace rct3::park
