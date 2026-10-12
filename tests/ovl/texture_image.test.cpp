// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <vector>

#include "ovl/texture_image.hpp"

using namespace rct3::ovl;

namespace {

// All values here are invented. Nothing is taken from real files.

void push(std::vector<std::byte>& out, std::initializer_list<unsigned> values) {
  for (auto v : values) out.push_back(static_cast<std::byte>(v));
}

void word(std::vector<std::byte>& out, std::uint32_t v) {
  for (int i = 0; i < 4; ++i) out.push_back(static_cast<std::byte>((v >> (8 * i)) & 0xFF));
}

struct Px {
  unsigned r, g, b, a;
  bool operator==(const Px&) const = default;
};

Px at(const RgbaImage& image, std::size_t x, std::size_t y) {
  const std::size_t i = (y * image.width + x) * 4;
  return {image.pixels[i], image.pixels[i + 1], image.pixels[i + 2], image.pixels[i + 3]};
}

// A colour block: endpoints, then four rows of index bits (two bits per pixel, first pixel lowest).
void colour_block(std::vector<std::byte>& out, unsigned c0, unsigned c1, unsigned row0, unsigned row1 = 0,
                  unsigned row2 = 0, unsigned row3 = 0) {
  push(out, {c0 & 0xFF, c0 >> 8, c1 & 0xFF, c1 >> 8, row0, row1, row2, row3});
}

std::vector<std::byte> palette_with(unsigned index, unsigned blue, unsigned green, unsigned red, unsigned extra) {
  std::vector<std::byte> palette(kFlexiPaletteSize, std::byte{0});
  palette[index * 4 + 0] = static_cast<std::byte>(blue);
  palette[index * 4 + 1] = static_cast<std::byte>(green);
  palette[index * 4 + 2] = static_cast<std::byte>(red);
  palette[index * 4 + 3] = static_cast<std::byte>(extra);
  return palette;
}

}  // namespace

TEST_CASE("flexi frames read palette entries as blue, green, red") {
  auto palette = palette_with(3, 10, 20, 30, 99);
  palette[7 * 4 + 0] = std::byte{1};
  palette[7 * 4 + 1] = std::byte{2};
  palette[7 * 4 + 2] = std::byte{3};
  const std::vector<std::byte> pixels = {std::byte{3}, std::byte{7}, std::byte{0}, std::byte{3}, std::byte{3},
                                         std::byte{7}};
  const std::vector<std::byte> alpha = {std::byte{255}, std::byte{128}, std::byte{0},
                                        std::byte{1},   std::byte{2},   std::byte{3}};

  FlexiFrame frame;
  frame.width = 3;
  frame.height = 2;
  frame.palette = palette;
  frame.pixels = pixels;

  SUBCASE("without an alpha plane the image is opaque and the spare palette byte is ignored") {
    const auto image = decode_flexi_frame(frame);
    REQUIRE(image.has_value());
    CHECK(image->width == 3);
    CHECK(image->height == 2);
    REQUIRE(image->pixels.size() == 24);
    CHECK(at(*image, 0, 0) == Px{30, 20, 10, 255});
    CHECK(at(*image, 1, 0) == Px{3, 2, 1, 255});
    CHECK(at(*image, 2, 0) == Px{0, 0, 0, 255});
    CHECK(at(*image, 2, 1) == Px{3, 2, 1, 255});
  }

  SUBCASE("an alpha plane supplies the alpha channel") {
    frame.alpha = std::span<const std::byte>(alpha);
    const auto image = decode_flexi_frame(frame);
    REQUIRE(image.has_value());
    CHECK(at(*image, 0, 0) == Px{30, 20, 10, 255});
    CHECK(at(*image, 1, 0).a == 128);
    CHECK(at(*image, 0, 1).a == 1);
    CHECK(at(*image, 2, 1).a == 3);
  }

  SUBCASE("short planes and bad sizes are refused") {
    FlexiFrame bad = frame;
    bad.pixels = std::span<const std::byte>(pixels).first(5);
    CHECK_FALSE(decode_flexi_frame(bad).has_value());

    bad = frame;
    bad.palette = std::span<const std::byte>(palette).first(1020);
    CHECK_FALSE(decode_flexi_frame(bad).has_value());

    bad = frame;
    bad.alpha = std::span<const std::byte>(alpha).first(4);
    CHECK_FALSE(decode_flexi_frame(bad).has_value());

    bad = frame;
    bad.width = 0;
    CHECK_FALSE(decode_flexi_frame(bad).has_value());

    bad = frame;
    bad.width = kMaxImageSide + 1;
    CHECK_FALSE(decode_flexi_frame(bad).has_value());
  }
}

TEST_CASE("raw pixels are stored as blue, green, red, alpha") {
  std::vector<std::byte> data;
  push(data, {1, 2, 3, 4, 5, 6, 7, 8});  // two pixels
  const auto image = decode_pixels(2, 2, 1, data);
  REQUIRE(image.has_value());
  CHECK(at(*image, 0, 0) == Px{3, 2, 1, 4});
  CHECK(at(*image, 1, 0) == Px{7, 6, 5, 8});

  CHECK_FALSE(decode_pixels(2, 2, 2, data).has_value());  // too short for 2 x 2
  CHECK_FALSE(decode_pixels(0, 2, 1, data).has_value());  // unknown format
  CHECK_FALSE(decode_pixels(2, 0, 1, data).has_value());
}

TEST_CASE("format 18 blocks decode with four colours or three and a transparent one") {
  SUBCASE("four colour mode") {
    // Endpoint 0 is pure red, endpoint 1 pure blue; first row picks 0, 1, 2, 3.
    std::vector<std::byte> data;
    colour_block(data, 0xF800, 0x001F, 0b11100100, 0b00000000);
    const auto image = decode_pixels(18, 4, 4, data);
    REQUIRE(image.has_value());
    CHECK(at(*image, 0, 0) == Px{255, 0, 0, 255});
    CHECK(at(*image, 1, 0) == Px{0, 0, 255, 255});
    CHECK(at(*image, 2, 0) == Px{170, 0, 85, 255});
    CHECK(at(*image, 3, 0) == Px{85, 0, 170, 255});
    CHECK(at(*image, 0, 1) == Px{255, 0, 0, 255});
  }

  SUBCASE("three colour mode") {
    std::vector<std::byte> data;
    colour_block(data, 0x001F, 0xF800, 0b11100100);
    const auto image = decode_pixels(18, 4, 4, data);
    REQUIRE(image.has_value());
    CHECK(at(*image, 0, 0) == Px{0, 0, 255, 255});
    CHECK(at(*image, 1, 0) == Px{255, 0, 0, 255});
    CHECK(at(*image, 2, 0) == Px{127, 0, 127, 255});
    CHECK(at(*image, 3, 0) == Px{0, 0, 0, 0});
  }

  SUBCASE("blocks are laid out left to right, then down, and clipped to the image") {
    std::vector<std::byte> data;
    colour_block(data, 0xF800, 0x0000, 0);  // block 0: red
    colour_block(data, 0x07E0, 0x0000, 0);  // block 1: green
    colour_block(data, 0x001F, 0x0000, 0);  // block 2: blue
    colour_block(data, 0xFFFF, 0x0000, 0);  // block 3: white
    const auto image = decode_pixels(18, 8, 8, data);
    REQUIRE(image.has_value());
    CHECK(at(*image, 0, 0) == Px{255, 0, 0, 255});
    CHECK(at(*image, 4, 3) == Px{0, 255, 0, 255});
    CHECK(at(*image, 0, 4) == Px{0, 0, 255, 255});
    CHECK(at(*image, 7, 7) == Px{255, 255, 255, 255});

    // A 2 x 2 image still takes one block and uses its top left corner.
    std::vector<std::byte> one;
    colour_block(one, 0x07E0, 0x0000, 0);
    const auto small = decode_pixels(18, 2, 2, one);
    REQUIRE(small.has_value());
    CHECK(small->pixels.size() == 16);
    CHECK(at(*small, 1, 1) == Px{0, 255, 0, 255});
  }
}

TEST_CASE("format 19 blocks carry explicit four bit alpha") {
  std::vector<std::byte> data;
  // Alpha: pixel 0 nibble 0, pixel 1 nibble 15, pixel 2 nibble 4, pixel 3 nibble 8, rest 0xA.
  push(data, {0xF0, 0x84, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA, 0xAA});
  colour_block(data, 0xF800, 0x001F, 0);  // red everywhere, and endpoint 0 > endpoint 1
  const auto image = decode_pixels(19, 4, 4, data);
  REQUIRE(image.has_value());
  CHECK(at(*image, 0, 0) == Px{255, 0, 0, 0});
  CHECK(at(*image, 1, 0) == Px{255, 0, 0, 255});
  CHECK(at(*image, 2, 0) == Px{255, 0, 0, 68});
  CHECK(at(*image, 3, 0) == Px{255, 0, 0, 136});
  CHECK(at(*image, 0, 1).a == 170);

  SUBCASE("the colour part never uses the transparent entry") {
    std::vector<std::byte> block;
    push(block, {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF});
    colour_block(block, 0x001F, 0xF800, 0b11100100);  // endpoint 0 < endpoint 1
    const auto result = decode_pixels(19, 4, 4, block);
    REQUIRE(result.has_value());
    // Four colour interpolation: one third blue plus two thirds red.
    CHECK(at(*result, 3, 0) == Px{170, 0, 85, 255});
  }

  SUBCASE("a truncated block is refused") {
    CHECK_FALSE(decode_pixels(19, 4, 4, std::span<const std::byte>(data).first(15)).has_value());
  }
}

TEST_CASE("bitmap mip levels follow each other, largest first") {
  // An 8 x 8 raw bitmap with four levels, each filled with its own byte value.
  std::vector<std::byte> data;
  for (unsigned level = 0; level < 4; ++level) {
    const std::size_t side = 8u >> level;
    for (std::size_t i = 0; i < side * side * 4; ++i) data.push_back(static_cast<std::byte>(10 + level));
  }
  Bitmap bitmap;
  bitmap.format = 2;
  bitmap.width = 8;
  bitmap.height = 8;
  bitmap.levels = 4;
  bitmap.data = data;

  const auto levels = decode_bitmap(bitmap);
  REQUIRE(levels.size() == 4);
  CHECK(levels[0].width == 8);
  CHECK(levels[1].width == 4);
  CHECK(levels[2].width == 2);
  CHECK(levels[3].width == 1);
  CHECK(levels[3].height == 1);
  for (unsigned level = 0; level < 4; ++level) CHECK(at(levels[level], 0, 0).r == 10 + level);

  SUBCASE("short data fails the whole chain") {
    bitmap.data = std::span<const std::byte>(data).first(data.size() - 1);
    CHECK(decode_bitmap(bitmap).empty());
  }

  SUBCASE("block formats round small levels up to a block") {
    std::vector<std::byte> blocks;
    // 8 x 8 with three levels: 4 blocks, then 1, then 1 for the 2 x 2 level.
    for (unsigned i = 0; i < 6; ++i) colour_block(blocks, i % 2 ? 0x07E0 : 0xF800, 0, 0);
    Bitmap b;
    b.format = 18;
    b.width = 8;
    b.height = 8;
    b.levels = 3;
    b.data = blocks;
    const auto result = decode_bitmap(b);
    REQUIRE(result.size() == 3);
    CHECK(result[2].width == 2);
    CHECK(at(result[2], 0, 0) == Px{0, 255, 0, 255});  // sixth block
  }

  SUBCASE("a zero level count or unknown format gives nothing") {
    bitmap.levels = 0;
    CHECK(decode_bitmap(bitmap).empty());
    bitmap.levels = 4;
    bitmap.format = 7;
    CHECK(decode_bitmap(bitmap).empty());
  }
}

TEST_CASE("flic images store later levels behind a small size record") {
  SUBCASE("raw levels go down to one pixel") {
    std::vector<std::byte> chunk;
    for (std::uint32_t v : {2u, 4u, 4u, 3u, 4u, 4u, 16u, 4u}) word(chunk, v);
    for (int i = 0; i < 16 * 4; ++i) chunk.push_back(std::byte{0x21});  // 4 x 4
    for (std::uint32_t v : {2u, 2u, 8u, 2u}) word(chunk, v);
    for (int i = 0; i < 2 * 2 * 4; ++i) chunk.push_back(std::byte{0x32});
    for (std::uint32_t v : {1u, 1u, 4u, 1u}) word(chunk, v);
    for (int i = 0; i < 4; ++i) chunk.push_back(std::byte{0x43});
    chunk.resize(chunk.size() + 16, std::byte{0});  // padding

    Flic flic;
    flic.revision = 2;
    flic.data = chunk;
    flic.image = FlicImageHeader{2, 4, 4, 3, 16, 4};
    const auto levels = decode_flic_image(flic);
    REQUIRE(levels.size() == 3);
    CHECK(levels[0].width == 4);
    CHECK(at(levels[0], 3, 3).r == 0x21);
    CHECK(levels[1].width == 2);
    CHECK(at(levels[1], 1, 1).r == 0x32);
    CHECK(levels[2].width == 1);
    CHECK(at(levels[2], 0, 0).r == 0x43);
  }

  SUBCASE("block levels stop at 4 x 4 and the declared count may be larger") {
    std::vector<std::byte> chunk;
    for (std::uint32_t v : {18u, 8u, 8u, 4u, 8u, 8u, 16u, 2u}) word(chunk, v);
    for (int i = 0; i < 4; ++i) colour_block(chunk, 0xF800, 0, 0);  // 8 x 8, four blocks
    for (std::uint32_t v : {4u, 4u, 8u, 1u}) word(chunk, v);
    colour_block(chunk, 0x07E0, 0, 0);  // 4 x 4, one block
    chunk.resize(chunk.size() + 16, std::byte{0});

    Flic flic;
    flic.revision = 2;
    flic.data = chunk;
    flic.image = FlicImageHeader{18, 8, 8, 4, 16, 2};
    const auto levels = decode_flic_image(flic);
    REQUIRE(levels.size() == 2);
    CHECK(at(levels[0], 7, 7) == Px{255, 0, 0, 255});
    CHECK(at(levels[1], 0, 0) == Px{0, 255, 0, 255});
  }

  SUBCASE("a flic without an image, or with short data, gives nothing") {
    Flic flic;
    flic.revision = 3;
    CHECK(decode_flic_image(flic).empty());

    std::vector<std::byte> chunk(40, std::byte{0});
    flic.revision = 2;
    flic.data = chunk;
    flic.image = FlicImageHeader{2, 4, 4, 1, 16, 4};  // needs 32 + 64 bytes
    CHECK(decode_flic_image(flic).empty());
  }
}

TEST_CASE("TGA output is an uncompressed 32-bit image with the top row first") {
  RgbaImage image;
  image.width = 2;
  image.height = 1;
  image.pixels = {1, 2, 3, 4, 5, 6, 7, 8};

  const auto tga = encode_tga(image);
  REQUIRE(tga.size() == 18 + 8);
  CHECK(tga[0] == std::byte{0});    // no image id
  CHECK(tga[1] == std::byte{0});    // no colour map
  CHECK(tga[2] == std::byte{2});    // uncompressed true colour
  CHECK(tga[12] == std::byte{2});   // width, low byte
  CHECK(tga[14] == std::byte{1});   // height, low byte
  CHECK(tga[16] == std::byte{32});  // bits per pixel
  CHECK((static_cast<unsigned>(tga[17]) & 0x20) != 0);  // top-left origin
  CHECK(tga[18] == std::byte{3});   // blue first
  CHECK(tga[19] == std::byte{2});
  CHECK(tga[20] == std::byte{1});
  CHECK(tga[21] == std::byte{4});   // alpha last

  image.pixels.pop_back();
  CHECK(encode_tga(image).empty());
  CHECK(encode_tga(RgbaImage{}).empty());
}
