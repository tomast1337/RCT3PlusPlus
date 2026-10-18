// SPDX-License-Identifier: GPL-3.0-or-later
#include "strings/unicode.hpp"

#include <doctest/doctest.h>

using namespace rct3::strings;

TEST_CASE("utf-16 to utf-8")
{
    CHECK(to_utf8(u"abc") == "abc");
    CHECK(to_utf8(u"é") == "\xc3\xa9");
    CHECK(to_utf8(u"€") == "\xe2\x82\xac");
    CHECK(to_utf8(u"\U0001F600") == "\xf0\x9f\x98\x80");
    CHECK(to_utf8(u"").empty());

    SUBCASE("unpaired surrogates become U+FFFD")
    {
        CHECK(
            to_utf8(std::u16string { u'a', char16_t(0xD800), u'b' })
            == "a\xef\xbf\xbd"
               "b");
        CHECK(to_utf8(std::u16string { char16_t(0xDC00) }) == "\xef\xbf\xbd");
        CHECK(to_utf8(std::u16string { char16_t(0xD800) }) == "\xef\xbf\xbd");
    }
}

TEST_CASE("utf-8 to utf-16")
{
    CHECK(to_utf16("abc") == u"abc");
    CHECK(to_utf16("\xc3\xa9") == u"é");
    CHECK(to_utf16("\xe2\x82\xac") == u"€");
    CHECK(to_utf16("\xf0\x9f\x98\x80") == u"\U0001F600");
    CHECK(to_utf16("").empty());

    SUBCASE("each bad byte becomes U+FFFD and the rest still decodes")
    {
        CHECK(
            to_utf16(
                "a\xff"
                "b")
            == u"a�b");
        CHECK(to_utf16("\xc3") == u"�");           // Cut short
        CHECK(to_utf16("\xc0\xaf") == u"��");      // Overlong
        CHECK(to_utf16("\xed\xa0\x80") == u"���"); // A surrogate
        CHECK(
            to_utf16(
                "\xe2\x82"
                "x")
            == u"��x");
    }
    SUBCASE("round trip")
    {
        const std::u16string text = u"Café 乐\U0001F3A2";
        CHECK(to_utf16(to_utf8(text)) == text);
    }
}

TEST_CASE("characters past U+FFFF take two units")
{
    std::u16string out;
    append_utf16(out, U'x');
    append_utf16(out, U'\U0001F3A2');
    REQUIRE(out.size() == 3);
    CHECK(is_high_surrogate(out[1]));
    CHECK(is_low_surrogate(out[2]));
}

TEST_CASE("search folding drops case and the accents of Latin letters")
{
    CHECK(rct3::strings::search_folded(u"Café CRÈME Ñoño") == u"cafe creme nono");
    CHECK(rct3::strings::search_folded(u"Straße ×2") == u"straße ×2");
}
