// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/scenery_item.hpp"

#include "builder.hpp"
#include "core/types.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_scenery_items;
using rct3::ovl::SoundCue;
using rct3::ovl::SoundStep;

namespace
{

    constexpr char kNameData[] = "Item:sid\0Group\0Path\0Key\0Val\0Icon:gsi\0GIcon:gsi\0GText:txt\0NameT:txt\0V0:svd\0V1:svd\0"
                                 "S0:snd\0S1:snd\0Cat:rcg";
    const std::string kNames(kNameData, sizeof kNameData);            // includes the final terminator
    const u32 kBase = (static_cast<u32>(sizeof kNameData) + 3) & ~3u; // padded for the builder

    u32 name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<u32>(at);
    }

    // Offsets inside the structure block.
    constexpr u32 kItem = 0;
    constexpr u32 kSquares = 240;     // 2 x 20 bytes
    constexpr u32 kMasks = 280;       // 3 words
    constexpr u32 kNodes = 292;       // 88 bytes
    constexpr u32 kParams = 380;      // 1 x 8 bytes
    constexpr u32 kSounds = 388;      // 1 x 16 bytes
    constexpr u32 kSoundSlots = 404;  // 2 words
    constexpr u32 kScripts = 412;     // 1 word
    constexpr u32 kVisualSlots = 416; // 2 words
    constexpr u32 kResearch = 424;    // 12 bytes
    constexpr u32 kScript = 436;      // 8 words
    constexpr u32 kEnd = 468;

    struct Words
    {
        std::vector<u32> w;
        explicit Words(usize n)
            : w(n, 0)
        {
        }
        void set(usize byte_offset, u32 v)
        {
            w.at(byte_offset / 4) = v;
        }
        void real(usize byte_offset, f32 v)
        {
            set(byte_offset, std::bit_cast<u32>(v));
        }
    };

    std::vector<std::byte> names()
    {
        std::string text = kNames;
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row()
    {
        std::vector<std::byte> out;
        append_u32(out, 0);
        append_u32(out, kBase + kItem);
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference_row(u32 location, const std::string& name)
    {
        std::vector<std::byte> out;
        for (auto v : { location, name_at(name), u32 { 0 }, u32 { 0 } })
            append_u32(out, v);
        return out;
    }

    std::vector<std::byte> concat(std::vector<std::vector<std::byte>> parts)
    {
        std::vector<std::byte> out;
        for (auto& p : parts)
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    std::vector<std::byte> all_references()
    {
        return concat({ reference_row(kBase + 0x58, "Icon:gsi"), reference_row(kBase + 0x5c, "GIcon:gsi"), reference_row(kBase + 0x60, "GText:txt"), reference_row(kBase + 0x78, "NameT:txt"), reference_row(kBase + kVisualSlots, "V0:svd"), reference_row(kBase + kVisualSlots + 4, "V1:svd"), reference_row(kBase + kSoundSlots, "S0:snd"), reference_row(kBase + kSoundSlots + 4, "S1:snd"), reference_row(kBase + kResearch, "Cat:rcg") });
    }

    // A second-expansion item (240 bytes) followed by the data it points at.
    std::vector<std::byte> structure(u32 version = 2, u32 x = 2, u32 z = 1)
    {
        Words w(kEnd / 4);
        w.set(4, 0x1041);
        w.set(8, (version << 16) | 3);
        w.set(12, 5);
        w.set(16, x);
        w.set(20, z);
        w.set(24, kBase + kSquares);
        w.real(0x1c, 1.0f);
        w.real(0x20, -1.0f);
        w.real(0x24, 2.0f);
        w.real(0x28, 4.0f);
        w.real(0x2c, 5.0f);
        w.real(0x30, 6.0f);
        w.set(0x34, 1);
        w.set(0x38, kBase + kNodes);
        w.set(0x3c, 1500);
        w.set(0x40, static_cast<u32>(-500));
        w.set(0x44, 5);
        w.set(0x48, 18);
        w.set(0x4c, name_at("Group"));
        w.set(0x50, 2);
        w.set(0x54, kBase + kVisualSlots);
        w.set(0x64, name_at("Path"));
        w.set(0x68, 1);
        w.set(0x6c, kBase + kParams);
        w.set(0x70, 1);
        w.set(0x74, kBase + kSounds);
        w.set(0x7c, 1);
        w.set(0x80, kBase + kResearch);
        w.set(0x84, 1440);
        w.set(0x88, 2880);
        w.set(0x8c, 3);
        w.set(0x90, 4);
        w.set(0x94, 5);
        w.set(0xb8, 7);
        w.set(0xd4, kBase + 4); // some address: marks ride data as present
        w.set(0xdc, 2);
        w.set(0xe0, 1);
        w.real(0xe4, -1.0f);
        w.set(0xe8, 0xFFFFFFFFu);

        // Squares: first has two masks, second has none and a null array.
        w.set(kSquares + 0, 3);
        w.set(kSquares + 4, 0);
        w.set(kSquares + 8, 4);
        w.set(kSquares + 12, kBase + kMasks);
        w.set(kSquares + 16, 2);
        w.set(kSquares + 20, 0);
        w.set(kSquares + 24, static_cast<u32>(-1));
        w.set(kSquares + 28, 0);
        w.set(kSquares + 32, 0);
        w.set(kSquares + 36, 0);
        w.set(kMasks, 0xF);
        w.set(kMasks + 4, 0x3);

        w.real(kNodes + 16, 0.5f);
        w.real(kNodes + 20, 0.25f);
        w.set(kNodes + 12, 9);
        w.set(kParams, name_at("Key"));
        w.set(kParams + 4, name_at("Val"));
        w.set(kSounds, 2);
        w.set(kSounds + 4, kBase + kSoundSlots);
        w.set(kSounds + 8, 1);
        w.set(kSounds + 12, kBase + kScripts);
        w.set(kScripts, kBase + kScript);
        // Start at 0.5 s, bring the volume to 0.25 by 2 s, end at 3 s.
        w.real(kScript, 0.5f);
        w.set(kScript + 4, 1);
        w.real(kScript + 8, 0.5f);
        w.set(kScript + 12, 3);
        w.real(kScript + 16, 0.25f);
        w.real(kScript + 20, 2.0f);
        w.real(kScript + 24, 3.0f);
        w.set(kScript + 28, 0);
        w.set(kResearch + 4, static_cast<u32>(-1));
        w.real(kResearch + 8, 10.0f);

        std::vector<std::byte> out;
        for (auto v : w.w)
            append_u32(out, v);
        REQUIRE(out.size() == kEnd);
        return out;
    }

    Overlay make(const std::vector<std::byte>& block, const std::vector<std::byte>& refs = all_references(), const std::vector<u32>& relocations = { })
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, refs } }, relocations), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("scenery items expose every decoded field")
{
    const auto overlay = make(structure());
    const auto items = read_scenery_items(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "Item");
    REQUIRE(items[0].item.has_value());
    const auto& it = *items[0].item;

    CHECK(it.version == 2);
    CHECK_FALSE(it.compact);
    CHECK(it.flags == 0x1041);
    CHECK(it.positioning == 3);
    CHECK(it.height_squares == 5);
    CHECK(it.x_squares == 2);
    CHECK(it.z_squares == 1);
    CHECK(it.bounds_min[1] == -1.0f);
    CHECK(it.bounds_max[2] == 6.0f);
    CHECK(it.cost == 1500);
    CHECK(it.removal_cost == -500);
    CHECK(it.unknown_44 == 5);
    CHECK(it.type == 18);
    CHECK(it.group == "Group");
    CHECK(it.source_path == "Path");
    CHECK(it.icon == "Icon");
    CHECK(it.group_icon == "GIcon");
    CHECK(it.group_text == "GText");
    CHECK(it.name_text == "NameT");
    CHECK(it.unknown_84[0] == 1440);
    CHECK(it.unknown_84[1] == 2880);
    CHECK(it.default_colours == std::array<u32, 3> { 3, 4, 5 });
    CHECK(it.late_words[(0xb8 - 0x98) / 4] == 7);
    CHECK(it.has_ride_data);
    CHECK(it.addon == 2);
    CHECK(it.addon_extra == 1);
    CHECK(it.unknown_float == -1.0f);
    CHECK(it.unknown_int == -1);

    REQUIRE(it.squares.size() == 2);
    CHECK(it.squares[0].flags == 3);
    CHECK(it.squares[0].max_height == 4);
    REQUIRE(it.squares[0].height_masks.size() == 2);
    CHECK(it.squares[0].height_masks[0] == 0xF);
    CHECK(it.squares[0].height_masks[1] == 0x3);
    CHECK(it.squares[1].min_height == -1);
    CHECK(it.squares[1].height_masks.empty());

    REQUIRE(it.nodes.size() == 1);
    CHECK(it.nodes[0].words[3] == 9);

    REQUIRE(it.visuals.size() == 2);
    CHECK(it.visuals[0] == "V0");
    CHECK(it.visuals[1] == "V1");

    REQUIRE(it.parameters.size() == 1);
    CHECK(it.parameters[0].name == "Key");
    CHECK(it.parameters[0].value == "Val");

    REQUIRE(it.sounds.size() == 1);
    REQUIRE(it.sounds[0].sounds.size() == 2);
    CHECK(it.sounds[0].sounds[0] == "S0");
    CHECK(it.sounds[0].sounds[1] == "S1");
    REQUIRE(it.sounds[0].scripts.size() == 1);
    REQUIRE(it.sounds[0].scripts[0].size() == 2);
    CHECK(it.sounds[0].scripts[0][0] == SoundStep { .at = 0.5f, .cue = SoundCue::Start });
    CHECK(it.sounds[0].scripts[0][1] == SoundStep { .at = 0.5f, .cue = SoundCue::Volume, .value = 0.25f, .until = 2.0f });

    REQUIRE(it.research.size() == 1);
    CHECK(it.research[0].category == "Cat");
    CHECK(it.research[0].unknown == -1);
    CHECK(it.research[0].level == 10.0f);
}

TEST_CASE("scenery item sizes follow the version word")
{
    SUBCASE("version 0 uses the 216-byte form and ignores the expansion words")
    {
        const auto overlay = make(structure(0));
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items[0].item.has_value());
        CHECK(items[0].item->version == 0);
        CHECK_FALSE(items[0].item->compact);
        CHECK(items[0].item->addon == 0);
        CHECK_FALSE(items[0].item->has_ride_data);
    }

    SUBCASE("version 1 has no second-expansion words")
    {
        const auto overlay = make(structure(1));
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items[0].item.has_value());
        CHECK(items[0].item->addon == 2);
        CHECK(items[0].item->unknown_float == 0.0f);
        CHECK(items[0].item->unknown_int == 0);
    }

    SUBCASE("an unknown version is refused")
    {
        const auto overlay = make(structure(3));
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].item.has_value());
    }
}

TEST_CASE("scenery items in the compact form")
{
    // The compact form is 164 bytes. Put it alone in its block so the longer form cannot fit.
    Words w(164 / 4);
    w.set(4, 0xC9);
    w.set(8, 4);
    w.set(12, 2);
    w.set(16, 1);
    w.set(20, 1);
    w.set(24, kBase + 164); // one square follows
    w.set(0x24, 2500);
    w.set(0x28, static_cast<u32>(-1000));
    w.set(0x2c, 5);
    w.set(0x30, 22);
    w.set(0x34, name_at("Group"));
    w.set(0x38, 1);
    w.set(0x3c, kBase + 164 + 20);
    w.set(0x48, name_at("Path"));
    std::vector<std::byte> block;
    for (auto v : w.w)
        append_u32(block, v);
    for (auto v : { u32 { 1 }, u32 { 0 }, u32 { 2 }, u32 { 0 }, u32 { 0 } })
        append_u32(block, v);
    append_u32(block, 0); // the visual slot

    const auto refs = concat({ reference_row(kBase + 164 + 20, "V0:svd"), reference_row(kBase + 0x5c, "NameT:txt"), reference_row(kBase + 0x40, "Icon:gsi") });
    const auto overlay = make(block, refs);
    const auto items = read_scenery_items(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].item.has_value());
    const auto& it = *items[0].item;
    CHECK(it.compact);
    CHECK(it.version == 0);
    CHECK(it.flags == 0xC9);
    CHECK(it.positioning == 4);
    CHECK(it.cost == 2500);
    CHECK(it.removal_cost == -1000);
    CHECK(it.type == 22);
    CHECK(it.group == "Group");
    CHECK(it.source_path == "Path");
    CHECK(it.name_text == "NameT");
    CHECK(it.icon == "Icon");
    CHECK(it.group_text.empty());
    REQUIRE(it.visuals.size() == 1);
    CHECK(it.visuals[0] == "V0");
    REQUIRE(it.squares.size() == 1);
    CHECK(it.squares[0].flags == 1);
    CHECK(it.squares[0].max_height == 2);
    CHECK(it.nodes.empty());
}

TEST_CASE("scenery item edge cases")
{
    SUBCASE("a footprint that cannot fit is refused without allocating it")
    {
        auto block = structure();
        // x_squares = 0xFFFFFFFF (offset 16)
        for (usize i = 0; i < 4; ++i)
            block[16 + i] = std::byte { 0xFF };
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a mask array that leaves the address space makes the item unreadable")
    {
        auto block = structure();
        // First square's mask pointer (offset 240 + 12) points at the last word of all data, count 2.
        const auto refs = all_references();
        const u32 at = kBase + kEnd + static_cast<u32>(symbol_row().size() + refs.size()) - 4;
        for (usize i = 0; i < 4; ++i)
            block[kSquares + 12 + i] = static_cast<std::byte>((at >> (8 * i)) & 0xFF);
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a null mask pointer with a count gives no masks")
    {
        auto block = structure();
        // Second square: count 3, pointer 0 and not relocated.
        block[kSquares + 20 + 16] = std::byte { 3 };
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items[0].item.has_value());
        CHECK(items[0].item->squares[1].height_masks.empty());
    }

    SUBCASE("a relocated zero pointer is a pointer to the first byte")
    {
        auto block = structure();
        block[kSquares + 20 + 16] = std::byte { 1 };
        const auto overlay = make(block, all_references(), { kBase + kSquares + 20 + 12 });
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items[0].item.has_value());
        REQUIRE(items[0].item->squares[1].height_masks.size() == 1);
        CHECK(items[0].item->squares[1].height_masks[0] == 0x6d657449u); // "Item", the first bytes of the data
    }

    SUBCASE("a mask array may continue into the unique file")
    {
        auto block = structure();
        const auto refs = all_references();
        // The array starts at the last word of the common file's data and has two words.
        const u32 at = kBase + kEnd + static_cast<u32>(symbol_row().size() + refs.size()) - 4;
        for (usize i = 0; i < 4; ++i)
            block[kSquares + 20 + 12 + i] = static_cast<std::byte>((at >> (8 * i)) & 0xFF);
        block[kSquares + 20 + 16] = std::byte { 2 };
        std::vector<std::byte> unique;
        append_u32(unique, 6);
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbol_row(), { }, refs } }, { }), build_file(5, { { unique } }, { }));
        REQUIRE(overlay.has_value());
        const auto items = read_scenery_items(*overlay, overlay->common());
        REQUIRE(items[0].item.has_value());
        REQUIRE(items[0].item->squares[1].height_masks.size() == 2);
        CHECK(items[0].item->squares[1].height_masks[1] == 6);
    }

    SUBCASE("an enormous node count is refused")
    {
        auto block = structure();
        for (usize i = 0; i < 4; ++i)
            block[0x34 + i] = std::byte { 0xFF };
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a sound table that leaves its block makes the item unreadable")
    {
        auto block = structure();
        block[0x70] = std::byte { 200 };
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a missing reference table leaves names empty")
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { structure() }, { symbol_row() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_scenery_items(*overlay, overlay->common());
        REQUIRE(items.size() == 1);
        REQUIRE(items[0].item.has_value());
        const auto& it = *items[0].item;
        CHECK(it.icon.empty());
        CHECK(it.name_text.empty());
        REQUIRE(it.visuals.size() == 2);
        CHECK(it.visuals[0].empty());
        REQUIRE(it.sounds.size() == 1);
        CHECK(it.sounds[0].sounds.size() == 2);
        CHECK(it.group == "Group");
    }

    SUBCASE("a reference of the wrong type is not used")
    {
        const auto refs = concat({ reference_row(kBase + 0x78, "Icon:gsi") });
        const auto overlay = make(structure(), refs);
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items[0].item.has_value());
        CHECK(items[0].item->name_text.empty());
    }

    SUBCASE("a structure cut short by its block is unreadable")
    {
        auto block = structure();
        block.resize(100);
        const auto overlay = make(block);
        const auto items = read_scenery_items(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].item.has_value());
    }
}
