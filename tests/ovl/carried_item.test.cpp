// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/carried_item.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_carried_item_extras;
using rct3::ovl::read_carried_items;

namespace
{

    // Names are NUL separated. Offsets are looked up by text so the test stays readable.
    constexpr char kNameData[] = "Snack:cid\0Wrapper:cid\0Sauce:ced\0Relish:ced\0Dip:ced\0SnackName:txt\0SnackPlural:txt\0SnackIcon:gsi\0"
                                 "SnackShape:shs\0Tex:ftx\0Palette\0PartA\0SauceName:txt\0SauceIcon:gsi\0Odd:cid";
    const std::string kNames(kNameData, sizeof kNameData);
    const std::uint32_t kBase = (static_cast<std::uint32_t>(sizeof kNameData) + 3) & ~3u;

    std::uint32_t name_at(const std::string& needle)
    {
        const auto at = kNames.find(needle);
        REQUIRE(at != std::string::npos);
        return static_cast<std::uint32_t>(at);
    }

    std::vector<std::byte> names()
    {
        std::string text = kNames;
        text.resize(kBase, '\0');
        return bytes_of_string(text);
    }

    std::vector<std::byte> symbol(const std::string& full, std::uint32_t data)
    {
        std::vector<std::byte> out;
        append_u32(out, name_at(full));
        append_u32(out, kBase + data);
        append_u16(out, 1);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> reference(std::uint32_t location, const std::string& name)
    {
        std::vector<std::byte> out;
        for (auto v : { location, name_at(name), std::uint32_t { 0 }, std::uint32_t { 0 } })
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

    struct Words
    {
        std::vector<std::uint32_t> w;
        explicit Words(std::size_t bytes)
            : w(bytes / 4, 0)
        {
        }
        void set(std::size_t at, std::uint32_t v)
        {
            w.at(at / 4) = v;
        }
        void real(std::size_t at, float v)
        {
            set(at, std::bit_cast<std::uint32_t>(v));
        }
        std::vector<std::byte> bytes() const
        {
            std::vector<std::byte> out;
            for (auto v : w)
                append_u32(out, v);
            return out;
        }
    };

    // Layout of the block: item "Snack" at 0 (156 bytes), its two extra slots, item "Wrapper" at 164
    // (156 bytes, no extras), then three 28-byte extras.
    constexpr std::uint32_t kSnack = 0;
    constexpr std::uint32_t kSlots = 156;
    constexpr std::uint32_t kWrapper = 164;
    constexpr std::uint32_t kExtras = 320;
    constexpr std::uint32_t kEnd = kExtras + 3 * 28;

    std::vector<std::byte> block()
    {
        Words w(kEnd);
        w.set(kSnack + 0x00, 1);
        w.set(kSnack + 0x04, 2);
        w.set(kSnack + 0x08, 2);
        w.real(kSnack + 0x20, 40.0f);
        w.set(kSnack + 0x28, 0x201);
        w.set(kSnack + 0x2c, 0xFFFFFFFFu);
        w.set(kSnack + 0x30, 6);
        w.real(kSnack + 0x34, 0.25f);
        w.real(kSnack + 0x38, -0.5f);
        w.set(kSnack + 0x40, 50);
        w.set(kSnack + 0x4c, 170);
        w.real(kSnack + 0x50, 0.01f);
        w.set(kSnack + 0x54, 0xFFFFFFFFu);
        w.set(kSnack + 0x5c, 2);
        w.set(kSnack + 0x60, kBase + kSlots);
        w.set(kSnack + 0x70, 1);
        w.real(kSnack + 0x78, 1.0f);
        w.real(kSnack + 0x7c, 0.75f);
        w.set(kSnack + 0x80, 16);
        w.set(kSnack + 0x84, name_at("PartA"));
        w.set(kSnack + 0x94, 1);
        w.set(kSnack + 0x98, name_at("Palette"));
        w.set(kWrapper + 0x00, 1);
        w.set(kWrapper + 0x5c, 0);

        w.set(kExtras + 0, 0);
        w.set(kExtras + 12, 4);
        w.real(kExtras + 16, 0.1f);
        w.real(kExtras + 20, -0.1f);
        w.real(kExtras + 24, 0.3f);
        return w.bytes();
    }

    std::vector<std::byte> references()
    {
        return concat({ reference(kBase + kSnack + 0x0c, "SnackName:txt"), reference(kBase + kSnack + 0x10, "SnackPlural:txt"), reference(kBase + kSnack + 0x14, "SnackIcon:gsi"), reference(kBase + kSnack + 0x18, "SnackShape:shs"), reference(kBase + kSnack + 0x24, "Wrapper:cid"), reference(kBase + kSnack + 0x74, "Tex:ftx"), reference(kBase + kSlots, "Sauce:ced"), reference(kBase + kSlots + 4, "Dip:ced"), reference(kBase + kExtras + 4, "SauceName:txt"), reference(kBase + kExtras + 8, "SauceIcon:gsi") });
    }

    Overlay make(const std::vector<std::byte>& data, const std::vector<std::byte>& symbols, const std::vector<std::byte>& refs = references())
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { data }, { symbols, { }, refs } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("carried items expose their fields and extras")
{
    const auto overlay = make(block(), concat({ symbol("Snack:cid", kSnack), symbol("Wrapper:cid", kWrapper) }));
    const auto items = read_carried_items(overlay, overlay.common());
    REQUIRE(items.size() == 2);
    CHECK(items[0].name == "Snack");
    REQUIRE(items[0].item.has_value());
    const auto& it = *items[0].item;
    CHECK(it.unknown_00 == 1);
    CHECK(it.unknown_04 == 2);
    CHECK(it.name_text == "SnackName");
    CHECK(it.plural_text == "SnackPlural");
    CHECK(it.icon == "SnackIcon");
    CHECK(it.shapes[0] == "SnackShape");
    CHECK(it.shapes[1].empty());
    CHECK(it.unknown_20 == 40.0f);
    CHECK(it.remains == "Wrapper");
    CHECK(it.flags == 0x201);
    CHECK(it.unknown_2c == -1);
    CHECK(it.unknown_30 == 6);
    CHECK(it.offset[0] == 0.25f);
    CHECK(it.offset[1] == -0.5f);
    CHECK(it.amounts == std::array<std::uint32_t, 4> { 50, 0, 0, 170 });
    CHECK(it.unknown_50 == 0.01f);
    CHECK(it.unknown_54 == -1);
    REQUIRE(it.extras.size() == 2);
    CHECK(it.extras[0] == "Sauce");
    CHECK(it.extras[1] == "Dip");
    CHECK(it.unknown_70 == 1);
    CHECK(it.texture == "Tex");
    CHECK(it.scale[1] == 0.75f);
    CHECK(it.unknown_80 == 16);
    CHECK(it.part_names[0] == "PartA");
    CHECK(it.unknown_94 == 1);
    CHECK(it.style_name == "Palette");

    REQUIRE(items[1].item.has_value());
    CHECK(items[1].item->extras.empty());
    CHECK(items[1].item->name_text.empty());
}

TEST_CASE("carried item string fields that are not relocated are empty")
{
    // The part name pointers are zero and not relocated, so they are null, not the first string.
    const auto overlay = make(block(), symbol("Wrapper:cid", kWrapper));
    const auto items = read_carried_items(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].item.has_value());
    for (const auto& part : items[0].item->part_names)
        CHECK(part.empty());
    CHECK(items[0].item->style_name.empty());
}

TEST_CASE("carried item edge cases")
{
    SUBCASE("an extras array outside its block makes the item unreadable")
    {
        auto data = block();
        data[0x60] = std::byte { 0xFF };
        data[0x61] = std::byte { 0xFF };
        const auto overlay = make(data, symbol("Snack:cid", kSnack));
        const auto items = read_carried_items(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a huge extras count is refused")
    {
        auto data = block();
        for (std::size_t i = 0; i < 4; ++i)
            data[0x5c + i] = std::byte { 0xFF };
        const auto overlay = make(data, symbol("Snack:cid", kSnack));
        const auto items = read_carried_items(overlay, overlay.common());
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a structure cut short by its block is unreadable")
    {
        auto data = block();
        data.resize(100);
        const auto overlay = make(data, symbol("Snack:cid", kSnack));
        const auto items = read_carried_items(overlay, overlay.common());
        CHECK_FALSE(items[0].item.has_value());
    }

    SUBCASE("a missing reference table leaves names empty")
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { block() }, { symbol("Snack:cid", kSnack) } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        const auto items = read_carried_items(*overlay, overlay->common());
        REQUIRE(items[0].item.has_value());
        CHECK(items[0].item->name_text.empty());
        CHECK(items[0].item->extras.size() == 2);
        CHECK(items[0].item->extras[0].empty());
    }
}

TEST_CASE("carried item extras")
{
    const auto overlay = make(block(), concat({ symbol("Sauce:ced", kExtras), symbol("Relish:ced", kExtras + 28) }));
    const auto extras = read_carried_item_extras(overlay, overlay.common());
    REQUIRE(extras.size() == 2);
    CHECK(extras[0].name == "Sauce");
    REQUIRE(extras[0].extra.has_value());
    const auto& e = *extras[0].extra;
    CHECK(e.name_text == "SauceName");
    CHECK(e.icon == "SauceIcon");
    CHECK(e.unknown_0c == 4);
    CHECK(e.offset[0] == 0.1f);
    CHECK(e.offset[1] == -0.1f);
    CHECK(e.offset[2] == 0.3f);
    REQUIRE(extras[1].extra.has_value());
    CHECK(extras[1].extra->name_text.empty());

    SUBCASE("an extra that does not fit its block is unreadable")
    {
        auto data = block();
        data.resize(kExtras + 10);
        const auto cut = make(data, symbol("Sauce:ced", kExtras));
        const auto cut_extras = read_carried_item_extras(cut, cut.common());
        REQUIRE(cut_extras.size() == 1);
        CHECK_FALSE(cut_extras[0].extra.has_value());
    }
}
