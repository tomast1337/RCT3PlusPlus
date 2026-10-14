// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/money_text.hpp"

#include <doctest/doctest.h>
#include <limits>

using namespace rct3::ui;

TEST_CASE("a currency is read from its strings")
{
    StringTable strings;
    strings.add("Money3a", u"<");
    strings.add("Money3b", u">");
    strings.add("Money3d", u";");
    const Currency c = currency(strings, 3);
    CHECK(c.before == u"<");
    CHECK(c.after == u">");
    CHECK(c.decimal == u";");
    CHECK(currency(strings, 4).before.empty());
}

TEST_CASE("money is written with its hundredths and grouped by threes")
{
    const Currency c { u"#", u"", u"." };
    CHECK(money_text(c, 0) == u"#0.00");
    CHECK(money_text(c, 7) == u"#0.07");
    CHECK(money_text(c, 12345) == u"#123.45");
    CHECK(money_text(c, 123456789) == u"#1,234,567.89");
    CHECK(money_text(c, -100050) == u"-#1,000.50");
}

TEST_CASE("a comma before the hundredths leaves the whole part ungrouped")
{
    const Currency c { u"", u" Q", u"," };
    CHECK(money_text(c, 123456789) == u"1234567,89 Q");
}

TEST_CASE("a currency without hundredths writes the stored number whole")
{
    const Currency c { u"@", u"", u"" };
    CHECK(money_text(c, 1234567) == u"@1,234,567");
    CHECK(money_text(c, -5) == u"-@5");
}

TEST_CASE("the smallest amount is written without overflow")
{
    const Currency c { u"", u"", u"" };
    CHECK(money_text(c, std::numeric_limits<std::int64_t>::min()) == u"-9,223,372,036,854,775,808");
}
