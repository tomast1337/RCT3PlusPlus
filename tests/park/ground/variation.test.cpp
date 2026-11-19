// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/variation.hpp"

#include "core/types.hpp"

#include <cstdint>
#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{
    i32 at(const std::vector<std::byte>& t, u32 x, u32 y, u32 c)
    {
        return std::to_integer<i32>(t[((static_cast<usize>(y % kVariationSize) * kVariationSize + x % kVariationSize) * 4) + c]);
    }
} // namespace

TEST_CASE("the variation picture is smooth, tiles, spreads and differs per channel")
{
    const auto t = variation_texels();
    REQUIRE(t.size() == static_cast<usize>(kVariationSize) * kVariationSize * 4);
    CHECK(t == variation_texels());
    i32 low = 255;
    i32 high = 0;
    i32 differ = 0;
    for (u32 y = 0; y < kVariationSize; ++y)
    {
        for (u32 x = 0; x < kVariationSize; ++x)
        {
            // Neighbours, also across the wrap, stay close.
            CHECK(std::abs(at(t, x, y, 0) - at(t, x + 1, y, 0)) <= 25); // A full swing over a feature (16 texels) at the steepest part of its ease
            CHECK(std::abs(at(t, x, y, 0) - at(t, x, y + 1, 0)) <= 25); // A full swing over a feature (16 texels) at the steepest part of its ease
            low = std::min(low, at(t, x, y, 0));
            high = std::max(high, at(t, x, y, 0));
            differ += at(t, x, y, 0) != at(t, x, y, 1) ? 1 : 0;
        }
    }
    CHECK(high - low > 100);
    CHECK(differ > static_cast<i32>(kVariationSize * kVariationSize / 2));
}
