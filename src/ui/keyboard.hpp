// SPDX-License-Identifier: GPL-3.0-or-later
//
// Keyboard input for the interface, read once per frame like the pointer: what was pressed and typed since
// the last update, in the order it happened, with the system's key repeat already applied. Keys go to the
// widget with the keyboard focus first, then up through its parents until one uses them (see Desktop).
//
// The clipboard keys arrive as their own presses, whatever keys made them, and a paste carries the
// clipboard's text with it. Text copied the other way comes out of Desktop::take_clipboard().
//
// An input method, as used to type Chinese or Japanese, composes text over several key presses before the
// user confirms it. What it is composing is part of the state (KeyboardState::composition) and is shown at
// the caret of the focused widget; once confirmed, it arrives as typed Character presses like any other
// text. The platform fills the composition when its window system reports one.
#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
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
        Copy,      ///< Control+C or Control+Insert
        Cut,       ///< Control+X or Shift+Delete
        Paste,     ///< Control+V or Shift+Insert: `text` holds what the clipboard had
        F1,        ///< The function keys, F1 to F12 in order
        F2,
        F3,
        F4,
        F5,
        F6,
        F7,
        F8,
        F9,
        F10,
        F11,
        F12,
    };

    struct KeyPress
    {
        Key key = Key::Escape;
        bool shift = false;
        bool control = false;
        char32_t character = 0;  ///< For Character and Shortcut
        std::u16string text { }; ///< For Paste
    };

    /// Text an input method is composing, not yet typed.
    struct Composition
    {
        std::u16string text { }; ///< Empty when nothing is being composed
        std::size_t caret = 0;   ///< Where in `text` the input method's cursor is, in UTF-16 units

        friend bool operator==(const Composition&, const Composition&) = default;
    };

    struct KeyboardState
    {
        std::vector<KeyPress> presses; ///< Since the last update, oldest first
        Composition composition;       ///< As it stands now
    };

} // namespace rct3::ui
