// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keyboard input for the interface, read once per frame like the pointer: the keys pressed since the last
// update, in order, with the system's key repeat already applied, and the text typed. Keys go to the widget
// with the keyboard focus first, then up through its parents until one uses them (see Desktop).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace rct3::ui
{

    /// The keys widgets react to. Printable characters arrive as text instead.
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
        Space, ///< Also arrives as text
    };

    struct KeyPress
    {
        Key key = Key::Escape;
        bool shift = false;
        bool control = false;
    };

    struct KeyboardState
    {
        std::vector<KeyPress> presses; ///< Since the last update, oldest first
        std::u16string text;           ///< Typed since the last update
    };

} // namespace rct3::ui
