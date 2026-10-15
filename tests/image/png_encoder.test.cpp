// SPDX-License-Identifier: GPL-3.0-or-later
#include "image/png_encoder.hpp"

#include "image/picture_file.hpp"

#include <cstdint>
#include <doctest/doctest.h>

using namespace rct3;

TEST_CASE("an encoded image decodes to the same pixels")
{
    SUBCASE("a small one")
    {
        const ovl::RgbaImage image { 2, 2, { 255, 0, 0, 255, 0, 255, 0, 255, 0, 0, 255, 255, 255, 255, 255, 128 } };
        const auto decoded = image::decode_picture(image::encode_png(image));
        REQUIRE(decoded);
        CHECK(decoded->width == 2);
        CHECK(decoded->height == 2);
        CHECK(decoded->pixels == image.pixels);
    }
    SUBCASE("one larger than a stored block")
    {
        ovl::RgbaImage image { 200, 100, { } };
        image.pixels.resize(std::size_t { image.width } * image.height * 4);
        for (std::size_t i = 0; i < image.pixels.size(); ++i)
            image.pixels[i] = static_cast<std::uint8_t>(i * 7);
        const auto decoded = image::decode_picture(image::encode_png(image));
        REQUIRE(decoded);
        CHECK(decoded->pixels == image.pixels);
    }
}

TEST_CASE("an image without its pixels encodes to nothing")
{
    CHECK(image::encode_png({ }).empty());
    CHECK(image::encode_png({ 4, 4, { 1, 2, 3 } }).empty());
}
