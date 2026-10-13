// SPDX-License-Identifier: GPL-3.0-or-later
//
// A key with the modifiers held for it, as the user binds it to a command: a function key (F1 to F12) with
// or without Control and Shift, or a letter or digit with Control (and Shift if wanted). Other keys are not
// bound, so a binding never takes keys that type text or move around the interface.
//
// Written as the keys joined by '+', modifiers first: "F12", "Shift+F3", "Ctrl+Shift+D". Reading ignores
// case and spaces, and takes "Control" for "Ctrl".
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
        char32_t character = 0; ///< For Shortcut: the letter, lower case, or the digit
        bool control = false;
        bool shift = false;

        friend bool operator==(const KeyCombo&, const KeyCombo&) = default;
    };

    /// The combination `press` makes, if it is one that can be bound.
    [[nodiscard]] std::optional<KeyCombo> key_combo(const KeyPress& press);

    [[nodiscard]] bool matches(const KeyCombo& combo, const KeyPress& press);

    [[nodiscard]] std::string to_string(const KeyCombo& combo);

    /// Nothing when `text` is not a combination that can be bound.
    [[nodiscard]] std::optional<KeyCombo> parse_key_combo(std::string_view text);

} // namespace rct3::ui
