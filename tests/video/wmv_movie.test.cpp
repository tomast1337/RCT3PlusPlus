// SPDX-License-Identifier: GPL-3.0-or-later
#include "video/wmv_movie.hpp"

#include "../audio/tiny_wmv.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::video;

TEST_CASE("Y, U and V become RGBA: black, white and the primaries")
{
    // Two by two pixels share one U and V; four of them, side by side.
    const std::array<std::uint8_t, 16> y = { 16, 16, 235, 235, 81, 81, 41, 41, 16, 16, 235, 235, 81, 81, 41, 41 };
    std::array<std::uint8_t, 4> u = { 128, 128, 90, 240 };
    std::array<std::uint8_t, 4> v = { 128, 128, 240, 110 };
    std::array<std::uint8_t, 8 * 2 * 4> rgba { };
    yuv_to_rgba(8, 2, y, 8, u, 4, v, 4, rgba);
    const auto pixel = [&](int col, int row) { return std::array { rgba[(row * 8 + col) * 4], rgba[(row * 8 + col) * 4 + 1], rgba[(row * 8 + col) * 4 + 2], rgba[(row * 8 + col) * 4 + 3] }; };
    CHECK(pixel(0, 0) == std::array<std::uint8_t, 4> { 0, 0, 0, 255 });
    CHECK(pixel(3, 1) == std::array<std::uint8_t, 4> { 255, 255, 255, 255 });
    const auto red = pixel(4, 0);
    CHECK(red[0] > 240);
    CHECK(red[1] < 15);
    CHECK(red[2] < 15);
    const auto blue = pixel(6, 1);
    CHECK(blue[2] > 240);
    CHECK(blue[0] < 15);
    CHECK(blue[1] < 15);
}

TEST_CASE("a movie that is not WMV3, or not ASF at all, is refused")
{
    const auto bytes = std::as_bytes(std::span(rct3::test::kTinyWmv));
    const auto other = WmvMovie::open(std::vector(bytes.begin(), bytes.end()));
    REQUIRE_FALSE(other);
    CHECK(other.error().find("not Windows Media Video 9") != std::string::npos);
    CHECK_FALSE(WmvMovie::open(std::vector<std::byte>(100, std::byte { 7 })));
    CHECK_FALSE(WmvMovie::open(std::vector<std::byte> { }));
    CHECK_FALSE(WmvMovie::open("no such movie.wmv"));
}
