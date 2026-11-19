// SPDX-License-Identifier: GPL-3.0-or-later
//
// How good a pool complex is, and what its ticket is worth to a guest.
//
// Its excitement (0 to 1) comes from the pool's design: how much water it has, how its depth varies, how
// many of its water squares have shaped walls (curved or cut corners, or a corner jutting out), lamps for
// its size, and ladders and lifeguards for its water. Dirt takes it down. Its worth adds to a plain ticket's
// a share for the excitement and some for each feature (spa pools, high diving boards, slides, sun loungers,
// showers, lazy rivers), each kind counting only up to a few. Every weight is chosen by eye.
#pragma once

#include "core/types.hpp"
#include "park/pools/pools.hpp"

#include <cstddef>
#include <cstdint>
#include <span>

namespace rct3::park::pools
{

    /// What a pool complex has, counted from its squares and the extras on them.
    struct PoolTally
    {
        i32 squares = 0; ///< Of every kind
        i32 water_squares = 0;
        i32 depth_sum = 0;  ///< Of all squares' depths
        i32 depth_sum2 = 0; ///< Of their squares
        i32 shaped = 0;     ///< Water squares with a curved or cut corner, or at a corner jutting out
        i32 lamps = 0;
        i32 ladders = 0;
        i32 lifeguards = 0;
        i32 jacuzzis = 0;
        i32 high_dives = 0;
        i32 loungers = 0;
        i32 showers = 0;
    };

    /// The tally of the complex made of `squares`; extras not on one of them are left out.
    [[nodiscard]] PoolTally pool_tally(std::span<const PoolSquare> squares, std::span<const PoolExtra> extras);

    /// The complex's excitement, 0 to 1, with its water `dirt` from clean (0) to a health hazard (1).
    [[nodiscard]] f32 pool_excitement(const PoolTally& tally, f32 dirt);

    /// The most a guest pays to go in, hundredths, with `slides` ending in its water and `rivers` lazy rivers joined
    /// to it.
    [[nodiscard]] i64 pool_worth(const PoolTally& tally, usize slides, usize rivers, f32 dirt);

} // namespace rct3::park::pools
