// SPDX-License-Identifier: GPL-3.0-or-later
//
// How good a pool complex is, and what its ticket is worth to a guest.
//
// Its excitement (0 to 1) comes from the pool's design: how much water it has, how its depth varies, how
// many of its water squares have shaped walls (curved or cut corners, or a corner jutting out), lamps for
// its size, and ladders and lifeguards for its water. Dirt takes it down. Its worth adds to a plain ticket's
// a share for the excitement and some for each feature (spa pools, high diving boards, slides, sun loungers,
// showers), each kind counting only up to a few. Every weight is chosen by eye.
#pragma once

#include "park/pools.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rct3::park
{

    /// What a pool complex has, counted from its squares and the extras on them.
    struct PoolTally
    {
        std::int32_t squares = 0;    ///< Of every kind
        std::int32_t water = 0;      ///< Water squares
        std::int32_t depth_sum = 0;  ///< Of all squares' depths
        std::int32_t depth_sum2 = 0; ///< Of their squares
        std::int32_t shaped = 0;     ///< Water squares with a curved or cut corner, or at a corner jutting out
        std::int32_t lamps = 0;
        std::int32_t ladders = 0;
        std::int32_t lifeguards = 0;
        std::int32_t jacuzzis = 0;
        std::int32_t high_dives = 0;
        std::int32_t loungers = 0;
        std::int32_t showers = 0;
    };

    /// The tally of the complex made of `squares`; extras not on one of them are left out.
    [[nodiscard]] PoolTally pool_tally(std::span<const PoolSquare> squares, std::span<const PoolExtra> extras);

    /// The complex's excitement, 0 to 1, with its water `dirt` from clean (0) to a health hazard (1).
    [[nodiscard]] float pool_excitement(const PoolTally& tally, float dirt);

    /// The most a guest pays to go in, hundredths, with `slides` ending in its water.
    [[nodiscard]] std::int64_t pool_worth(const PoolTally& tally, std::size_t slides, float dirt);

} // namespace rct3::park
