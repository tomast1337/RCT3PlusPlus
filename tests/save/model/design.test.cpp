// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/design.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <vector>

using namespace rct3::save;

namespace
{
    /// The alpha of pixel `i`, and its grey level.
    std::uint8_t alpha(const std::vector<std::uint8_t>& rgba, std::size_t i)
    {
        return rgba.at(i * 4 + 3);
    }
    std::uint8_t level(const std::vector<std::uint8_t>& rgba, std::size_t i)
    {
        return rgba.at(i * 4);
    }
} // namespace

TEST_CASE("a packed design picture lays opaque pixels and skips clear runs, row after row")
{
    // Invented 3 x 2 picture: clear, full, clear | a third, clear, clear; then the end.
    const std::vector<std::uint8_t> packed { 64, 63, 64, 21, 0, 9, 65, 0, 0, 63 };
    const auto rgba = design_picture(3, 2, packed, { });
    REQUIRE(rgba.size() == 3 * 2 * 4);
    CHECK(alpha(rgba, 0) == 0);
    CHECK(alpha(rgba, 1) == 255);
    CHECK(level(rgba, 1) == 255);
    CHECK(alpha(rgba, 2) == 0);
    CHECK(alpha(rgba, 3) == 255);
    CHECK(level(rgba, 3) == 85);
    CHECK(alpha(rgba, 4) == 0); // The lone zero took the 9 with it; nothing after the end is drawn
    CHECK(alpha(rgba, 5) == 0);
}

TEST_CASE("an unpacked design picture is clear where its byte is zero")
{
    const std::vector<std::uint8_t> unpacked { 0, 200, 7, 0 };
    const auto rgba = design_picture(2, 2, { }, unpacked);
    REQUIRE(rgba.size() == 16);
    CHECK(alpha(rgba, 0) == 0);
    CHECK(level(rgba, 1) == 200);
    CHECK(alpha(rgba, 2) == 255);
    CHECK(design_picture(3, 2, { }, unpacked).empty()); // Not the size it claims
    CHECK(design_picture(0, 2, { }, unpacked).empty());
}
