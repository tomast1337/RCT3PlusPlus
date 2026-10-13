// SPDX-License-Identifier: GPL-3.0-or-later
//
// A key with the modifiers held for it, as the user binds it to a command. These can be bound:
//
// - the function keys (F1 to F12), Escape, Enter, Tab, Page Up, Page Down and Insert, with or without
//   Control and Shift;
// - Delete and Backspace with Control or Shift;
// - a letter or digit with Control, and Shift if wanted.
//
// Other keys are not bound to the interface, so its bindings never take the keys that type text or move the
// caret. The game's own keys, for moving the camera and the like, can be any key with any of Control, Shift
// and Alt: letters, digits, the arrows, Home, End and Space too (play_key_combo()).
//
// Written as the keys joined by '+', modifiers first: "F12", "Shift+Tab", "Ctrl+Page Down", "Ctrl+Shift+D",
// "Alt+Left Arrow". Reading ignores case and spaces, and takes "Control" for "Ctrl", "Esc" for "Escape",
// "PgUp" and "PgDn" for the page keys and "Left" for "Left Arrow". Shown to the user as the game shows keys:
// Alt, Shift, then Ctrl, and "Esc" for Escape (key_text()).
#pragma once

#include "ui/keyboard.hpp"

#include <optional>
#include <string>
#include <string_view>

namespace rct3::ui
{

    struct KeyCombo
    {
        Key key = Key::F12;
        char32_t character = 0; ///< For Shortcut and Letter: the letter, lower case, or the digit
        bool control = false;
        bool shift = false;
        bool alt = false; ///< Game keys only

        friend bool operator==(const KeyCombo&, const KeyCombo&) = default;
    };

    /// The combination `press` makes, if it is one that can be bound.
    [[nodiscard]] std::optional<KeyCombo> key_combo(const KeyPress& press);

    [[nodiscard]] bool matches(const KeyCombo& combo, const KeyPress& press);

    /// The combination `press` makes for a game key: any key but typed text and the clipboard commands. A
    /// letter or digit is a Letter whether Control is held or not.
    [[nodiscard]] std::optional<KeyCombo> play_key_combo(const KeyPress& press);

    [[nodiscard]] bool play_matches(const KeyCombo& combo, const KeyPress& press);

    /// As written in files.
    [[nodiscard]] std::string to_string(const KeyCombo& combo);

    /// As shown to the user.
    [[nodiscard]] std::string key_text(const KeyCombo& combo);

    /// Nothing when `text` is not a combination that can be bound.
    [[nodiscard]] std::optional<KeyCombo> parse_key_combo(std::string_view text);
    /// The same for a game key.
    [[nodiscard]] std::optional<KeyCombo> parse_play_key_combo(std::string_view text);

} // namespace rct3::ui
