// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/string_format.hpp"

#include <array>
#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("placeholders are filled in by name, in any order")
{
    const std::array<FormatArg, 2> args { { { u"count", u"12" }, { u"park", u"Lakeside" } } };
    CHECK(format_string(u"{count} guests in {park}", args) == u"12 guests in Lakeside");
    CHECK(format_string(u"{park}: {count}, {count}", args) == u"Lakeside: 12, 12");
}

TEST_CASE("doubled braces are braces, and unknown or broken placeholders stay")
{
    const std::array<FormatArg, 1> args { { { u"n", u"3" } } };
    CHECK(format_string(u"{{n}} is {n}", args) == u"{n} is 3");
    CHECK(format_string(u"{missing} {n", args) == u"{missing} {n");
    CHECK(format_string(u"}} and } alone", args) == u"} and } alone");
    CHECK(format_string(u"", args).empty());
}
