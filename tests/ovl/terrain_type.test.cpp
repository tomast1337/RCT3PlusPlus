// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/terrain_type.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_terrain_types;

namespace {

constexpr char kNameData[] = "Ground:ter\0Rock:ter\0Ground:tex\0Rock:tex\0GroundHelp\0GroundLabel\0RockHelp\0RockLabel";
const std::string kNames(kNameData, sizeof kNameData);
const std::uint32_t kBase = (static_cast<std::uint32_t>(sizeof kNameData) + 3) & ~3u;

std::uint32_t name_at(const std::string& needle) {
  const auto at = kNames.find(needle);
  REQUIRE(at != std::string::npos);
  return static_cast<std::uint32_t>(at);
}

std::vector<std::byte> names() {
  std::string text = kNames;
  text.resize(kBase, '\0');
  return bytes_of_string(text);
}

std::vector<std::byte> symbols() {
  std::vector<std::byte> out;
  for (const auto& [name, data] : {std::pair<std::string, std::uint32_t>{"Ground:ter", 0}, {"Rock:ter", 60}}) {
    append_u32(out, name_at(name));
    append_u32(out, kBase + data);
    append_u16(out, 1);
    append_u16(out, 0xFFFF);
    append_u32(out, 0);
  }
  return out;
}

std::vector<std::byte> references(bool second_texture = true) {
  std::vector<std::byte> out;
  for (auto v : {kBase + 0x14, name_at("Ground:tex"), 0u, 0u}) append_u32(out, v);
  if (second_texture) {
    for (auto v : {kBase + 60 + 0x14, name_at("Rock:tex"), 0u, 0u}) append_u32(out, v);
  }
  return out;
}

std::vector<std::byte> block() {
  std::vector<std::uint32_t> w(30, 0);
  w[0] = 1;
  w[3] = 4;
  w[4] = 2;
  w[6] = name_at("GroundHelp");
  w[7] = name_at("GroundLabel");
  w[8] = 0xFF112233u;
  w[9] = 0xFF112244u;
  w[10] = std::bit_cast<std::uint32_t>(0.1f);
  w[12] = std::bit_cast<std::uint32_t>(0.3f);
  w[14] = std::bit_cast<std::uint32_t>(-0.5f);
  w[15 + 0] = 1;
  w[15 + 2] = 1;
  w[15 + 3] = 5;
  w[15 + 6] = name_at("RockHelp");
  w[15 + 7] = name_at("RockLabel");
  std::vector<std::byte> out;
  for (auto v : w) append_u32(out, v);
  return out;
}

Overlay make(const std::vector<std::byte>& data, const std::vector<std::byte>& refs = references()) {
  auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {data}, {symbols(), {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("terrain types expose texture, names, colours and parameters") {
  const auto overlay = make(block());
  const auto types = read_terrain_types(overlay, overlay.common());
  REQUIRE(types.size() == 2);

  CHECK(types[0].name == "Ground");
  REQUIRE(types[0].terrain.has_value());
  const auto& t = *types[0].terrain;
  CHECK(t.unknown_00 == 1);
  CHECK(t.index == 4);
  CHECK(t.unknown_10 == 2);
  CHECK(t.texture == "Ground");
  CHECK(t.help_name == "GroundHelp");
  CHECK(t.label_name == "GroundLabel");
  CHECK(t.colours[0] == 0xFF112233u);
  CHECK(t.colours[1] == 0xFF112244u);
  CHECK(t.parameters[0] == 0.1f);
  CHECK(t.parameters[2] == 0.3f);
  CHECK(t.parameters[4] == -0.5f);

  REQUIRE(types[1].terrain.has_value());
  CHECK(types[1].terrain->unknown_08 == 1);
  CHECK(types[1].terrain->index == 5);
  CHECK(types[1].terrain->texture == "Rock");
  CHECK(types[1].terrain->label_name == "RockLabel");
}

TEST_CASE("terrain type edge cases") {
  SUBCASE("a type cut short by its block is unreadable, others are kept") {
    auto data = block();
    data.resize(100);
    const auto overlay = make(data);
    const auto types = read_terrain_types(overlay, overlay.common());
    REQUIRE(types.size() == 2);
    CHECK(types[0].terrain.has_value());
    CHECK_FALSE(types[1].terrain.has_value());
  }

  SUBCASE("a missing texture reference leaves the name empty") {
    const auto overlay = make(block(), references(false));
    const auto types = read_terrain_types(overlay, overlay.common());
    REQUIRE(types[1].terrain.has_value());
    CHECK(types[1].terrain->texture.empty());
    CHECK(types[0].terrain->texture == "Ground");
  }

  SUBCASE("a string pointer outside every block gives an empty name") {
    auto data = block();
    data[0x18] = std::byte{0xFF};
    data[0x19] = std::byte{0xFF};
    const auto overlay = make(data);
    const auto types = read_terrain_types(overlay, overlay.common());
    REQUIRE(types[0].terrain.has_value());
    CHECK(types[0].terrain->help_name.empty());
  }
}
