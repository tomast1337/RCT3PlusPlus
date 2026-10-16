// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the player has asked the next piece of track to be, and which asks the track's open end allows.
//
// The player picks a curve, a slope the piece ends at and a bank it ends at, and whether it has a lift
// chain. A value is offered when some piece fitting the open end has it, whatever the other picks are.
// Picking a value that no piece has together with the other picks moves those to the nearest picks that
// have one, keeping the value picked. Pieces with a use of their own (stations, loops, helices, brakes and
// the like) are picked by name instead and stay picked until a curve, slope or bank is picked again.
#pragma once

#include "park/track_pieces.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <vector>

namespace rct3::park
{

    inline constexpr std::size_t kCurveChoices = 2 * kCurveSizes + 1;
    inline constexpr std::size_t kSlopeChoices = 2 * kSlopeSteps + 1;
    inline constexpr std::size_t kBankChoices = 2 * kBankSteps; ///< -3 to 4, upside down last

    /// Index of a curve, slope or bank among its choices: the leftmost or lowest first.
    [[nodiscard]] constexpr std::size_t curve_index(int curve)
    {
        return static_cast<std::size_t>(curve + kCurveSizes);
    }
    [[nodiscard]] constexpr std::size_t slope_index(int slope)
    {
        return static_cast<std::size_t>(slope + kSlopeSteps);
    }
    [[nodiscard]] constexpr std::size_t bank_index(int bank)
    {
        return static_cast<std::size_t>(bank + kBankSteps - 1);
    }

    struct TrackChoice
    {
        int curve = 0;
        int slope = 0; ///< That the piece ends at
        int bank = 0;  ///< That the piece ends at
        bool chain = false;
        std::optional<LaidPiece> named; ///< A piece picked by name, standing for the picks above

        friend bool operator==(const TrackChoice&, const TrackChoice&) = default;
    };

    /// Which picks the open end allows.
    struct ChoiceOptions
    {
        std::array<bool, kCurveChoices> curves { };
        std::array<bool, kSlopeChoices> slopes { };
        std::array<bool, kBankChoices> banks { };
        bool any_chain = false;       ///< Some fitting piece has a lift chain
        bool chain = false;           ///< The picked curve, slope and bank come with a lift chain
        std::vector<LaidPiece> named; ///< Pieces of their own use that fit, stations first
    };

    /// The three picks a piece is chosen by.
    enum class ChoiceAxis : std::uint8_t
    {
        Curve,
        Slope,
        Bank,
    };

    /// What `candidates` (pieces_after of `pieces`) offer for `choice`.
    [[nodiscard]] ChoiceOptions choice_options(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, const TrackChoice& choice);

    /// The piece `choice` asks for among `candidates`, if any. A piece fitting either way is laid as drawn
    /// first; without the asked chain, the piece without.
    [[nodiscard]] std::optional<LaidPiece> chosen_piece(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, const TrackChoice& choice);

    /// The picks after picking `value` on `axis`: the other picks move to the nearest that have a piece
    /// with it when the picks as they are have none.
    [[nodiscard]] TrackChoice pick_value(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice, ChoiceAxis axis, int value);

    /// The picks with the lift chain turned over, when the picked piece comes the other way.
    [[nodiscard]] TrackChoice pick_chain(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice);

    /// `choice` moved to the nearest picks that have a piece, as after a new open end. Unchanged when
    /// nothing fits at all.
    [[nodiscard]] TrackChoice settle_choice(std::span<const PieceShape> pieces, std::span<const LaidPiece> candidates, TrackChoice choice);

} // namespace rct3::park
