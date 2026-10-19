// SPDX-License-Identifier: GPL-3.0-or-later
#include "park/ground_textures.hpp"

#include <doctest/doctest.h>
#include <string_view>
#include <vector>

using namespace rct3::park;

TEST_CASE("cliff faces are told from land surfaces by their name")
{
    CHECK(ground_paint("Cliff_03") == GroundPaint::Cliff);
    CHECK(ground_paint("Terrain_07") == GroundPaint::Surface);
    CHECK(ground_paint("Sand") == GroundPaint::Surface);
    CHECK(ground_key(GroundPaint::Cliff, 3) != ground_key(GroundPaint::Surface, 3));
}

TEST_CASE("an overlay that is not there is an error")
{
    GroundImages images;
    CHECK_FALSE(add_ground_images(images, "no such overlay.ovl").has_value());
    CHECK_FALSE(ground_surfaces("no such overlay.ovl").has_value());
    CHECK(images.empty());
}

TEST_CASE("a queue style's ground is its picture that is not a mask, the one named after it first")
{
    const std::vector<std::string_view> own { "Plain.Alpha", "Plain" };
    CHECK(queue_picture(own, "Other") == 1u);
    const std::vector<std::string_view> named { "Extra", "GroundSet.alpha", "GroundSet" };
    CHECK(queue_picture(named, "Set") == 2u);
    CHECK(queue_picture(named, "Unknown") == 0u);
    const std::vector<std::string_view> masks { "A.ALPHA" };
    CHECK_FALSE(queue_picture(masks, "A"));
}

TEST_CASE("a queue's mask leaves its dark parts to the ground below")
{
    rct3::ovl::RgbaImage picture { 3, 1, { 9, 9, 9, 255, 9, 9, 9, 255, 9, 9, 9, 255 } };
    PaletteImage mask { .width = 3, .height = 1 };
    mask.palette[1] = { 255, 255, 255 };
    mask.palette[2] = { 102, 102, 102 };
    mask.indices = { 0, 1, 2 };
    apply_queue_mask(picture, mask);
    CHECK(picture.pixels[3] == 0);
    CHECK(picture.pixels[7] == 255);
    CHECK(picture.pixels[11] == 102);
    CHECK(picture.pixels[0] == 9);

    SUBCASE("a mask of another size is left unused")
    {
        rct3::ovl::RgbaImage other { 1, 1, { 9, 9, 9, 255 } };
        apply_queue_mask(other, mask);
        CHECK(other.pixels[3] == 255);
    }
}

TEST_CASE("each cliff face gets the tunnel pictures nearest it in colour")
{
    const auto plain = [](std::uint8_t r, std::uint8_t g, std::uint8_t b) { return rct3::ovl::RgbaImage { 1, 1, { r, g, b, 255 } }; };
    GroundImages images;
    images.emplace(ground_key(GroundPaint::Cliff, 0), plain(200, 40, 30));
    images.emplace(ground_key(GroundPaint::Cliff, 4), plain(20, 30, 220));
    const std::vector<TunnelPictures> styles { { plain(10, 10, 250), plain(0, 0, 1) }, { plain(250, 10, 10), plain(1, 0, 0) } };
    add_tunnel_images(images, styles);
    CHECK(images.at(ground_key(GroundPaint::TunnelWall, 0)).pixels[0] == 250);
    CHECK(images.at(ground_key(GroundPaint::TunnelCeiling, 0)).pixels[0] == 1);
    CHECK(images.at(ground_key(GroundPaint::TunnelWall, 4)).pixels[2] == 250);
    CHECK_FALSE(images.contains(ground_key(GroundPaint::TunnelWall, 1)));
}
