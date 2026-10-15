// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/person_parts.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;

namespace
{

    const std::string kNames("S:ppg\0P:prt\0H:phd\0T:tex\0M:mms\0B:sal\0K:ssk\0", 42);
    constexpr std::uint32_t kBase = 48; // names padded

    std::uint32_t name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<std::uint32_t>(at);
    }

    // Offsets inside the structure block.
    constexpr std::uint32_t kSet = 0;      // 84 bytes
    constexpr std::uint32_t kChances = 84; // one float
    constexpr std::uint32_t kList = 88;    // two reference fields
    constexpr std::uint32_t kPiece = 96;   // 20 bytes
    constexpr std::uint32_t kHead = 116;   // 192 bytes
    constexpr std::uint32_t kEnd = 308;

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    std::vector<std::byte> concat(std::vector<std::vector<std::byte>> parts)
    {
        std::vector<std::byte> out;
        for (auto& p : parts)
            out.insert(out.end(), p.begin(), p.end());
        return out;
    }

    std::vector<std::byte> names()
    {
        std::string text = kNames;
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol_row(const std::string& name, std::uint32_t at)
    {
        std::vector<std::byte> out;
        words(out, { name_at(name), kBase + at });
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference_row(std::uint32_t at, const std::string& name)
    {
        std::vector<std::byte> out;
        words(out, { kBase + at, name_at(name), 0, 0 });
        return out;
    }

    std::vector<std::byte> structure(std::uint8_t level_two_fixed = 1)
    {
        std::vector<std::byte> out;
        words(out, { 1, 0, 0 });
        out.insert(out.end(), { std::byte { 2 }, std::byte { 1 }, std::byte { 1 }, std::byte { 1 } });
        words(out, { 0, kBase + kChances, std::bit_cast<std::uint32_t>(100.0f) });
        // Levels: one fixed and one optional piece; nothing, with a pointer that must not be followed; one fixed.
        words(out, { 0x0101, kBase + kList, 0, 12345, level_two_fixed, kBase + kList });
        words(out, { 0 }); // head reference
        for (int i = 0; i < 6; ++i)
            words(out, { 0 });
        words(out, { 0 }); // swimwear head
        REQUIRE(out.size() == kChances);
        words(out, { std::bit_cast<std::uint32_t>(0.25f), 0, 0 });
        words(out, { 1, 0, 0, 0, 2 });
        REQUIRE(out.size() == kHead);
        out.resize(kEnd, std::byte { 0 });
        return out;
    }

    std::vector<std::byte> references()
    {
        return concat({ reference_row(kList, "P:prt"), reference_row(kList + 4, "P:prt"), reference_row(kSet + 52, "H:phd"), reference_row(kPiece + 8, "T:tex"), reference_row(kPiece + 12, "M:mms"), reference_row(kHead + 136, "T:tex"), reference_row(kHead + 140, "B:sal"), reference_row(kHead + 144, "K:ssk"), reference_row(kHead + 144 + 6 * 4, "K:ssk") });
    }

    Overlay make(const std::vector<std::byte>& block)
    {
        const auto symbols = concat({ symbol_row("S:ppg", kSet), symbol_row("P:prt", kPiece), symbol_row("H:phd", kHead) });
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block }, { symbols, { }, references() } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("a section lists its pieces at each level, fixed first, then optional")
{
    const auto overlay = make(structure());
    const auto items = rct3::ovl::read_body_sections(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "S");
    REQUIRE(items[0].section.has_value());
    const auto& s = *items[0].section;
    CHECK(s.age_group == 2);
    CHECK(s.sex == 1);
    CHECK(s.kind == static_cast<std::uint8_t>(rct3::ovl::SectionKind::Body));
    CHECK(s.optional_chances == std::vector<float> { 0.25f });
    CHECK(s.levels[0].fixed == std::vector<std::string_view> { "P" });
    CHECK(s.levels[0].optional == std::vector<std::string_view> { "P" });
    CHECK(s.levels[1].fixed.empty()); // Its pointer is never followed
    CHECK(s.levels[2].fixed.size() == 1);
    CHECK(s.head == "H");
    CHECK(s.swim_head.empty());
    CHECK(s.swim_levels[0].fixed.empty());
}

TEST_CASE("a level whose list leaves the file makes the section unreadable")
{
    const auto overlay = make(structure(200));
    CHECK_FALSE(rct3::ovl::read_body_sections(overlay, overlay.common())[0].section.has_value());
}

TEST_CASE("a piece names its mesh, its picture and the colour that picks its cell")
{
    const auto overlay = make(structure());
    const auto items = rct3::ovl::read_body_pieces(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].piece.has_value());
    CHECK(items[0].piece->texture == "T");
    CHECK(items[0].piece->mesh == "M");
    CHECK(items[0].piece->channel == static_cast<std::uint32_t>(rct3::ovl::ColourChannel::Bottom));
}

TEST_CASE("a head names its picture, its bones and a skin for each face at each level")
{
    const auto overlay = make(structure());
    const auto items = rct3::ovl::read_head_rigs(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].head.has_value());
    const auto& h = *items[0].head;
    CHECK(h.texture == "T");
    CHECK(h.bones == "B");
    CHECK(h.skins[0] == "K");
    CHECK(h.skins[1].empty());
    CHECK(h.skins[rct3::ovl::kHeadFaces] == "K");
}
