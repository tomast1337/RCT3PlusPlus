// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/texture.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_textures;

namespace
{

    // Name block, 16 bytes: "T:tex" 0, "Sty:txs" 6. The structure block follows at address 16.
    constexpr std::uint32_t kBase = 16;
    constexpr std::uint32_t kNameStyle = 6;

    // Offsets inside the structure block.
    constexpr std::uint32_t kHeader = 0; // 56 bytes, the last word is the node cell pointer
    constexpr std::uint32_t kCell = 56;  // pointer to the node
    constexpr std::uint32_t kNode = 60;  // 12 bytes
    constexpr std::uint32_t kEnd = 72;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    std::vector<std::byte> names()
    {
        std::string text("T:tex\0Sty:txs\0", 14);
        text.resize(16, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        words(out, { 0, kBase + kHeader });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference_row()
    {
        std::vector<std::byte> out;
        words(out, { kBase + kHeader + 0x2c, kNameStyle, 0, 0 });
        return out;
    }

    std::vector<std::byte> structure(std::uint32_t table_value = 7, std::uint32_t cell = kBase + kCell, std::uint32_t node_address = kBase + kNode)
    {
        std::vector<std::byte> out;
        for (int i = 0; i < 16; ++i)
            append_u16(out, static_cast<std::uint16_t>(table_value));
        words(out, { 1, 8, 16, 0 });                                // parameters and the style field
        append_u16(out, 1);                                         // first
        append_u16(out, 2);                                         // count
        words(out, { cell });                                       // node cell pointer
        words(out, { node_address });                               // the cell
        words(out, { 0, 12, std::bit_cast<std::uint32_t>(30.0f) }); // the node
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, reference_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("textures expose parameters, style and node")
{
    const auto overlay = make(structure());
    const auto items = read_textures(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "T");
    REQUIRE(items[0].texture.has_value());

    const auto& t = *items[0].texture;
    CHECK(t.table[0] == 7);
    CHECK(t.table[15] == 7);
    CHECK(t.parameters[0] == 1);
    CHECK(t.parameters[1] == 8);
    CHECK(t.parameters[2] == 16);
    CHECK(t.style == "Sty");
    CHECK(t.first == 1);
    CHECK(t.count == 2);
    REQUIRE(t.node.has_value());
    CHECK(t.node->address == kBase + kNode);
    CHECK(t.node->first == 0);
    CHECK(t.node->second == 12);
    CHECK(t.node->scale == 30.0f);
}

TEST_CASE("texture edge cases")
{
    SUBCASE("no node pointer means no node")
    {
        const auto overlay = make(structure(7, 0));
        const auto items = read_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].texture.has_value());
        CHECK_FALSE(items[0].texture->node.has_value());
    }

    SUBCASE("a node that leaves its block makes the texture unreadable")
    {
        const auto overlay = make(structure(7, kBase + kCell, kBase + kEnd - 4));
        const auto items = read_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }

    SUBCASE("a different table value is another record layout and is refused")
    {
        const auto overlay = make(structure(6));
        const auto items = read_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }

    SUBCASE("a record shorter than its header is unreadable")
    {
        auto block = structure();
        block.resize(40);
        const auto overlay = make(block);
        const auto items = read_textures(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].texture.has_value());
    }
}
