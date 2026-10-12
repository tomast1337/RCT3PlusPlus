// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/texture_pixels.hpp"

#include <doctest/doctest.h>

using namespace rct3::ovl;

TEST_CASE("flipping reverses the row order and keeps each row intact")
{
    RgbaImage image { 2, 3, { } };
    for (std::uint8_t row = 0; row < 3; ++row)
        image.pixels.insert(image.pixels.end(), { row, 1, 2, 3, row, 5, 6, 7 });
    const RgbaImage flipped = flipped_vertically(image);
    REQUIRE(flipped.pixels.size() == image.pixels.size());
    for (std::uint8_t row = 0; row < 3; ++row)
    {
        CHECK(flipped.pixels[row * 8] == 2 - row);
        CHECK(flipped.pixels[row * 8 + 5] == 5);
    }
    CHECK(flipped_vertically(flipped).pixels == image.pixels);
}

TEST_CASE("textures without a node have no pixels")
{
    const TexturePixels empty;
    CHECK_FALSE(empty.first_frame(Texture { }).has_value());
}
