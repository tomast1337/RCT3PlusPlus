// SPDX-License-Identifier: GPL-3.0-or-later
//
// Placing a saved design of a tracked ride whole. Its ghost may be refused for one reason at a time, told to the
// player in this order: some of it is off land the park owns, it stands too high over the ground, it is in the way
// of something standing, it passes into the ground (unless the player asked for the ground to be cut), or its pieces
// that run into a pool, as a slide's way back to its lift does, are not on a pool's edge.
#pragma once

#include "park/pools/pools.hpp"
#include "park/track/pieces.hpp"
#include "save/model/world.hpp"

#include <cstdint>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::park
{

    enum class DesignRefusal : std::uint8_t
    {
        None,
        NotOwned,
        TooHigh,
        Intersects,
        IntoGround,
        OffPoolEdge,
    };

    /// The string key that tells the player why; empty for none.
    [[nodiscard]] std::string_view refusal_key(DesignRefusal refusal) noexcept;

    /// How far, in whole units, a design's lowest piece may stand over the ground under any of its pieces.
    inline constexpr float kDesignRaise = 12; ///< Chosen by eye: a gentle valley is spanned, a design lifted high is not

    /// Whether a design whose lowest piece stands at `lowest` stands too high over `grounds`, the ground under each
    /// of its pieces. On level ground a design stands as it was built and is never too high.
    [[nodiscard]] bool stands_too_high(float lowest, std::span<const float> grounds) noexcept;

    /// Where the piece `object`, built of `record`, joins a pool beside it.
    [[nodiscard]] std::vector<pools::PoolOpening> piece_pool_openings(const save::PlacedObject& object, const track::PieceRecord& record);

    /// Whether each of `openings` has a square of `map` across its side, at the opening's height.
    [[nodiscard]] bool meets_pools(const pools::PoolMap& map, std::span<const pools::PoolOpening> openings);

} // namespace rct3::park
