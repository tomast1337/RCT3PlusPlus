// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground/variation.hpp"

#include <cstdint>
#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3::park;
using namespace rct3::park::ground;

namespace
{
    std::int32_t at(const std::vector<std::byte>& t, std::uint32_t x, std::uint32_t y, std::uint32_t c)
    {
        return std::to_integer<std::int32_t>(t[((static_cast<std::size_t>(y % kVariationSize) * kVariationSize + x % kVariationSize) * 4) + c]);
    }
} // namespace

TEST_CASE("the variation picture is smooth, tiles, spreads and differs per channel")
{
    const auto t = variation_texels();
    REQUIRE(t.size() == static_cast<std::size_t>(kVariationSize) * kVariationSize * 4);
    CHECK(t == variation_texels());
    std::int32_t low = 255;
    std::int32_t high = 0;
    std::int32_t differ = 0;
    for (std::uint32_t y = 0; y < kVariationSize; ++y)
    {
        for (std::uint32_t x = 0; x < kVariationSize; ++x)
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
    CHECK(differ > static_cast<std::int32_t>(kVariationSize * kVariationSize / 2));
}
