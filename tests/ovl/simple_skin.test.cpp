// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/simple_skin.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_simple_skins;

namespace
{

    // Name block: "A:ssk" at 0, padded to 16. The structure block follows at address 16.
    constexpr std::uint32_t kBase = 16;
    constexpr std::uint32_t kIds = 28;      // 2 parts * 4
    constexpr std::uint32_t kCounts = 36;   // 2 parts * 4
    constexpr std::uint32_t kIndices = 44;  // 1 triangle * 6 bytes, padded to 8
    constexpr std::uint32_t kVertices = 52; // 3 vertices * 32

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    void real(std::vector<std::byte>& out, float v)
    {
        append_u32(out, std::bit_cast<std::uint32_t>(v));
    }

    std::vector<std::byte> names()
    {
        std::string text("A:ssk\0", 6);
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { 0, kBase });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> structure(std::uint32_t parts = 2, std::uint32_t triangles = 1, std::uint32_t vertices = 3)
    {
        std::vector<std::byte> out;
        words(out, { parts, kBase + kIds, kBase + kCounts, triangles, kBase + kIndices, vertices, kBase + kVertices });
        words(out, { 100, 110 }); // ids
        words(out, { 1, 2 });     // counts
        for (std::uint16_t i : { 0, 1, 2, 0 })
            append_u16(out, i);
        for (std::uint32_t i = 0; i < 3; ++i)
        {
            for (float v : { float(i), 1.0f, 2.0f, 0.0f, 1.0f, 0.0f, 0.25f * float(i), 0.5f })
                real(out, v);
        }
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("simple skins expose parts, vertices and indices")
{
    const auto overlay = make(structure());
    const auto items = read_simple_skins(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "A");
    REQUIRE(items[0].skin.has_value());

    const auto& s = *items[0].skin;
    REQUIRE(s.part_ids.size() == 2);
    CHECK(s.part_ids[1] == 110);
    CHECK(s.part_vertex_counts[0] == 1);
    REQUIRE(s.vertices.size() == 3);
    CHECK(s.vertices[2].position[0] == 2.0f);
    CHECK(s.vertices[2].normal[2] == 0.0f);
    CHECK(s.vertices[2].uv[0] == 0.5f);
    CHECK(s.vertices[2].uv[1] == 0.5f);
    REQUIRE(s.indices.size() == 3);
    CHECK(s.indices[2] == 2);
}

TEST_CASE("simple skin edge cases")
{
    SUBCASE("a vertex count that cannot fit is refused")
    {
        const auto overlay = make(structure(2, 1, 0x7FFFFFFF));
        CHECK_FALSE(read_simple_skins(overlay, overlay.common())[0].skin.has_value());
    }

    SUBCASE("vertices that leave the block make the skin unreadable")
    {
        const auto overlay = make(structure(2, 1, 4));
        CHECK_FALSE(read_simple_skins(overlay, overlay.common())[0].skin.has_value());
    }

    SUBCASE("a skin without geometry reads")
    {
        const auto overlay = make(structure(0, 0, 0));
        const auto items = read_simple_skins(overlay, overlay.common());
        REQUIRE(items[0].skin.has_value());
        CHECK(items[0].skin->vertices.empty());
        CHECK(items[0].skin->indices.empty());
    }
}
