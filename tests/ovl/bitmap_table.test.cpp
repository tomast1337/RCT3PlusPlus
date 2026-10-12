// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/bitmap_table.hpp"

#include "builder.hpp"

#include <array>
#include <cstring>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::bitmap_data_size;
using rct3::ovl::bitmap_layout;
using rct3::ovl::BitmapLayout;
using rct3::ovl::Overlay;
using rct3::ovl::read_bitmap_tables;

namespace
{

    void words(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }

    std::vector<std::byte> chunk(const std::vector<std::byte>& body)
    {
        std::vector<std::byte> out;
        append_u32(out, static_cast<std::uint32_t>(body.size()));
        out.insert(out.end(), body.begin(), body.end());
        return out;
    }

    // Chunk 0: two reserved words then {format, width, height, levels} per bitmap.
    std::vector<std::byte> descriptions(std::initializer_list<std::array<std::uint32_t, 4>> bitmaps, std::uint32_t reserved = 0)
    {
        std::vector<std::byte> out;
        words(out, { reserved, 0 });
        for (const auto& b : bitmaps)
            words(out, { b[0], b[1], b[2], b[3] });
        return out;
    }

    // A version 1 file with one loader of tag "btbl" and one loader row of that type. The header from
    // build_file has no loaders, so the loader list is spliced in after its zero count.
    std::vector<std::byte> make_file(std::uint32_t count, const std::vector<std::byte>& chunk0, const std::vector<std::byte>& chunk1, std::uint32_t extra_count = 2)
    {
        std::vector<std::byte> row;
        words(row, { 0, 20, extra_count, 0, 0 }); // type, data address, chunk count, symbol, resolve
        std::vector<std::byte> data;
        words(data, { 0, count });

        std::vector<std::byte> tail = chunk(chunk0);
        const auto second = chunk(chunk1);
        tail.insert(tail.end(), second.begin(), second.end());
        auto file = build_file(1, { { }, { }, { { }, row, data } }, { }, tail);

        // Offset 20 holds the loader count of a version 1 header, entries follow it.
        Builder b;
        b.str("FGDK");
        b.str("BmpTbl");
        b.u32(1);
        b.str("btbl");
        const std::uint32_t one = 1;
        std::memcpy(file.data() + 20, &one, 4);
        file.insert(file.begin() + 24, b.bytes.begin(), b.bytes.end());
        return file;
    }

    std::vector<std::byte> filled(std::size_t size, std::uint8_t first)
    {
        std::vector<std::byte> out(size);
        for (std::size_t i = 0; i < size; ++i)
            out[i] = static_cast<std::byte>(first + i);
        return out;
    }

    Overlay load(std::vector<std::byte> file)
    {
        auto overlay = Overlay::from_bytes(std::move(file), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

} // namespace

TEST_CASE("bitmap formats map to a storage layout and size")
{
    CHECK(bitmap_layout(2) == BitmapLayout::Pixels32);
    CHECK(bitmap_layout(18) == BitmapLayout::Blocks8);
    CHECK(bitmap_layout(19) == BitmapLayout::Blocks16);
    CHECK_FALSE(bitmap_layout(0).has_value());
    CHECK_FALSE(bitmap_layout(20).has_value());

    CHECK(bitmap_data_size(2, 4, 4, 1) == 64);
    CHECK(bitmap_data_size(2, 4, 4, 3) == 64 + 16 + 4);
    CHECK(bitmap_data_size(18, 8, 8, 1) == 32);
    // Levels below one block still take a whole block.
    CHECK(bitmap_data_size(18, 4, 4, 3) == 8 * 3);
    CHECK(bitmap_data_size(19, 8, 4, 2) == 32 + 16);
    CHECK_FALSE(bitmap_data_size(7, 4, 4, 1).has_value());
}

TEST_CASE("bitmap tables slice the pixel chunk by bitmap")
{
    // 4x4 32-bit with 2 levels (64 + 16), then 8x4 in 8-byte blocks with 1 level (16).
    const auto pixels = filled(96, 10);
    const auto overlay = load(make_file(2, descriptions({ { 2, 4, 4, 2 }, { 18, 8, 4, 1 } }), pixels));
    const auto items = read_bitmap_tables(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].table.has_value());

    const auto& bitmaps = items[0].table->bitmaps;
    REQUIRE(bitmaps.size() == 2);
    CHECK(bitmaps[0].format == 2);
    CHECK(bitmaps[0].width == 4);
    CHECK(bitmaps[0].levels == 2);
    REQUIRE(bitmaps[0].data.size() == 80);
    CHECK(bitmaps[0].data[0] == std::byte { 10 });
    REQUIRE(bitmaps[1].data.size() == 16);
    CHECK(bitmaps[1].data[0] == std::byte { 90 });
    CHECK(bitmaps[1].data[15] == std::byte { 105 });
}

TEST_CASE("an empty bitmap table reads")
{
    const auto overlay = load(make_file(0, descriptions({ }), { }));
    const auto items = read_bitmap_tables(overlay, overlay.common());
    REQUIRE(items.size() == 1);
    REQUIRE(items[0].table.has_value());
    CHECK(items[0].table->bitmaps.empty());
}

TEST_CASE("bitmap table edge cases")
{
    SUBCASE("pixel data that does not match the descriptions is unreadable")
    {
        const auto overlay = load(make_file(1, descriptions({ { 2, 4, 4, 1 } }), filled(63, 0)));
        CHECK_FALSE(read_bitmap_tables(overlay, overlay.common())[0].table.has_value());
        const auto longer = load(make_file(1, descriptions({ { 2, 4, 4, 1 } }), filled(65, 0)));
        CHECK_FALSE(read_bitmap_tables(longer, longer.common())[0].table.has_value());
    }

    SUBCASE("a bitmap count that the description chunk cannot hold is unreadable")
    {
        const auto overlay = load(make_file(0x7FFFFFFF, descriptions({ { 2, 4, 4, 1 } }), filled(64, 0)));
        CHECK_FALSE(read_bitmap_tables(overlay, overlay.common())[0].table.has_value());
    }

    SUBCASE("unknown format, oversized side and bad level count are unreadable")
    {
        for (const auto& bad : { std::array<std::uint32_t, 4> { 5, 4, 4, 1 }, { 2, 4096, 4, 1 }, { 2, 4, 4, 0 }, { 2, 4, 4, 12 } })
        {
            const auto overlay = load(make_file(1, descriptions({ bad }), filled(64, 0)));
            CHECK_FALSE(read_bitmap_tables(overlay, overlay.common())[0].table.has_value());
        }
    }

    SUBCASE("a row without exactly two chunks is unreadable")
    {
        const auto overlay = load(make_file(0, descriptions({ }), { }, 1));
        const auto items = read_bitmap_tables(overlay, overlay.common());
        REQUIRE(items.size() == 1);
        CHECK_FALSE(items[0].table.has_value());
    }
}
