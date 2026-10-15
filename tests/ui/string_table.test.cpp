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

TEST_CASE("merging keeps the strings already there")
{
    StringTable a;
    a.add("Ride", u"Ride");
    StringTable b;
    b.add("Ride", u"Attraction");
    b.add("Shop", u"Shop");
    a.merge(b);
    CHECK(a.get("Ride") == u"Ride");
    CHECK(a.get("Shop") == u"Shop");
}

TEST_CASE("a key is found whatever the case of its letters when asked so")
{
    StringTable t;
    t.add("BigWheelName", u"Big wheel");
    CHECK(t.get("bigwheelname").empty());
    CHECK(t.get_ignoring_case("bigwheelNAME") == u"Big wheel");
    CHECK(t.get_ignoring_case("SmallWheelName").empty());
    StringTable more;
    more.add("CupName", u"Cup");
    t.merge(more);
    CHECK(t.get_ignoring_case("CUPNAME") == u"Cup");
}
