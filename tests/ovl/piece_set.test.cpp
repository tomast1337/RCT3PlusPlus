// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/piece_set.hpp"

#include "builder.hpp"

#include <bit>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;

namespace
{

    constexpr char kNameData[] = "Road:ptd\0Lane:qtd\0Bath:pty\0Pen:enc\0Own\0PieceA\0PieceB\0Label:txt\0Icon:gsi\0Ground:ftx\0Cat:rcg\0"
                                 "Spl1:spl\0Spl2:spl\0V0:svd\0V1:svd\0ItemName:txt";
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

    std::vector<std::byte> symbol(const std::string& full)
    {
        std::vector<std::byte> out;
        append_u32(out, name_at(full));
        append_u32(out, kBase); // every structure starts at the first byte of the block
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

    // A scenery item structure of the base size with the given number of visual references. Item `at`
    // starts at byte offset `at` of the block. A second reference is stored right after the structure.
    void put_item(Words& w, std::size_t at, std::uint32_t flags, std::uint32_t visuals, std::uint32_t version = 0)
    {
        w.set(at + 4, flags);
        w.set(at + 8, version << 16);
        w.set(at + 0x50, visuals);
        w.set(at + 0x54, kBase + static_cast<std::uint32_t>(at) + 0xd4);
    }

    Overlay make(const std::vector<std::byte>& data, const std::string& symbol_name, const std::vector<std::byte>& refs)
    {
        auto overlay = Overlay::from_bytes(build_file(5, { { names() }, { data }, { symbol(symbol_name), { }, refs } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("path types in the base layout")
{
    // Header 0x150, one research row (12 bytes), item with two visuals (220 bytes), plain item (216 bytes).
    constexpr std::size_t kRow = 0x150;
    constexpr std::size_t kFirst = kRow + 12;
    constexpr std::size_t kSecond = kFirst + 216 + 4;
    Words w(kSecond + 216);
    w.set(0, 1);
    w.set(4, name_at("Own"));
    w.set(0x10, name_at("PieceA"));
    w.set(0x14, name_at("PieceB"));
    w.set(0x148, 1);
    w.set(0x14c, kBase + kRow);
    w.set(kRow + 4, 0xFFFFFFFFu);
    put_item(w, kFirst, 0x10c0, 2);
    put_item(w, kSecond, 0x20, 1);
    const auto refs = concat({ reference(kBase + 8, "Label:txt"), reference(kBase + 0xc, "Icon:gsi"), reference(kBase + kRow, "Cat:rcg"), reference(kBase + kFirst + 0xd4, "V0:svd"), reference(kBase + kFirst + 0xd8, "V1:svd"), reference(kBase + kFirst + 0x78, "ItemName:txt") });

    const auto overlay = make(w.bytes(), "Road:ptd", refs);
    const auto types = rct3::ovl::read_path_types(overlay, overlay.common());
    REQUIRE(types.size() == 1);
    REQUIRE(types[0].path.has_value());
    const auto& p = *types[0].path;
    CHECK(types[0].name == "Road");
    CHECK(p.header_word == 1);
    CHECK(p.name == "Own");
    CHECK(p.name_text == "Label");
    CHECK(p.icon == "Icon");
    REQUIRE(p.piece_names.size() == 78);
    CHECK(p.piece_names[0] == "PieceA");
    CHECK(p.piece_names[1] == "PieceB");
    REQUIRE(p.research.size() == 1);
    CHECK(p.research[0].category == "Cat");
    CHECK(p.research[0].unknown == -1);
    REQUIRE(p.items.size() == 2);
    CHECK(p.items[0].flags == 0x10c0);
    REQUIRE(p.items[0].visuals.size() == 2);
    CHECK(p.items[0].visuals[0] == "V0");
    CHECK(p.items[0].visuals[1] == "V1");
    CHECK(p.items[0].name_text == "ItemName");
    CHECK(p.items[1].flags == 0x20);
}

TEST_CASE("path types in the longer layout have no research table")
{
    constexpr std::size_t kHeader = 0x25c;
    Words w(kHeader + 232);
    w.set(0, 0x01000003u);
    w.set(4, name_at("Own"));
    w.set(0x10, name_at("PieceA"));
    w.set(0x258, name_at("PieceB"));
    put_item(w, kHeader, 0x40, 1, 1);
    const auto overlay = make(w.bytes(), "Road:ptd", { });
    const auto types = rct3::ovl::read_path_types(overlay, overlay.common());
    REQUIRE(types.size() == 1);
    REQUIRE(types[0].path.has_value());
    const auto& p = *types[0].path;
    CHECK(p.header_word == 0x01000003u);
    REQUIRE(p.piece_names.size() == 147);
    CHECK(p.piece_names[0] == "PieceA");
    CHECK(p.piece_names[146] == "PieceB");
    CHECK(p.research.empty());
    REQUIRE(p.items.size() == 1);
    CHECK(p.items[0].version == 1);
}

TEST_CASE("queue types")
{
    constexpr std::size_t kItems = 0x34;
    Words w(kItems + 216);
    w.set(0, name_at("Own"));
    w.set(0x10, name_at("PieceA"));
    w.set(0x28, name_at("PieceB"));
    put_item(w, kItems, 0xc0, 1);
    const auto refs = concat({ reference(kBase + 4, "Label:txt"), reference(kBase + 8, "Icon:gsi"), reference(kBase + 0xc, "Ground:ftx") });
    const auto overlay = make(w.bytes(), "Lane:qtd", refs);
    const auto types = rct3::ovl::read_queue_types(overlay, overlay.common());
    REQUIRE(types.size() == 1);
    REQUIRE(types[0].queue.has_value());
    const auto& q = *types[0].queue;
    CHECK(q.name == "Own");
    CHECK(q.name_text == "Label");
    CHECK(q.ground == "Ground");
    CHECK(q.piece_names[0] == "PieceA");
    CHECK(q.piece_names[6] == "PieceB");
    CHECK(q.research.empty());
    REQUIRE(q.items.size() == 1);
    CHECK(q.items[0].flags == 0xc0);
}

TEST_CASE("pool types")
{
    constexpr std::size_t kItems = 0x258;
    Words w(kItems + 232);
    w.set(0, name_at("Own"));
    w.set(0xc, name_at("PieceA"));
    w.set(0xc + 88 * 4, name_at("PieceB"));
    w.set(0x170 + 3 * 4, 2000);
    w.set(0x170 + 5 * 4, static_cast<std::uint32_t>(-2000));
    w.real(0x1b8, 0.5f);
    put_item(w, kItems, 0x40, 1, 1);
    const auto refs = concat({ reference(kBase + 0x1c8, "Spl1:spl"), reference(kBase + 0x1cc, "Spl2:spl"), reference(kBase + 4, "Label:txt") });
    const auto overlay = make(w.bytes(), "Bath:pty", refs);
    const auto pools = rct3::ovl::read_pool_types(overlay, overlay.common());
    REQUIRE(pools.size() == 1);
    REQUIRE(pools[0].pool.has_value());
    const auto& p = *pools[0].pool;
    CHECK(p.name == "Own");
    CHECK(p.name_text == "Label");
    REQUIRE(p.piece_names.size() == 89);
    CHECK(p.piece_names[0] == "PieceA");
    CHECK(p.piece_names[88] == "PieceB");
    CHECK(p.unknown_values[3] == 2000);
    CHECK(p.unknown_values[5] == -2000);
    CHECK(p.unknown_floats[0] == 0.5f);
    REQUIRE(p.splines.size() == 36);
    CHECK(p.splines[0] == "Spl1");
    CHECK(p.splines[1] == "Spl2");
    CHECK(p.splines[2].empty());
    REQUIRE(p.items.size() == 1);
}

TEST_CASE("enclosure types")
{
    constexpr std::size_t kItems = 0x5c;
    Words w(kItems + 240);
    w.set(0, name_at("Own"));
    w.set(0xc, name_at("PieceA"));
    w.set(0x2c, name_at("PieceB"));
    w.set(0x30, 2000);
    w.set(0x34, static_cast<std::uint32_t>(-1700));
    w.real(0x50, 3.0f);
    put_item(w, kItems, 0x2040, 1, 2);
    const auto refs = concat({ reference(kBase + 0x40, "Spl1:spl"), reference(kBase + 0x4c, "Spl2:spl") });
    const auto overlay = make(w.bytes(), "Pen:enc", refs);
    const auto types = rct3::ovl::read_enclosure_types(overlay, overlay.common());
    REQUIRE(types.size() == 1);
    REQUIRE(types[0].enclosure.has_value());
    const auto& e = *types[0].enclosure;
    CHECK(e.piece_names[0] == "PieceA");
    CHECK(e.piece_names[8] == "PieceB");
    CHECK(e.unknown_values[0] == 2000);
    CHECK(e.unknown_values[1] == -1700);
    CHECK(e.splines[0] == "Spl1");
    CHECK(e.splines[3] == "Spl2");
    CHECK(e.unknown_floats[0] == 3.0f);
    REQUIRE(e.items.size() == 1);
    CHECK(e.items[0].version == 2);
}

TEST_CASE("piece set edge cases")
{
    SUBCASE("a header cut short by its block is unreadable")
    {
        Words w(0x20);
        const auto overlay = make(w.bytes(), "Lane:qtd", { });
        const auto types = rct3::ovl::read_queue_types(overlay, overlay.common());
        REQUIRE(types.size() == 1);
        CHECK_FALSE(types[0].queue.has_value());
    }

    SUBCASE("trailing bytes that do not make a whole item make the type unreadable")
    {
        Words w(0x34 + 216 + 8);
        put_item(w, 0x34, 0xc0, 1);
        const auto overlay = make(w.bytes(), "Lane:qtd", { });
        const auto types = rct3::ovl::read_queue_types(overlay, overlay.common());
        CHECK_FALSE(types[0].queue.has_value());
    }

    SUBCASE("a header without items has an empty item list")
    {
        Words w(0x34);
        const auto overlay = make(w.bytes(), "Lane:qtd", { });
        const auto types = rct3::ovl::read_queue_types(overlay, overlay.common());
        REQUIRE(types[0].queue.has_value());
        CHECK(types[0].queue->items.empty());
    }

    SUBCASE("an item with an unknown version makes the type unreadable")
    {
        Words w(0x34 + 216);
        put_item(w, 0x34, 0xc0, 1, 7);
        const auto overlay = make(w.bytes(), "Lane:qtd", { });
        const auto types = rct3::ovl::read_queue_types(overlay, overlay.common());
        CHECK_FALSE(types[0].queue.has_value());
    }

    SUBCASE("an unknown path layout is refused")
    {
        Words w(0x300);
        w.set(0, 0x02000001u);
        const auto overlay = make(w.bytes(), "Road:ptd", { });
        const auto types = rct3::ovl::read_path_types(overlay, overlay.common());
        CHECK_FALSE(types[0].path.has_value());
    }

    SUBCASE("a research count that does not fit is refused")
    {
        Words w(0x150);
        w.set(0, 1);
        w.set(0x148, 0xFFFFFFFFu);
        const auto overlay = make(w.bytes(), "Road:ptd", { });
        const auto types = rct3::ovl::read_path_types(overlay, overlay.common());
        CHECK_FALSE(types[0].path.has_value());
    }
}
