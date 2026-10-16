// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_variation.hpp"

#include <cstdlib>
#include <doctest/doctest.h>

using namespace rct3::park;

namespace
{
    int at(const std::vector<std::byte>& t, std::uint32_t x, std::uint32_t y, std::uint32_t c)
    {
        return std::to_integer<int>(t[((static_cast<std::size_t>(y % kVariationSize) * kVariationSize + x % kVariationSize) * 4) + c]);
    }
} // namespace

TEST_CASE("the variation picture is smooth, tiles, spreads and differs per channel")
{
    const auto t = variation_texels();
    REQUIRE(t.size() == static_cast<std::size_t>(kVariationSize) * kVariationSize * 4);
    CHECK(t == variation_texels());
    int low = 255;
    int high = 0;
    int differ = 0;
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
    CHECK(differ > static_cast<int>(kVariationSize * kVariationSize / 2));
}
