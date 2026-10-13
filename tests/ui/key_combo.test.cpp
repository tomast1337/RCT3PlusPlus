// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_combo.hpp"

#include <doctest/doctest.h>

using namespace rct3::ui;

TEST_CASE("key combinations read and write the same way")
{
    for (const char* text : { "F1", "F12", "Shift+F3", "Ctrl+F5", "Ctrl+Shift+F10", "Ctrl+D", "Ctrl+Shift+D", "Ctrl+7" })
    {
        const auto combo = parse_key_combo(text);
        REQUIRE_MESSAGE(combo, text);
        CHECK(to_string(*combo) == text);
    }
    CHECK(parse_key_combo(" control + shift + f12 ") == KeyCombo { Key::F12, 0, true, true });
}

TEST_CASE("only keys that do not type or move can be bound")
{
    for (const char* text : { "", "D", "Shift+D", "F0", "F13", "F01", "Alt+F4", "Ctrl+", "Ctrl+Home", "Ctrl+;" })
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
