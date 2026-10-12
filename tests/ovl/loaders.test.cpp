// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder.hpp"
#include "ovl/ovl.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Error;
using rct3::ovl::parse;
using rct3::ovl::read_loaders;

namespace
{

    std::vector<std::byte> loader_row(std::uint32_t type, std::uint32_t data, std::uint32_t extra, std::uint32_t symbol, std::uint32_t resolve)
    {
        std::vector<std::byte> row;
        for (auto v : { type, data, extra, symbol, resolve })
            append_u32(row, v);
        return row;
    }

    std::vector<std::byte> concat(std::vector<std::byte> a, const std::vector<std::byte>& b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

    // A chunk: u32 size, then `size` marker bytes.
    std::vector<std::byte> chunk(std::uint32_t size)
    {
        std::vector<std::byte> out;
        append_u32(out, size);
        out.insert(out.end(), size, std::byte { 0x5A });
        return out;
    }

} // namespace

TEST_CASE("loader rows are read with their fields")
{
    const auto rows = concat(loader_row(1, 0x40, 2, 0x10, 3), loader_row(0, 0x50, 0, 0, 0));
    const auto tail = concat(chunk(4), chunk(2));
    const auto file = build_file(1, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { }, tail);

    auto f = parse(file);
    REQUIRE(f.has_value());
    auto t = read_loaders(*f, file);
    REQUIRE(t.has_value());

    REQUIRE(t->entries.size() == 2);
    CHECK(t->entries[0].loader_type == 1);
    CHECK(t->entries[0].data == 0x40);
    CHECK(t->entries[0].extra_count == 2);
    CHECK(t->entries[0].symbol_address == 0x10);
    CHECK(t->entries[0].symbols_to_resolve == 3);
    CHECK(t->entries[1].extra_count == 0);

    REQUIRE(t->extra[0].size() == 2);
    CHECK(t->extra[0][0].size == 4);
    CHECK(t->extra[0][1].size == 2);
    CHECK(t->extra[1].empty());
    CHECK(t->end_offset == file.size());
}

TEST_CASE("v5 packs the chunk count into the low half of the field")
{
    const auto rows = loader_row(0, 0x40, 0xBEEF0001, 0, 0);
    const auto file = build_file(5, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { }, chunk(3));

    auto f = parse(file);
    REQUIRE(f.has_value());
    auto t = read_loaders(*f, file);
    REQUIRE(t.has_value());
    CHECK(t->entries[0].extra_count == 1);
    CHECK(t->entries[0].unknown == 0xBEEF);
    REQUIRE(t->extra[0].size() == 1);
    CHECK(t->extra[0][0].size == 3);
}

TEST_CASE("v4 chunks follow the stream relocation table")
{
    const auto rows = loader_row(0, 0, 1, 0, 0);
    const auto file = build_file(4, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { }, chunk(5));

    auto f = parse(file);
    REQUIRE(f.has_value());
    auto t = read_loaders(*f, file);
    REQUIRE(t.has_value());
    REQUIRE(t->extra[0].size() == 1);
    CHECK(t->extra[0][0].size == 5);
    CHECK(t->end_offset == file.size());
}

TEST_CASE("bytes after the chunk stream are left alone")
{
    const auto rows = loader_row(0, 0, 1, 0, 0);
    const auto tail = concat(chunk(1), bytes_of({ 1, 2, 3, 4, 5 }));
    const auto file = build_file(1, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { }, tail);

    auto f = parse(file);
    REQUIRE(f.has_value());
    auto t = read_loaders(*f, file);
    REQUIRE(t.has_value());
    CHECK(t->end_offset == file.size() - 5);
}

TEST_CASE("loader table edge cases")
{
    SUBCASE("no loader block means no entries")
    {
        const auto file = build_file(5, { }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        auto t = read_loaders(*f, file);
        REQUIRE(t.has_value());
        CHECK(t->entries.empty());
        CHECK(t->end_offset == f->end_offset);
    }

    SUBCASE("a size that is not a whole number of rows is rejected")
    {
        auto rows = loader_row(0, 0, 0, 0, 0);
        rows.pop_back();
        const auto file = build_file(5, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        CHECK(read_loaders(*f, file).error() == Error::BadLoaderTable);
    }

    SUBCASE("a chunk larger than the rest of the file fails")
    {
        std::vector<std::byte> tail;
        append_u32(tail, 1000);
        const auto rows = loader_row(0, 0, 1, 0, 0);
        const auto file = build_file(1, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { }, tail);
        auto f = parse(file);
        REQUIRE(f.has_value());
        CHECK(read_loaders(*f, file).error() == Error::Truncated);
    }

    SUBCASE("a chunk count larger than the file is refused before looping")
    {
        const auto rows = loader_row(0, 0, 0xFFFFFFFF, 0, 0);
        const auto file = build_file(1, { { }, { }, { std::vector<std::byte>(16, std::byte { 0 }), rows } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        CHECK(read_loaders(*f, file).error() == Error::ImplausibleCount);
    }
}
