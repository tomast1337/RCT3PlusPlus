// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/research_category.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_research_categories;

namespace {

constexpr char kNameData[] = "Alpha:rcg\0Beta:rcg\0Gamma:rcg\0AlphaText:txt\0Beta\0Label:txt";
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

std::vector<std::byte> symbol(const std::string& full, std::uint32_t data) {
  std::vector<std::byte> out;
  append_u32(out, name_at(full));
  append_u32(out, kBase + data);
  append_u16(out, 1);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::vector<std::byte> reference(std::uint32_t location, const std::string& name) {
  std::vector<std::byte> out;
  for (auto v : {location, name_at(name), std::uint32_t{0}, std::uint32_t{0}}) append_u32(out, v);
  return out;
}

// Three 40-byte categories and two slots after them.
constexpr std::uint32_t kSlots = 120;

std::vector<std::byte> block() {
  std::vector<std::uint32_t> w(kSlots / 4 + 2, 0);
  // Alpha: kind 1, name pointer, two weights, no prerequisites.
  w[0] = 1;
  w[1] = name_at("Beta");
  w[3] = std::bit_cast<std::uint32_t>(25.0f);
  w[4] = std::bit_cast<std::uint32_t>(10.0f);
  // Beta: needs Alpha and one missing reference.
  w[10 + 0] = 0;
  w[10 + 1] = name_at("Beta");
  w[10 + 5] = 2;
  w[10 + 6] = kBase + kSlots;
  // Gamma: kind 2.
  w[20] = 2;
  w[20 + 1] = name_at("Beta");
  w[20 + 4] = std::bit_cast<std::uint32_t>(1.0f);
  std::vector<std::byte> out;
  for (auto v : w) append_u32(out, v);
  return out;
}

Overlay make(const std::vector<std::byte>& data, const std::vector<std::byte>& refs) {
  std::vector<std::byte> symbols = symbol("Alpha:rcg", 0);
  for (const auto& row : {symbol("Beta:rcg", 40), symbol("Gamma:rcg", 80)}) symbols.insert(symbols.end(), row.begin(), row.end());
  auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {data}, {symbols, {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

std::vector<std::byte> references() {
  std::vector<std::byte> out = reference(kBase + 8, "AlphaText:txt");
  const auto slot = reference(kBase + kSlots, "Alpha:rcg");
  out.insert(out.end(), slot.begin(), slot.end());
  return out;
}

}  // namespace

TEST_CASE("research categories expose kind, texts and prerequisites") {
  const auto overlay = make(block(), references());
  const auto categories = read_research_categories(overlay, overlay.common());
  REQUIRE(categories.size() == 3);

  CHECK(categories[0].name == "Alpha");
  REQUIRE(categories[0].category.has_value());
  CHECK(categories[0].category->kind == 1);
  CHECK(categories[0].category->name_text == "AlphaText");
  CHECK(categories[0].category->unknown_0c == 25.0f);
  CHECK(categories[0].category->unknown_10 == 10.0f);
  CHECK(categories[0].category->prerequisites.empty());

  REQUIRE(categories[1].category.has_value());
  CHECK(categories[1].category->prerequisites.size() == 2);
  CHECK(categories[1].category->prerequisites[0] == "Alpha");
  CHECK(categories[1].category->prerequisites[1].empty());  // no reference for the second slot

  REQUIRE(categories[2].category.has_value());
  CHECK(categories[2].category->kind == 2);
  CHECK(categories[2].category->unknown_10 == 1.0f);
}

TEST_CASE("research category edge cases") {
  SUBCASE("a prerequisite array outside the block makes the category unreadable") {
    auto data = block();
    data[10 * 4 + 24] = std::byte{0xF0};  // slot pointer of Beta moved far away
    data[10 * 4 + 25] = std::byte{0xF0};
    const auto overlay = make(data, references());
    const auto categories = read_research_categories(overlay, overlay.common());
    REQUIRE(categories.size() == 3);
    CHECK_FALSE(categories[1].category.has_value());
    CHECK(categories[0].category.has_value());
  }

  SUBCASE("a huge prerequisite count is refused without allocating") {
    auto data = block();
    for (std::size_t i = 0; i < 4; ++i) data[10 * 4 + 20 + i] = std::byte{0xFF};
    const auto overlay = make(data, references());
    const auto categories = read_research_categories(overlay, overlay.common());
    CHECK_FALSE(categories[1].category.has_value());
  }

  SUBCASE("the last category cut short by its block is unreadable") {
    auto data = block();
    data.resize(90);
    const auto overlay = make(data, references());
    const auto categories = read_research_categories(overlay, overlay.common());
    REQUIRE(categories.size() == 3);
    CHECK_FALSE(categories[2].category.has_value());
  }

  SUBCASE("a missing reference table leaves texts and prerequisites empty") {
    auto overlay = Overlay::from_bytes(
        build_file(5, {{names()}, {block()}, {symbol("Beta:rcg", 40)}}, {}), std::nullopt);
    REQUIRE(overlay.has_value());
    const auto categories = read_research_categories(*overlay, overlay->common());
    REQUIRE(categories[0].category.has_value());
    CHECK(categories[0].category->prerequisites.size() == 2);
    CHECK(categories[0].category->prerequisites[0].empty());
  }
}
