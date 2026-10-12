// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/bone_anim.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_bone_anims;

namespace {

// Name block: "A:ban" 0, "arm" 6, "leg" 10, padded to 16. The structure block follows at address 16.
constexpr std::uint32_t kBase = 16;
constexpr std::uint32_t kNameArm = 6;
constexpr std::uint32_t kNameLeg = 10;

// Offsets inside the structure block.
constexpr std::uint32_t kHeader = 0;      // 12 bytes
constexpr std::uint32_t kBones = 12;      // 2 * 20
constexpr std::uint32_t kPositions = 52;  // 2 keyframes * 16
constexpr std::uint32_t kRotations = 84;  // 3 keyframes * 16
constexpr std::uint32_t kEnd = 132;

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

void real(std::vector<std::byte>& out, float v) { append_u32(out, std::bit_cast<std::uint32_t>(v)); }

void key(std::vector<std::byte>& out, float time, float x, float y, float z) {
  for (float f : {time, x, y, z}) real(out, f);
}

std::vector<std::byte> names() {
  std::string text("A:ban\0arm\0leg\0", 14);
  text.resize(kBase, '\0');
  return bytes_of_string(text);
}

std::vector<std::byte> symbol_row() {
  std::vector<std::byte> out;
  words(out, {0, kBase + kHeader});
  append_u16(out, 1);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::vector<std::byte> structure(std::uint32_t bone_count = 2, std::uint32_t rotation_count = 3) {
  std::vector<std::byte> out;
  words(out, {bone_count, kBase + kBones});
  real(out, 2.0f);
  words(out, {kNameArm, 2, kBase + kPositions, rotation_count, kBase + kRotations});  // bone 0
  words(out, {kNameLeg, 0, 0, 0, 0});                                                 // bone 1: still
  key(out, 0.0f, 1.0f, 2.0f, 3.0f);
  key(out, 2.0f, 4.0f, 5.0f, 6.0f);
  key(out, 0.0f, 0.1f, 0.2f, 0.3f);
  key(out, 1.0f, 0.4f, 0.5f, 0.6f);
  key(out, 2.0f, 0.7f, 0.8f, 0.9f);
  REQUIRE(out.size() == kEnd);
  return out;
}

Overlay make(const std::vector<std::byte>& block) {
  auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {block}, {symbol_row()}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("bone animations expose bones and keyframes") {
  const auto overlay = make(structure());
  const auto items = read_bone_anims(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK(items[0].name == "A");
  REQUIRE(items[0].anim.has_value());

  const auto& a = *items[0].anim;
  CHECK(a.duration == 2.0f);
  REQUIRE(a.bones.size() == 2);

  const auto& arm = a.bones[0];
  CHECK(arm.name == "arm");
  REQUIRE(arm.positions.size() == 2);
  CHECK(arm.positions[1].time == 2.0f);
  CHECK(arm.positions[1].value[2] == 6.0f);
  REQUIRE(arm.rotations.size() == 3);
  CHECK(arm.rotations[1].time == 1.0f);
  CHECK(arm.rotations[2].value[0] == 0.7f);

  const auto& leg = a.bones[1];
  CHECK(leg.name == "leg");
  CHECK(leg.positions.empty());
  CHECK(leg.rotations.empty());
}

TEST_CASE("bone animation edge cases") {
  SUBCASE("a bone count that cannot fit is refused") {
    const auto overlay = make(structure(0x7FFFFFFF));
    const auto items = read_bone_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("a keyframe run that leaves its block makes the animation unreadable") {
    const auto overlay = make(structure(2, 4));  // one keyframe more than the block holds
    const auto items = read_bone_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("a keyframe count that overflows is refused without allocating") {
    const auto overlay = make(structure(2, 0xFFFFFFFF));
    const auto items = read_bone_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].anim.has_value());
  }

  SUBCASE("an animation without bones reads") {
    const auto overlay = make(structure(0));
    const auto items = read_bone_anims(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].anim.has_value());
    CHECK(items[0].anim->bones.empty());
    CHECK(items[0].anim->duration == 2.0f);
  }
}
