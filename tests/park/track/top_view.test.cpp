// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/track/top_view.hpp"

#include <doctest/doctest.h>
#include <vector>

using namespace rct3::park::track;

namespace
{
    std::uint8_t alpha(const std::vector<std::uint8_t>& rgba, std::uint32_t size, std::uint32_t x, std::uint32_t y)
    {
        return rgba.at((std::size_t { y } * size + x) * 4 + 3);
    }
    std::uint8_t level(const std::vector<std::uint8_t>& rgba, std::uint32_t size, std::uint32_t x, std::uint32_t y)
    {
        return rgba.at((std::size_t { y } * size + x) * 4);
    }
} // namespace

TEST_CASE("a track's top view draws its line across the square, lighter where it is higher")
{
    // An invented straight climb along x, 20 metres long and 5 high.
    const std::vector<glm::vec3> points { { 0, 0, 0 }, { 20, 5, 0 } };
    constexpr std::uint32_t kSize = 64;
    const auto rgba = top_view(points, kSize);
    REQUIRE(rgba.size() == kSize * kSize * 4);
    // Fitted between the margins, along the middle row.
    CHECK(alpha(rgba, kSize, 8, 32) == 255);
    CHECK(alpha(rgba, kSize, 55, 32) == 255);
    CHECK(alpha(rgba, kSize, 32, 32) == 255);
    CHECK(alpha(rgba, kSize, 4, 32) == 0);
    CHECK(alpha(rgba, kSize, 32, 10) == 0);
    CHECK(level(rgba, kSize, 56, 32) == 255);
    CHECK(level(rgba, kSize, 8, 32) < level(rgba, kSize, 32, 32));
    CHECK(top_view({ }, kSize).empty());
}
