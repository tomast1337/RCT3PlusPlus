// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/structure_placement.hpp"

#include "park/item_category.hpp"
#include "park/object_placement.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>

namespace rct3::park
{

    namespace
    {

        const ObjectModel* model_of(const save::PlacedObject& o, const ObjectModels& models)
        {
            const auto it = models.find(model_key(o));
            return it == models.end() ? nullptr : &it->second;
        }

        /// The tiles an item covers along the park's columns and rows, as it is turned.
        std::array<std::int32_t, 2> span_of(std::int32_t rotation, const ObjectModel* model)
        {
            const std::array<std::int32_t, 2> squares = model ? model->squares : std::array<std::int32_t, 2> { 1, 1 };
            return (rotation & 1) != 0 ? std::array { squares[1], squares[0] } : squares;
        }

        /// A point of the box, in tiles from its first corner, turned with it: where it lies in the turned box.
        glm::vec2 turned(glm::vec2 p, std::int32_t turns, const save::Structure& s)
        {
            const auto cols = static_cast<float>(s.cols);
            const auto rows = static_cast<float>(s.rows);
            switch (turns & 3)
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
        glm::vec2 corner(const save::Structure& s, save::Tile at, std::int32_t turns)
        {
            const std::int32_t c = s.cols / 2;
            const std::int32_t r = s.rows / 2;
            switch (turns & 3)
            {
                case 1:
                    return { static_cast<float>(at.col - r), static_cast<float>(at.row + c + 1 - s.cols) };
                case 2:
                    return { static_cast<float>(at.col + c + 1 - s.cols), static_cast<float>(at.row + r + 1 - s.rows) };
                case 3:
                    return { static_cast<float>(at.col + r + 1 - s.rows), static_cast<float>(at.row - c) };
                default:
                    return { static_cast<float>(at.col - c), static_cast<float>(at.row - r) };
            }
        }

    } // namespace

    bool goes_in_structure(std::uint32_t category) noexcept
    {
        constexpr std::array kLeft { 6u, 12u, 13u, 14u, 15u, 16u, 17u, 18u, 19u, 21u, 23u, 24u, 25u, 26u, 27u, 30u, 31u, 32u, 33u, 37u, 38u, 39u, 40u };
        return !std::ranges::contains(kLeft, category);
    }

    save::Structure pick_structure(std::span<const save::Tile> marked, std::span<const save::PlacedObject> objects, const ObjectModels& models, const save::TerrainGrid& grid)
    {
        save::Structure s;
        if (marked.empty())
            return s;
        const auto [low_col, high_col] = std::ranges::minmax(marked, { }, &save::Tile::col);
        const auto [low_row, high_row] = std::ranges::minmax(marked, { }, &save::Tile::row);
        s.cols = high_col.col - low_col.col + 1;
        s.rows = high_row.row - low_row.row + 1;
        s.tiles.assign(static_cast<std::size_t>(s.cols) * static_cast<std::size_t>(s.rows), 0);
        for (const save::Tile t : marked)
            s.tiles[static_cast<std::size_t>(t.row - low_row.row) * static_cast<std::size_t>(s.cols) + static_cast<std::size_t>(t.col - low_col.col)] = 1;
        for (const save::PlacedObject& o : objects)
        {
            if (o.fixture || o.hidden || !o.free_standing() || o.symbol.empty())
                continue;
            const ObjectModel* model = model_of(o, models);
            if (!model || !goes_in_structure(model->category))
                continue;
            const auto span = span_of(o.rotation, model);
            bool on_mark = false;
            for (std::int32_t c = 0; c < span[0] && !on_mark; ++c)
                for (std::int32_t r = 0; r < span[1] && !on_mark; ++r)
                    on_mark = s.has_tile(o.col + c - low_col.col, o.row + r - low_row.row);
            if (!on_mark)
                continue;
            // The height it stands at, less the nudge its own model adds.
            const float y = object_transform(o, static_cast<Positioning>(model->positioning), grid, model->squares)[3].y - o.height_adjust.value_or(0);
            s.pieces.push_back(
                {
                    .symbol = o.symbol,
                    .overlay = o.overlay,
                    .col = o.col - low_col.col,
                    .row = o.row - low_row.row,
                    .height = static_cast<std::int32_t>(std::lround(y)),
                    .rotation = o.rotation & 3,
                    .quadrant = o.quadrant & 3,
                    .colours = o.colours,
                });
        }
        save::rebase_heights(s);
        return s;
    }

    save::Tile structure_tile(const save::Structure& structure, save::Tile at, std::int32_t turns, std::int32_t col, std::int32_t row)
    {
        const glm::vec2 p = corner(structure, at, turns) + turned({ static_cast<float>(col) + 0.5f, static_cast<float>(row) + 0.5f }, turns, structure);
        return { static_cast<int>(std::floor(p.x)), static_cast<int>(std::floor(p.y)) };
    }

    std::vector<save::PlacedObject> structure_objects(const save::Structure& structure, save::Tile at, std::int32_t turns, std::int32_t height, const ObjectModels& models)
    {
        std::vector<save::PlacedObject> out;
        out.reserve(structure.pieces.size());
        const glm::vec2 origin = corner(structure, at, turns);
        for (const save::StructurePiece& p : structure.pieces)
        {
            save::PlacedObject o;
            o.symbol = p.symbol;
            o.overlay = p.overlay;
            o.rotation = (p.rotation + turns) & 3;
            o.colours = p.colours;
            o.base_height = height + p.height;
            o.height_offset = p.height;
            o.force_absolute_height = true;
            // Its tiles turn with the box; it stands from the first of them.
            const auto span = span_of(p.rotation, model_of(o, models));
            const glm::vec2 a = turned({ static_cast<float>(p.col), static_cast<float>(p.row) }, turns, structure);
            const glm::vec2 b = turned({ static_cast<float>(p.col + span[0]), static_cast<float>(p.row + span[1]) }, turns, structure);
            o.col = static_cast<std::int32_t>(origin.x + std::min(a.x, b.x));
            o.row = static_cast<std::int32_t>(origin.y + std::min(a.y, b.y));
            // A quarter of a tile turns with it too.
            const glm::vec2 q = turned({ static_cast<float>(p.col) + ((p.quadrant & 1) != 0 ? 0.75f : 0.25f), static_cast<float>(p.row) + ((p.quadrant & 2) != 0 ? 0.75f : 0.25f) }, turns, structure);
            o.quadrant = (q.x - std::floor(q.x) >= 0.5f ? 1 : 0) + (q.y - std::floor(q.y) >= 0.5f ? 2 : 0);
            out.push_back(std::move(o));
        }
        return out;
    }

    StructureProblem structure_problem(const save::Structure& structure, save::Tile at, std::int32_t turns, std::span<const save::PlacedObject> placed, const ObjectModels& models, const SolidTiles& solids, std::span<const save::PathTile> paths, const save::TerrainGrid& grid)
    {
        for (std::int32_t row = 0; row < structure.rows; ++row)
            for (std::int32_t col = 0; col < structure.cols; ++col)
            {
                if (!structure.has_tile(col, row))
                    continue;
                const save::Tile t = structure_tile(structure, at, turns, col, row);
                if (!grid.contains(t) || grid.at(t.col, t.row).ownership() != save::Ownership::Owned)
                    return StructureProblem::NotOwned;
            }
        const Clash c = clash(solid_tiles(placed, models, grid), solids, paths, grid);
        return c == Clash::None ? StructureProblem::None : c == Clash::Path ? StructureProblem::Path : StructureProblem::Blocked;
    }

} // namespace rct3::park
