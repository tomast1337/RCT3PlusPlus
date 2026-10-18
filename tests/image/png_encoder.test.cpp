// SPDX-License-Identifier: GPL-3.0-or-later
#include "image/png_encoder.hpp"

#include "image/picture_file.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <filesystem>
#include <fstream>

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

TEST_CASE("numbered pictures go one past the highest number and never replace a file")
{
    namespace fs = std::filesystem;
    const fs::path dir = fs::temp_directory_path() / "rct3pp_numbered_png";
    fs::remove_all(dir);
    fs::create_directories(dir);
    const ovl::RgbaImage image { 1, 1, { 1, 2, 3, 255 } };

    const auto first = image::save_numbered_png(dir, "shot_", image);
    REQUIRE(first);
    CHECK(first->filename() == "shot_0001.png");
    {
        std::ofstream(dir / "shot_0007.png") << "kept";
    }
    {
        std::ofstream(dir / "shot_extra.png") << "not a number";
    }
    {
        std::ofstream(dir / "other_0042.png") << "another prefix";
    }
    const auto next = image::save_numbered_png(dir, "shot_", image);
    REQUIRE(next);
    CHECK(next->filename() == "shot_0008.png");
    CHECK(fs::file_size(dir / "shot_0007.png") == 4);
    CHECK_FALSE(image::save_numbered_png(dir, "shot_", { }));
    fs::remove_all(dir);
}
