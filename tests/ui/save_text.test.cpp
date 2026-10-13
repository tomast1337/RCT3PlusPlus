// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/save_text.hpp"

#include <doctest/doctest.h>

using namespace rct3;
using namespace rct3::ui;

namespace
{
    StringTable strings()
    {
        StringTable s;
        s.add("ShortMonth0", u"Fir");
        s.add("ShortMonth3", u"Fou");
        s.add("Year", u"Season");
        s.add("ZeroGuests", u"nobody (#)");
        s.add("OneGuest", u"# visitor");
        s.add("ManyGuests", u"# visitors");
        return s;
    }
} // namespace

TEST_CASE("the clock shows twelve hours with am and pm, or twenty-four")
{
    CHECK(clock_text(555, true) == u"9:15 am");
    CHECK(clock_text(0, true) == u"12:00 am");
    CHECK(clock_text(720, true) == u"12:00 pm");
    CHECK(clock_text(1082, true) == u"6:02 pm");
    CHECK(clock_text(1082, false) == u"18:02");
    CHECK(clock_text(-1, false) == u"23:59");
}

TEST_CASE("the date leaves the day out when there is none")
{
    const StringTable s = strings();
    CHECK(date_text(s, 0, 0, 1) == u"Fir, Season 1");
    CHECK(date_text(s, 14, 3, 2) == u"14 Fou, Season 2");
}

TEST_CASE("the guests line picks its wording by count")
{
    const StringTable s = strings();
    CHECK(guests_text(s, 0) == u"nobody (0)");
    CHECK(guests_text(s, 1) == u"1 visitor");
    CHECK(guests_text(s, 228) == u"228 visitors");
}

TEST_CASE("a park's lines: name, date and time, guests; others show their size")
{
    const StringTable s = strings();
    save::SaveSummary park;
    park.park_name = "Caf\xC3\xA9 Park";
    park.month = 3;
    park.year = 2;
    park.day = 14;
    park.minutes = 1082;
    park.guests = 1;
    const auto lines = save_lines(s, park, true);
    CHECK(lines[0] == u"Café Park");
    CHECK(lines[1] == u"14 Fou, Season 2  6:02 pm");
    CHECK(lines[2] == u"1 visitor");

    park.kind = 1;
    const auto other = save_lines(s, park, true);
    CHECK(other[1].empty());
    CHECK(other[2] == u"3 x 2");
}
