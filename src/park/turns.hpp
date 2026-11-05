// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which way things face on the park's grid, as types, so a side of a square and a model's turn cannot be mixed
// up with each other or with a count of anything else. Use `.value` for the plain number; each is marked kUnit,
// so the park file keeps it as its bare number.
//
// Side: one of the four sides of a grid square, which is also the way to the square across it, so it doubles
// as a heading. Counted clockwise as the park is drawn: 0 towards the next row, 1 the next column, 2 the row
// before, 3 the column before. The path builder, the pools, path decks and edge walls all count sides this way.
//
// ModelTurn: how far a placed object's model is turned, in quarter turns, as placed objects store their
// rotation. What a turn of 0 faces depends on the model; for a pool piece and a path deck piece, turning by
// r moves each of its sides r places on, so a turn and the side it brings the model's side 0 to are the same
// count (ModelTurn::toward).
#pragma once

#include "save/model/world.hpp"

#include <array>
#include <cstdint>
#include <optional>

namespace rct3::park
{

    struct Side
    {
        static constexpr bool kUnit = true;
        std::int32_t value = 0; ///< 0 to 3

        static const Side kNextRow;
        static const Side kNextCol;
        static const Side kRowBefore;
        static const Side kColBefore;

        /// The side `quarter_turns` clockwise from side 0, any whole number of turns: the edge from a raw count.
        [[nodiscard]] static constexpr Side from(std::int32_t quarter_turns) noexcept
        {
            return { quarter_turns & 3 };
        }
        /// The side `quarter_turns` clockwise from this one (anticlockwise when negative).
        [[nodiscard]] constexpr Side turned(std::int32_t quarter_turns) const noexcept
        {
            return from(value + quarter_turns);
        }
        [[nodiscard]] constexpr Side opposite() const noexcept
        {
            return turned(2);
        }
        /// The next side clockwise: a right turn for something heading this way.
        [[nodiscard]] constexpr Side right() const noexcept
        {
            return turned(1);
        }
        [[nodiscard]] constexpr Side left() const noexcept
        {
            return turned(-1);
        }
        /// Whether the side is towards a column (the next one or the one before), so a heading this way runs
        /// along a row.
        [[nodiscard]] constexpr bool on_columns() const noexcept
        {
            return (value & 1) != 0;
        }
        /// This side's bit in a set of sides kept as bits, side 0 the lowest.
        [[nodiscard]] constexpr std::uint32_t bit() const noexcept
        {
            return 1u << (value & 3);
        }
        /// Columns and rows from a square to the one across this side.
        [[nodiscard]] constexpr std::array<int, 2> step() const noexcept
        {
            constexpr std::array<std::array<int, 2>, 4> kSteps { { { 0, 1 }, { 1, 0 }, { 0, -1 }, { -1, 0 } } };
            return kSteps[static_cast<std::size_t>(value & 3)];
        }
        /// The tile across this side of `tile`.
        [[nodiscard]] constexpr save::Tile beyond(save::Tile tile) const noexcept
        {
            const auto [dc, dr] = step();
            return { tile.col + dc, tile.row + dr };
        }
        /// The side a step of (`dc`, `dr`) crosses; none unless it is one square to a side.
        [[nodiscard]] static constexpr std::optional<Side> towards(int dc, int dr) noexcept
        {
            for (std::int32_t s = 0; s < 4; ++s)
                if (Side { s }.step() == std::array { dc, dr })
                    return Side { s };
            return std::nullopt;
        }
        friend constexpr bool operator==(Side, Side) = default;
    };
    inline constexpr Side Side::kNextRow { 0 };
    inline constexpr Side Side::kNextCol { 1 };
    inline constexpr Side Side::kRowBefore { 2 };
    inline constexpr Side Side::kColBefore { 3 };

    /// The four sides, from side 0 clockwise.
    inline constexpr std::array<Side, 4> kSides { Side::kNextRow, Side::kNextCol, Side::kRowBefore, Side::kColBefore };

    struct ModelTurn
    {
        static constexpr bool kUnit = true;
        std::int32_t value = 0; ///< Quarter turns, 0 to 3

        /// The turn of `quarter_turns`, any whole number of them.
        [[nodiscard]] static constexpr ModelTurn from(std::int32_t quarter_turns) noexcept
        {
            return { quarter_turns & 3 };
        }
        /// For a model whose sides turn with it (a pool piece, a path deck piece): the turn that brings its side 0
        /// to `side`.
        [[nodiscard]] static constexpr ModelTurn toward(Side side) noexcept
        {
            return { side.value };
        }
        /// For a model whose sides turn with it: the side this turn brings its side 0 to.
        [[nodiscard]] constexpr Side facing() const noexcept
        {
            return Side::from(value);
        }
        /// This turn and `quarter_turns` more.
        [[nodiscard]] constexpr ModelTurn turned(std::int32_t quarter_turns) const noexcept
        {
            return from(value + quarter_turns);
        }
        friend constexpr bool operator==(ModelTurn, ModelTurn) = default;
    };

} // namespace rct3::park
