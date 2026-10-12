// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <bit>
#include <string>
#include <vector>

#include "builder.hpp"
#include "ovl/static_shape.hpp"

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_static_shapes;

namespace {

// Name block, 28 bytes: "T:shs" 0, "Tex:ftx" 6, "Sty:txs" 14, "hook" 22. The structure block follows
// at address 28.
constexpr std::uint32_t kBase = 28;
constexpr std::uint32_t kNameShape = 0;
constexpr std::uint32_t kNameTexture = 6;
constexpr std::uint32_t kNameStyle = 14;
constexpr std::uint32_t kNameHook = 22;

// Offsets inside the structure block.
constexpr std::uint32_t kMeshArray = 0;     // 2 pointers
constexpr std::uint32_t kMesh0 = 8;         // 40 bytes
constexpr std::uint32_t kMesh1 = 48;        // 40 bytes
constexpr std::uint32_t kHeader = 88;       // 56 bytes
constexpr std::uint32_t kEffectNames = 144; // 1 pointer
constexpr std::uint32_t kMatrix = 148;      // 64 bytes
constexpr std::uint32_t kVertices0 = 212;   // 3 * 36
constexpr std::uint32_t kIndices0 = 320;    // 3 * 4
constexpr std::uint32_t kVertices1 = 332;   // 3 * 36
constexpr std::uint32_t kIndices1 = 440;    // 6 * 4
constexpr std::uint32_t kEnd = 464;

void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values) {
  for (auto v : values) append_u32(out, v);
}

void real(std::vector<std::byte>& out, float v) { append_u32(out, std::bit_cast<std::uint32_t>(v)); }

std::vector<std::byte> names() {
  std::string text("T:shs\0Tex:ftx\0Sty:txs\0hook\0", 27);
  text.push_back('\0');
  return bytes_of_string(text);
}

std::vector<std::byte> symbol_row() {
  std::vector<std::byte> out;
  words(out, {kNameShape, kBase + kHeader});
  append_u16(out, 1);
  append_u16(out, 0xFFFF);
  append_u32(out, 0);
  return out;
}

std::vector<std::byte> reference_rows(std::uint32_t second_name = kNameStyle) {
  std::vector<std::byte> out;
  words(out, {kBase + kMesh0 + 4, kNameTexture, 0, 0});
  words(out, {kBase + kMesh0 + 8, second_name, 0, 0});
  return out;
}

std::vector<std::byte> structure(std::uint32_t mesh_count = 2, std::uint32_t indices1 = kBase + kIndices1) {
  std::vector<std::byte> out;
  words(out, {kBase + kMesh0, kBase + kMesh1});                                   // mesh array
  words(out, {0xFFFFFFFF, 0, 0, 1, 12, 3, 3, 3, kBase + kVertices0, kBase + kIndices0});  // mesh 0
  words(out, {4, 0, 0, 0, 0, 1, 3, 6, kBase + kVertices1, indices1});             // mesh 1
  for (float f : {-1.0f, -2.0f, -3.0f, 1.0f, 2.0f, 3.0f}) real(out, f);           // bounds
  words(out, {6, 9, 1, mesh_count, kBase + kMeshArray, 1, kBase + kMatrix, kBase + kEffectNames});
  words(out, {kNameHook});                                                        // effect name pointer
  for (std::size_t i = 0; i < 16; ++i) real(out, i % 5 == 0 ? 1.0f : 0.0f);       // identity matrix
  const auto vertices = [&out](std::size_t first) {
    for (std::size_t v = first; v < first + 3; ++v) {
      for (std::size_t k = 0; k < 6; ++k) real(out, static_cast<float>(v * 10 + k));
      words(out, {0xFF030201u});  // colour bytes 01 02 03 FF
      real(out, static_cast<float>(v * 10 + 7));
      real(out, static_cast<float>(v * 10 + 8));
    }
  };
  vertices(0);
  words(out, {0, 1, 2});
  vertices(3);
  words(out, {2, 1, 0, 0, 1, 2});
  REQUIRE(out.size() == kEnd);
  return out;
}

Overlay make(const std::vector<std::byte>& block, const std::vector<std::byte>& refs = reference_rows()) {
  auto overlay = Overlay::from_bytes(
      build_file(5, {{names()}, {block}, {symbol_row(), {}, refs}}, {}), std::nullopt);
  REQUIRE(overlay.has_value());
  return std::move(*overlay);
}

}  // namespace

TEST_CASE("static shapes expose meshes, references and effects") {
  const auto overlay = make(structure());
  const auto items = read_static_shapes(overlay, overlay.common());
  REQUIRE(items.size() == 1);
  CHECK(items[0].name == "T");
  REQUIRE(items[0].shape.has_value());

  const auto& s = *items[0].shape;
  CHECK(s.bounds_min[1] == -2.0f);
  CHECK(s.bounds_max[2] == 3.0f);
  CHECK(s.vertex_total == 6);
  CHECK(s.index_total == 9);
  CHECK(s.secondary_count == 1);

  REQUIRE(s.meshes.size() == 2);
  const auto& m0 = s.meshes[0];
  CHECK(m0.support_type == 0xFFFFFFFF);
  CHECK(m0.texture == "Tex");
  CHECK(m0.style == "Sty");
  CHECK(m0.transparency == 1);
  CHECK(m0.texture_flags == 12);
  CHECK(m0.sides == 3);
  CHECK(m0.vertex_count == 3);
  REQUIRE(m0.vertices.size() == 108);
  REQUIRE(m0.indices.size() == 12);
  const auto v = m0.vertex(1);
  CHECK(v.position[0] == 10.0f);
  CHECK(v.normal[2] == 15.0f);
  CHECK(v.colour == std::array<std::uint8_t, 4>{0x01, 0x02, 0x03, 0xFF});
  CHECK(v.texcoord[0] == 17.0f);
  CHECK(v.texcoord[1] == 18.0f);
  CHECK(m0.index(2) == 2);

  const auto& m1 = s.meshes[1];
  CHECK(m1.support_type == 4);
  CHECK(m1.texture.empty());
  CHECK(m1.style.empty());
  CHECK(m1.index_count == 6);
  CHECK(m1.index(0) == 2);
  CHECK(m1.vertex(0).position[0] == 30.0f);

  REQUIRE(s.effects.size() == 1);
  CHECK(s.effects[0].name == "hook");
  CHECK(s.effects[0].transform[0] == 1.0f);
  CHECK(s.effects[0].transform[5] == 1.0f);
  CHECK(s.effects[0].transform[1] == 0.0f);
}

TEST_CASE("static shape edge cases") {
  SUBCASE("an index array that leaves its block makes the shape unreadable") {
    const auto overlay = make(structure(2, kBase + kEnd - 8));
    const auto items = read_static_shapes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].shape.has_value());
  }

  SUBCASE("a mesh count that cannot fit is refused") {
    const auto overlay = make(structure(0x7FFFFFFF));
    const auto items = read_static_shapes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].shape.has_value());
  }

  SUBCASE("a reference with the wrong tag is not used") {
    const auto overlay = make(structure(), reference_rows(kNameHook));
    const auto items = read_static_shapes(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].shape.has_value());
    CHECK(items[0].shape->meshes[0].texture == "Tex");
    CHECK(items[0].shape->meshes[0].style.empty());
  }

  SUBCASE("a file without references still reads, with empty names") {
    auto overlay = Overlay::from_bytes(build_file(5, {{names()}, {structure()}, {symbol_row()}}, {}), std::nullopt);
    REQUIRE(overlay.has_value());
    const auto items = read_static_shapes(*overlay, overlay->common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].shape.has_value());
    CHECK(items[0].shape->meshes[0].texture.empty());
  }

  SUBCASE("a header shorter than 56 bytes is unreadable") {
    auto block = structure();
    // Point the symbol at the last 8 bytes of the block.
    auto overlay = Overlay::from_bytes(
        build_file(5, {{names()}, {block}, {[] {
                         std::vector<std::byte> out;
                         words(out, {kNameShape, kBase + kEnd - 8});
                         append_u16(out, 1);
                         append_u16(out, 0);
                         append_u32(out, 0);
                         return out;
                       }()}}, {}),
        std::nullopt);
    REQUIRE(overlay.has_value());
    const auto items = read_static_shapes(*overlay, overlay->common());
    REQUIRE(items.size() == 1);
    CHECK_FALSE(items[0].shape.has_value());
  }
}

TEST_CASE("reference table size must be a whole number of rows") {
  auto bytes = build_file(5, {{names()}, {structure()}, {symbol_row(), {}, bytes_of({1, 2, 3, 4, 5, 6})}}, {});
  const auto overlay = Overlay::from_bytes(std::move(bytes), std::nullopt);
  REQUIRE(overlay.has_value());
  const auto refs = rct3::ovl::read_references(overlay->common().file, overlay->common().bytes, overlay->space());
  REQUIRE_FALSE(refs.has_value());
  CHECK(refs.error() == rct3::ovl::Error::BadReferenceTable);
  // The shape still reads: references are optional.
  const auto items = read_static_shapes(*overlay, overlay->common());
  REQUIRE(items.size() == 1);
  CHECK(items[0].shape.has_value());
}
