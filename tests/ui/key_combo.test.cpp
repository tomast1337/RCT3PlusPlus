// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_combo.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("key combinations read and write the same way")
{
    for (const char* text : { "F1", "F12", "Shift+F3", "Ctrl+F5", "Ctrl+Shift+F10", "Ctrl+D", "Ctrl+Shift+D", "Ctrl+7", "Escape", "Enter", "Shift+Tab", "Ctrl+Page Down", "Ctrl+Insert", "Shift+Delete" })
    {
        const auto combo = parse_key_combo(text);
        REQUIRE_MESSAGE(combo, text);
        CHECK(to_string(*combo) == text);
    }
    CHECK(parse_key_combo(" control + shift + f12 ") == KeyCombo { Key::F12, 0, true, true });
    CHECK(parse_key_combo("ctrl+pgdn") == KeyCombo { Key::PageDown, 0, true, false });
    CHECK(parse_key_combo("Esc") == KeyCombo { Key::Escape });
}

TEST_CASE("only keys that do not type or move can be bound")
{
    for (const char* text : { "", "D", "Shift+D", "F0", "F13", "F01", "Alt+F4", "Ctrl+", "Ctrl+Home", "Ctrl+;", "Delete", "Backspace", "Space", "Left" })
        CHECK_MESSAGE(!parse_key_combo(text), text);
    CHECK_FALSE(key_combo({ Key::Home, false, true }));
    CHECK_FALSE(key_combo({ Key::Character, false, false, U'd' }));
}

TEST_CASE("a combination matches only its own modifiers")
{
    const KeyCombo f12 { Key::F12 };
    CHECK(matches(f12, { Key::F12 }));
    CHECK_FALSE(matches(f12, { Key::F12, true }));
    CHECK_FALSE(matches(f12, { Key::F11 }));
    const KeyCombo d { Key::Shortcut, U'd', true, false };
    CHECK(matches(d, { Key::Shortcut, false, true, U'd' }));
    CHECK_FALSE(matches(d, { Key::Shortcut, true, true, U'd' }));
}

TEST_CASE("game keys can be any key, shown as the game shows them")
{
    const auto letter = play_key_combo({ Key::Letter, true, false, U'n' });
    REQUIRE(letter);
    CHECK(key_text(*letter) == "Shift+N");
    CHECK(play_key_combo({ Key::Shortcut, false, true, U'c' }) == KeyCombo { Key::Letter, U'c', true });
    CHECK(play_key_combo({ Key::Left }) == KeyCombo { Key::Left });
    CHECK_FALSE(play_key_combo({ Key::Character, false, false, U'n' }));
    CHECK_FALSE(key_combo({ Key::Left }));
    CHECK_FALSE(key_combo({ Key::F5, false, false, 0, { }, true })); // Alt is for game keys

    const KeyCombo all { Key::F11, 0, true, true, true };
    CHECK(key_text(all) == "Alt+Shift+Ctrl+F11");
    CHECK(to_string(all) == "Ctrl+Shift+Alt+F11");
    CHECK(parse_play_key_combo(to_string(all)) == all);
    CHECK(key_text({ Key::Escape }) == "Esc");
    CHECK(parse_play_key_combo("left arrow") == KeyCombo { Key::Left });
    CHECK(parse_play_key_combo("Left") == KeyCombo { Key::Left });
    CHECK(parse_play_key_combo("B") == KeyCombo { Key::Letter, U'b' });
    CHECK_FALSE(parse_key_combo("B"));
}
