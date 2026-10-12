// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/string_table.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("missing keys read as empty")
{
    StringTable t;
    t.add("Dialog_Title", u"A title");
    CHECK(t.get("Dialog_Title") == u"A title");
    CHECK(t.contains("Dialog_Title"));
    CHECK(t.get("Dialog_Other").empty());
    CHECK_FALSE(t.contains("Dialog_Other"));
    t.add("Dialog_Title", u"Replaced");
    CHECK(t.get("Dialog_Title") == u"Replaced");
    CHECK(t.size() == 1);
}

TEST_CASE("loading a file that is not there fails")
{
    CHECK_FALSE(StringTable::load("does-not-exist.common.ovl").has_value());
}
