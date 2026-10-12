// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/model_anim.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_model_anims;

namespace {

// Name block: "A:modelanim" at 0, padded to 16. The item block follows at address 16.
constexpr std::uint32_t kBase = 16;
constexpr std::uint32_t kPositions = 48;  // frames * 1 bone * 12
constexpr std::uint32_t kFrames = 2;

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

void real(std::vector<std::byte>& out, float v) { append_u32(out, std::bit_cast<std::uint32_t>(v)); }

void text(std::vector<std::byte>& out, const std::string& s) {
  append_u16(out, static_cast<std::uint16_t>(s.size()));
  for (char c : s) out.push_back(static_cast<std::byte>(c));
}

std::vector<std::byte> names() {
  std::string t("A:modelanim\0", 12);
  t.resize(kBase, '\0');
  return bytes_of_string(t);
}

// Header (48 bytes), 2 positions (24 bytes), then 2 frames * 2 bones * 16 bytes of rotations.
std::vector<std::byte> item(std::uint32_t position_bones = 1, std::uint32_t plain = 0, std::uint32_t frames = kFrames) {
  std::vector<std::byte> out;
  real(out, 0.5f);
  words(out, {frames, 0, 0, 0, 0, position_bones, 2, plain, 0, kBase + kPositions, kBase + kPositions + 24});
  for (float v : {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f}) real(out, v);
  for (std::uint32_t i = 0; i < 4; ++i) {
    for (float v : {0.0f, 0.0f, 0.0f, 1.0f}) real(out, v + float(i) * 10);
  }
  return out;
}

std::vector<std::byte> chunk_of(const std::string& names_text) {
  std::vector<std::byte> out;
  append_u32(out, static_cast<std::uint32_t>(names_text.size()));
  for (char c : names_text) out.push_back(static_cast<std::byte>(c));
  return out;
}

std::vector<std::byte> symbol_row() {
  std::vector<std::byte> out;
  words(out, {0, kBase, 1});
  return out;
}

std::vector<std::byte> loader_row() {
  std::vector<std::byte> out;
  words(out, {0, kBase, 1, 0, 0});
  return out;
}

std::vector<std::byte> make_file(const std::vector<std::byte>& item_bytes, const std::vector<std::byte>& chunk) {
  auto file = build_file(1, {{names()}, {item_bytes}, {symbol_row(), loader_row()}}, {}, chunk);
  constexpr std::size_t kLoaderCount = 20;
  file[kLoaderCount] = std::byte{1};
  std::vector<std::byte> declaration;
  text(declaration, "FGDK");
  text(declaration, "ModelAnim");
  append_u32(declaration, 2);
  text(declaration, "modelanim");
  file.insert(file.begin() + kLoaderCount + 4, declaration.begin(), declaration.end());
  return file;
}

Overlay load(const std::vector<std::byte>& file) {
  auto overlay = Overlay::from_bytes(file, std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

const std::string kNames("hip\0hip\0leg\0", 12);

}  // namespace

TEST_CASE("model animations expose bone names and samples") {
  const auto overlay = load(make_file(item(), chunk_of(kNames)));
  const auto items = read_model_anims(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK(items[0].name == "A");
  REQUIRE(items[0].anim.has_value());

  const auto& a = *items[0].anim;
  CHECK(a.duration == 0.5f);
  CHECK(a.frame_count == 2);
  REQUIRE(a.position_bones.size() == 1);
  CHECK(a.position_bones[0] == "hip");
  REQUIRE(a.rotation_bones.size() == 2);
  CHECK(a.rotation_bones[0] == "hip");
  CHECK(a.rotation_bones[1] == "leg");
  REQUIRE(a.positions.size() == 2);
  CHECK(a.positions[1][2] == 6.0f);
  REQUIRE(a.rotations.size() == 4);
  CHECK(a.rotations[3][0] == 30.0f);
  CHECK(a.rotations[3][3] == 31.0f);
}

TEST_CASE("model animation edge cases") {
  SUBCASE("fewer names than the lists need makes the item unreadable") {
    const auto overlay = load(make_file(item(), chunk_of(std::string("hip\0leg\0", 8))));
    const auto items = read_model_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("a frame count that overflows the block is refused") {
    const auto overlay = load(make_file(item(1, 0, 0x7FFFFFFF), chunk_of(kNames)));
    const auto items = read_model_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("samples that leave the block make the item unreadable") {
    const auto overlay = load(make_file(item(1, 0, 3), chunk_of(kNames)));
    const auto items = read_model_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("a non-empty plain list is not supported") {
    const auto overlay = load(make_file(item(1, 1), chunk_of(kNames)));
    const auto items = read_model_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }
}
