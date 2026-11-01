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

TEST_CASE("each thought has a mood: glad ones happy, complaints sad, the rest neutral")
{
    CHECK(thought_mood(1) == Mood::Sad);      // Lost
    CHECK(thought_mood(26) == Mood::Happy);   // Good value
    CHECK(thought_mood(30) == Mood::Neutral); // The music is fine
    CHECK(thought_mood(0) == Mood::Neutral);
    CHECK(thought_mood(256) == Mood::Neutral);
}

TEST_CASE("a thought's text key has its mood; other keys have none")
{
    CHECK(key_mood("Thought_ImLost2") == Mood::Sad);
    CHECK(key_mood("Thought_PI_Stall_TooExpensive1") == Mood::Sad);
    CHECK(key_mood("Thought_PI_Attraction_Evaluated1") == Mood::Neutral);
    CHECK(key_mood("Thought_Themed1Good3") == Mood::Happy);
    CHECK_FALSE(key_mood("RideFixedMessage"));
    CHECK_FALSE(key_mood("Thought_None1"));
    CHECK_FALSE(key_mood("Thought_NoSuchThought1"));
}

TEST_CASE("each mood has its own face")
{
    CHECK(mood_icon(Mood::Happy) != mood_icon(Mood::Sad));
    CHECK(mood_icon(Mood::Neutral) != mood_icon(Mood::Sad));
    CHECK(mood_icon(Mood::Neutral) != mood_icon(Mood::Happy));
}
