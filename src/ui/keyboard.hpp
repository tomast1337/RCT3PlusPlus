// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keyboard input for the interface, read once per frame like the pointer: what was pressed and typed since
// the last update, in the order it happened, with the system's key repeat already applied. Keys go to the
// widget with the keyboard focus first, then up through its parents until one uses them (see Desktop).
#pragma once

#include <cstdint>
#include <vector>

namespace rct3::ui
{

    enum class Key : std::uint8_t
    {
        Left,
        Right,
        Up,
        Down,
        PageUp,
        PageDown,
        Home,
        End,
        Tab,
        Escape,
        Enter,
        Backspace,
        Delete,
        Space,     ///< Also arrives as a typed ' '
        Character, ///< Typed text: `character` holds it
        Shortcut,  ///< A letter or digit key with Control held: `character` holds it, lower case
    };

    struct KeyPress
    {
        Key key = Key::Escape;
        bool shift = false;
        bool control = false;
        char32_t character = 0; ///< For Character and Shortcut
    };

    struct KeyboardState
    {
        std::vector<KeyPress> presses; ///< Since the last update, oldest first
    };

} // namespace rct3::ui
