// SPDX-License-Identifier: GPL-3.0-or-later
#include "builder.hpp"
#include "ovl/ovl.hpp"

#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::AddressSpace;
using rct3::ovl::Error;
using rct3::ovl::parse;
using rct3::ovl::read_symbols;
using rct3::ovl::split_symbol_name;

namespace
{

    // Two NUL-terminated strings back to back: "alpha:ftx" at 0 and "beta:txt" at 10.
    std::vector<std::byte> string_table()
    {
        return bytes_of_string(std::string("alpha:ftx\0beta:txt\0", 20));
    }

    std::vector<std::byte> wide_row(std::uint32_t name, std::uint32_t data, std::uint16_t is_pointer, std::uint32_t hash)
    {
        std::vector<std::byte> row;
        append_u32(row, name);
        append_u32(row, data);
        append_u16(row, is_pointer);
        append_u16(row, 0xFFFF);
        append_u32(row, hash);
        return row;
    }

    std::vector<std::byte> narrow_row(std::uint32_t name, std::uint32_t data, std::uint32_t is_pointer)
    {
        std::vector<std::byte> row;
        append_u32(row, name);
        append_u32(row, data);
        append_u32(row, is_pointer);
        return row;
    }

    std::vector<std::byte> concat(std::vector<std::byte> a, const std::vector<std::byte>& b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

} // namespace

TEST_CASE("symbol names split at the last colon")
{
    CHECK(split_symbol_name("Thing:ftx").name == "Thing");
    CHECK(split_symbol_name("Thing:ftx").tag == "ftx");
    CHECK(split_symbol_name("a:b:txt").name == "a:b");
    CHECK(split_symbol_name("plain").tag.empty());
    CHECK(split_symbol_name("plain").name == "plain");
    CHECK(split_symbol_name("").name.empty());
}

TEST_CASE("v5 symbol table resolves names through the string table")
{
    const auto symbols = concat(wide_row(0, 0x40, 1, 0xAABBCCDD), wide_row(10, 0x3F800000, 0, 0x11));
    const auto file = build_file(5, { { string_table() }, { }, { symbols } }, { });

    auto f = parse(file);
    REQUIRE(f.has_value());
    AddressSpace space;
    space.add(*f, file, 0);

    auto out = read_symbols(*f, file, space);
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 2);

    CHECK((*out)[0].name == "alpha:ftx");
    CHECK((*out)[0].is_pointer);
    CHECK((*out)[0].data == 0x40);
    CHECK((*out)[0].hash == 0xAABBCCDD);
    CHECK((*out)[0].unknown == 0xFFFF);

    CHECK((*out)[1].name == "beta:txt");
    CHECK_FALSE((*out)[1].is_pointer); // an inline value, here the bits of 1.0f
    CHECK((*out)[1].data == 0x3F800000);
}

TEST_CASE("v1 symbol rows are 12 bytes")
{
    const auto symbols = concat(narrow_row(0, 5, 1), narrow_row(10, 6, 0));
    const auto file = build_file(1, { { string_table() }, { }, { symbols } }, { });

    auto f = parse(file);
    REQUIRE(f.has_value());
    AddressSpace space;
    space.add(*f, file, 0);

    auto out = read_symbols(*f, file, space);
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 2);
    CHECK((*out)[0].name == "alpha:ftx");
    CHECK((*out)[0].is_pointer);
    CHECK((*out)[1].name == "beta:txt");
    CHECK_FALSE((*out)[1].is_pointer);
}

TEST_CASE("a unique file's names resolve into its common file")
{
    const auto common_bytes = build_file(5, { { string_table() } }, { });
    auto common = parse(common_bytes);
    REQUIRE(common.has_value());
    CHECK(common->data_size == 20);

    // The unique file holds only a symbol table. Its names point at the common file's strings.
    const auto unique_bytes = build_file(5, { { }, { }, { wide_row(10, 0x20 + 20, 1, 1) } }, { });
    auto unique = parse(unique_bytes);
    REQUIRE(unique.has_value());

    AddressSpace space;
    space.add(*common, common_bytes, 0);
    space.add(*unique, unique_bytes, common->data_size);

    auto out = read_symbols(*unique, unique_bytes, space);
    REQUIRE(out.has_value());
    REQUIRE(out->size() == 1);
    CHECK((*out)[0].name == "beta:txt");

    SUBCASE("without the common file the name is unresolved, not an error")
    {
        AddressSpace alone;
        alone.add(*unique, unique_bytes, common->data_size);
        auto missing = read_symbols(*unique, unique_bytes, alone);
        REQUIRE(missing.has_value());
        CHECK((*missing)[0].name.empty());
    }
}

TEST_CASE("symbol table edge cases")
{
    SUBCASE("no table means no symbols")
    {
        const auto file = build_file(5, { { string_table() } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        AddressSpace space;
        space.add(*f, file, 0);
        auto out = read_symbols(*f, file, space);
        REQUIRE(out.has_value());
        CHECK(out->empty());
    }

    SUBCASE("a size that is not a whole number of rows is rejected")
    {
        auto partial = wide_row(0, 0, 1, 0);
        partial.pop_back();
        const auto file = build_file(5, { { string_table() }, { }, { partial } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        AddressSpace space;
        space.add(*f, file, 0);
        CHECK(read_symbols(*f, file, space).error() == Error::BadSymbolTable);
    }

    SUBCASE("a name pointer past every block leaves the name empty")
    {
        const auto file = build_file(5, { { string_table() }, { }, { wide_row(9999, 0, 1, 0) } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        AddressSpace space;
        space.add(*f, file, 0);
        auto out = read_symbols(*f, file, space);
        REQUIRE(out.has_value());
        CHECK((*out)[0].name.empty());
    }

    SUBCASE("a string with no terminator in its block is not read past the block")
    {
        const auto file = build_file(5, { { bytes_of_string("abc") }, { }, { wide_row(0, 0, 1, 0) } }, { });
        auto f = parse(file);
        REQUIRE(f.has_value());
        AddressSpace space;
        space.add(*f, file, 0);
        auto out = read_symbols(*f, file, space);
        REQUIRE(out.has_value());
        CHECK((*out)[0].name.empty());
    }
}
