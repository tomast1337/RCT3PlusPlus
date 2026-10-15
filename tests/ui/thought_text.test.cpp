// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/thought_text.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("a thought's number names its texts, none for no thought")
{
    CHECK(thought_key(1) == "Thought_ImLost");
    CHECK(thought_key(0).empty());
    CHECK(thought_key(-3).empty());
    CHECK(thought_key(256).empty());
}

TEST_CASE("the random number picks one of a thought's texts and the name goes in")
{
    StringTable s;
    s.add("Thought_ImLost1", u"Where is %NAME%?");
    s.add("Thought_ImLost2", u"Lost near %NAME%, again near %NAME%");
    s.add("Thought_ImLost3", u"Lost");
    CHECK(thought_text(s, 1, 0, u"Park") == u"Where is Park?");
    CHECK(thought_text(s, 1, 4, u"Park") == u"Lost near Park, again near Park");
    CHECK(thought_text(s, 1, 99998, u"Park") == u"Lost");
    // A name holding the mark is put in once.
    CHECK(thought_text(s, 1, 0, u"%NAME%") == u"Where is %NAME%?");
    CHECK(thought_text(s, 2, 0, u"Park").empty());
}
