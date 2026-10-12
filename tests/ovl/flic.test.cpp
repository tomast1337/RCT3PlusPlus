// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/flic.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_flics;

namespace {

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

void text(std::vector<std::byte>& out, const std::string& s) {
  append_u16(out, static_cast<std::uint16_t>(s.size()));
  for (char c : s) out.push_back(static_cast<std::byte>(c));
}

// A v1 file with one loader declared in its header, whose rows `rows` are in the loader table, an item
// block at address 0 and `chunks` after the relocation table.
std::vector<std::byte> make_file(std::uint32_t revision, const std::vector<std::byte>& item,
                                 const std::vector<std::byte>& rows, const std::vector<std::byte>& chunks,
                                 const std::string& tag = "flic") {
  auto file = build_file(1, {{item}, {}, {{}, rows}}, {}, chunks);
  // Header: magic, flags and padding, version, two empty reference lists, then the loader count.
  constexpr std::size_t kLoaderCount = 20;
  file[kLoaderCount] = std::byte{1};
  std::vector<std::byte> declaration;
  text(declaration, "FGDK");
  text(declaration, "Flic");
  append_u32(declaration, revision);
  text(declaration, tag);
  file.insert(file.begin() + kLoaderCount + 4, declaration.begin(), declaration.end());
  return file;
}

std::vector<std::byte> row(std::uint32_t data, std::uint32_t chunk_count = 1) {
  std::vector<std::byte> out;
  words(out, {0, data, chunk_count, 0, 0});
  return out;
}

std::vector<std::byte> chunk(const std::vector<std::byte>& payload) {
  std::vector<std::byte> out;
  append_u32(out, static_cast<std::uint32_t>(payload.size()));
  out.insert(out.end(), payload.begin(), payload.end());
  return out;
}

std::vector<std::byte> item(std::uint32_t frames, float rate) {
  std::vector<std::byte> out;
  words(out, {0, frames, std::bit_cast<std::uint32_t>(rate)});
  return out;
}

Overlay load(const std::vector<std::byte>& file) {
  auto overlay = Overlay::from_bytes(file, std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

// A 16 x 8 block compressed image with 16-byte blocks, one level: header and 4 x 2 blocks.
std::vector<std::byte> image_chunk(std::uint32_t format = 19, std::uint32_t row_bytes = 64) {
  std::vector<std::byte> out;
  words(out, {format, 16, 8, 1, 16, 8, row_bytes, 2});
  words(out, {0, 0, 0, 0});  // four words of an unknown purpose
  out.resize(out.size() + 8 * 16, std::byte{0x11});
  return out;
}

}  // namespace

TEST_CASE("revision 3 flics expose their frame numbers") {
  std::vector<std::byte> payload;
  words(payload, {7, 8, 9});
  const auto overlay = load(make_file(3, item(3, 30.0f), row(0), chunk(payload)));
  const auto items = read_flics(overlay, overlay.common());

  REQUIRE(items.size() == 1);
  CHECK(items[0].address == 0);
  REQUIRE(items[0].flic.has_value());
  const auto& f = *items[0].flic;
  CHECK(f.revision == 3);
  CHECK(f.frame_count == 3);
  CHECK(f.rate == 30.0f);
  CHECK(f.data.size() == 12);
  REQUIRE(f.frames.size() == 3);
  CHECK(f.frames[0] == 7);
  CHECK(f.frames[2] == 9);
  CHECK_FALSE(f.image.has_value());
}

TEST_CASE("revision 2 flics expose the image header and the chunk") {
  const auto payload = image_chunk();
  const auto overlay = load(make_file(2, item(1, 1.0f), row(0), chunk(payload)));
  const auto items = read_flics(overlay, overlay.common());

  REQUIRE(items.size() == 1);
  REQUIRE(items[0].flic.has_value());
  const auto& f = *items[0].flic;
  CHECK(f.revision == 2);
  CHECK(f.frame_count == 1);
  CHECK(f.data.size() == payload.size());
  CHECK(f.frames.empty());
  REQUIRE(f.image.has_value());
  CHECK(f.image->format == 19);
  CHECK(f.image->width == 16);
  CHECK(f.image->height == 8);
  CHECK(f.image->level_count == 1);
  CHECK(f.image->row_bytes == 64);
  CHECK(f.image->row_count == 2);
}

TEST_CASE("flic edge cases") {
  SUBCASE("an image header whose row size does not follow from the format is not used") {
    const auto overlay = load(make_file(2, item(1, 1.0f), row(0), chunk(image_chunk(19, 60))));
    const auto items = read_flics(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].flic.has_value());
    CHECK_FALSE(items[0].flic->image.has_value());
  }

  SUBCASE("a frame list whose size does not match the count gives no frames") {
    std::vector<std::byte> payload;
    words(payload, {1, 2});
    const auto overlay = load(make_file(3, item(3, 1.0f), row(0), chunk(payload)));
    const auto items = read_flics(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].flic.has_value());
    CHECK(items[0].flic->frames.empty());
    CHECK(items[0].flic->data.size() == 8);
  }

  SUBCASE("a row without a chunk is unreadable") {
    const auto overlay = load(make_file(3, item(1, 1.0f), row(0, 0), {}));
    const auto items = read_flics(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].flic.has_value());
  }

  SUBCASE("an item that leaves its block is unreadable") {
    std::vector<std::byte> payload;
    words(payload, {0});
    const auto overlay = load(make_file(3, item(1, 1.0f), row(8), chunk(payload)));
    const auto items = read_flics(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].flic.has_value());
  }

  SUBCASE("rows of other handlers are ignored") {
    std::vector<std::byte> payload;
    words(payload, {0});
    const auto overlay = load(make_file(3, item(1, 1.0f), row(0), chunk(payload), "ftx"));
    CHECK(read_flics(overlay, overlay.common()).empty());
  }
}
