// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/text.hpp"

#include "builder.hpp"
#include "strings/unicode.hpp"

#include <doctest/doctest.h>
#include <string>
#include <vector>

using namespace rct3::test;
using rct3::ovl::Overlay;
using rct3::ovl::read_texts;
using rct3::strings::to_utf8;

namespace
{

    std::vector<std::byte> utf16(std::u16string_view text, bool terminate = true)
    {
        std::vector<std::byte> out;
        for (char16_t c : text)
            append_u16(out, static_cast<std::uint16_t>(c));
        if (terminate)
            append_u16(out, 0);
        return out;
    }

    std::vector<std::byte> row(std::uint32_t name, std::uint32_t data, std::uint16_t is_pointer)
    {
        std::vector<std::byte> out;
        append_u32(out, name);
        append_u32(out, data);
        append_u16(out, is_pointer);
        append_u16(out, 0xFFFF);
        append_u32(out, 0);
        return out;
    }

    std::vector<std::byte> concat(std::vector<std::byte> a, const std::vector<std::byte>& b)
    {
        a.insert(a.end(), b.begin(), b.end());
        return a;
    }

} // namespace

TEST_CASE("text items are read from txt symbols")
{
    // Block 0 holds: "Hello:txt" (ascii, 10 bytes with NUL) at 0, then UTF-16 "Hi" + NUL at 10 (6 bytes),
    // "Other:ftx" at 16 (10 bytes), "Bad:txt" at 26 (8 bytes), and an unterminated UTF-16 "x" at 34.
    std::vector<std::byte> strings = bytes_of_string(std::string("Hello:txt\0", 10));
    strings = concat(strings, utf16(u"Hi"));
    strings = concat(strings, bytes_of_string(std::string("Other:ftx\0", 10)));
    strings = concat(strings, bytes_of_string(std::string("Bad:txt\0", 8)));
    strings = concat(strings, utf16(u"x", false));

    std::vector<std::byte> symbols;
    symbols = concat(symbols, row(0, 10, 1));  // Hello:txt -> "Hi"
    symbols = concat(symbols, row(16, 10, 1)); // Other:ftx, not text
    symbols = concat(symbols, row(26, 34, 1)); // Bad:txt -> unterminated
    symbols = concat(symbols, row(0, 10, 0));  // Hello:txt as a value, skipped

    auto overlay = Overlay::from_bytes(build_file(5, { { strings }, { }, { symbols } }, { }), std::nullopt);
    REQUIRE(overlay.has_value());

    const auto items = read_texts(*overlay, overlay->common());
    REQUIRE(items.size() == 2);
    CHECK(items[0].name == "Hello");
    REQUIRE(items[0].value.has_value());
    CHECK(*items[0].value == u"Hi");
    CHECK(items[1].name == "Bad");
    CHECK_FALSE(items[1].value.has_value());
}

TEST_CASE("a text in the common file is readable from the unique file's symbols")
{
    const auto common = build_file(5, { { concat(utf16(u"caf\u00e9"), bytes_of_string(std::string("T:txt\0", 6))) } }, { });
    // The unique symbol's name and data both live in the common file. Common data is 16 bytes.
    const auto unique = build_file(5, { { }, { }, { row(10, 0, 1) } }, { });

    auto overlay = Overlay::from_bytes(common, unique);
    REQUIRE(overlay.has_value());
    REQUIRE(overlay->unique() != nullptr);
    const auto items = read_texts(*overlay, *overlay->unique());
    REQUIRE(items.size() == 1);
    CHECK(items[0].name == "T");
    REQUIRE(items[0].value.has_value());
    CHECK(to_utf8(*items[0].value) == "caf\xc3\xa9");
}
