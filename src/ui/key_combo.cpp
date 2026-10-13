// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_combo.hpp"

#include <array>
#include <cctype>
#include <vector>

namespace rct3::ui
{

    namespace
    {

        struct KeyName
        {
            Key key;
            std::string_view name;
            bool needs_modifier = false; ///< Only bound to the interface with Control or Shift held
            bool play_only = false;      ///< Only bound to the game
            std::string_view shown { };  ///< When not `name`
        };
        constexpr std::array<KeyName, 29> kKeyNames = { {
            { Key::Escape, "Escape", false, false, "Esc" },
            { Key::Enter, "Enter" },
            { Key::Tab, "Tab" },
            { Key::PageUp, "Page Up" },
            { Key::PageDown, "Page Down" },
            { Key::Insert, "Insert" },
            { Key::Delete, "Delete", true },
            { Key::Backspace, "Backspace", true },
            { Key::F1, "F1" },
            { Key::F2, "F2" },
            { Key::F3, "F3" },
            { Key::F4, "F4" },
            { Key::F5, "F5" },
            { Key::F6, "F6" },
            { Key::F7, "F7" },
            { Key::F8, "F8" },
            { Key::F9, "F9" },
            { Key::F10, "F10" },
            { Key::F11, "F11" },
            { Key::F12, "F12" },
            { Key::Left, "Left Arrow", false, true },
            { Key::Right, "Right Arrow", false, true },
            { Key::Up, "Up Arrow", false, true },
            { Key::Down, "Down Arrow", false, true },
            { Key::Home, "Home", false, true },
            { Key::End, "End", false, true },
            { Key::Space, "Space", false, true },
            { Key::Shortcut, "" },
            { Key::Letter, "", false, true },
        } };

        // Other names read for the same keys.
        struct Alias
        {
            std::string_view alias;
            Key key;
        };
        constexpr std::array<Alias, 8> kAliases = { {
            { "esc", Key::Escape },
            { "pgup", Key::PageUp },
            { "pgdn", Key::PageDown },
            { "ins", Key::Insert },
            { "left", Key::Left },
            { "right", Key::Right },
            { "up", Key::Up },
            { "down", Key::Down },
        } };

        const KeyName* find_key(Key key)
        {
            for (const KeyName& k : kKeyNames)
            {
                if (k.key == key)
                    return &k;
            }
            return nullptr;
        }

        bool is_bindable_character(char32_t c)
        {
            return (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9');
        }

        std::string key_name(const KeyCombo& combo, bool shown)
        {
            if (combo.key == Key::Shortcut || combo.key == Key::Letter)
            {
                const char32_t c = combo.character;
                return std::string(1, static_cast<char>(c >= U'a' && c <= U'z' ? c - U'a' + U'A' : c));
            }
            const KeyName* k = find_key(combo.key);
            if (k == nullptr)
                return { };
            return std::string(shown && !k->shown.empty() ? k->shown : k->name);
        }

        /// Lower case, without spaces.
        std::string folded(std::string_view s)
        {
            std::string out;
            for (const char c : s)
            {
                if (c != ' ')
                    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

        std::optional<KeyPress> parsed_press(std::string_view text)
        {
            std::vector<std::string> parts;
            const std::string all = folded(text);
            std::size_t start = 0;
            while (start <= all.size())
            {
                const std::size_t plus = all.find('+', start);
                const std::size_t end = plus == std::string::npos ? all.size() : plus;
                parts.push_back(all.substr(start, end - start));
                start = end + 1;
            }

            KeyPress press;
            for (std::size_t i = 0; i + 1 < parts.size(); ++i)
            {
                if (parts[i] == "ctrl" || parts[i] == "control")
                    press.control = true;
                else if (parts[i] == "shift")
                    press.shift = true;
                else if (parts[i] == "alt")
                    press.alt = true;
                else
                    return std::nullopt;
            }
            const std::string& last = parts.back();
            if (last.size() == 1)
            {
                press.key = Key::Shortcut;
                press.character = static_cast<char32_t>(last[0]);
                return press;
            }
            for (const KeyName& k : kKeyNames)
            {
                if (!k.name.empty() && folded(k.name) == last)
                {
                    press.key = k.key;
                    return press;
                }
            }
            for (const Alias& a : kAliases)
            {
                if (a.alias == last)
                {
                    press.key = a.key;
                    return press;
                }
            }
            return std::nullopt;
        }
    } // namespace

    std::optional<KeyCombo> key_combo(const KeyPress& press)
    {
        if (press.alt)
            return std::nullopt;
        if (press.key == Key::Shortcut)
        {
            if (press.control && is_bindable_character(press.character))
                return KeyCombo { Key::Shortcut, press.character, true, press.shift };
            return std::nullopt;
        }
        const KeyName* k = find_key(press.key);
        if (k == nullptr || k->play_only || (k->needs_modifier && !press.control && !press.shift))
            return std::nullopt;
        return KeyCombo { press.key, 0, press.control, press.shift };
    }

    std::optional<KeyCombo> play_key_combo(const KeyPress& press)
    {
        if (press.key == Key::Shortcut || press.key == Key::Letter)
        {
            if (is_bindable_character(press.character))
                return KeyCombo { Key::Letter, press.character, press.control, press.shift, press.alt };
            return std::nullopt;
        }
        if (find_key(press.key) == nullptr)
            return std::nullopt;
        return KeyCombo { press.key, 0, press.control, press.shift, press.alt };
    }

    bool play_matches(const KeyCombo& combo, const KeyPress& press)
    {
        const auto made = play_key_combo(press);
        return made && *made == combo;
    }

    bool matches(const KeyCombo& combo, const KeyPress& press)
    {
        const auto made = key_combo(press);
        return made && *made == combo;
    }

    std::string to_string(const KeyCombo& combo)
    {
        std::string out;
        if (combo.control)
            out += "Ctrl+";
        if (combo.shift)
            out += "Shift+";
        if (combo.alt)
            out += "Alt+";
        return out + key_name(combo, false);
    }

    std::string key_text(const KeyCombo& combo)
    {
        std::string out;
        if (combo.alt)
            out += "Alt+";
        if (combo.shift)
            out += "Shift+";
        if (combo.control)
            out += "Ctrl+";
        return out + key_name(combo, true);
    }

    std::optional<KeyCombo> parse_key_combo(std::string_view text)
    {
        const auto press = parsed_press(text);
        return press ? key_combo(*press) : std::nullopt;
    }

    std::optional<KeyCombo> parse_play_key_combo(std::string_view text)
    {
        const auto press = parsed_press(text);
        return press ? play_key_combo(*press) : std::nullopt;
    }

} // namespace rct3::ui
