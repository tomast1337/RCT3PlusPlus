// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_combo.hpp"

#include <cctype>
#include <vector>

namespace rct3::ui
{

    namespace
    {

        constexpr int kFunctionKeys = 12;

        bool is_function(Key key)
        {
            return key >= Key::F1 && key <= Key::F12;
        }

        bool is_bindable_character(char32_t c)
        {
            return (c >= U'a' && c <= U'z') || (c >= U'0' && c <= U'9');
        }

        std::string lower(std::string_view s)
        {
            std::string out;
            for (const char c : s)
            {
                if (c != ' ')
                    out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            }
            return out;
        }

    } // namespace

    std::optional<KeyCombo> key_combo(const KeyPress& press)
    {
        if (is_function(press.key))
            return KeyCombo { press.key, 0, press.control, press.shift };
        if (press.key == Key::Shortcut && press.control && is_bindable_character(press.character))
            return KeyCombo { Key::Shortcut, press.character, true, press.shift };
        return std::nullopt;
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
        if (combo.key == Key::Shortcut)
        {
            const char32_t c = combo.character;
            out += static_cast<char>(c >= U'a' && c <= U'z' ? c - U'a' + U'A' : c);
        }
        else
        {
            out += "F" + std::to_string(static_cast<int>(combo.key) - static_cast<int>(Key::F1) + 1);
        }
        return out;
    }

    std::optional<KeyCombo> parse_key_combo(std::string_view text)
    {
        std::vector<std::string> parts;
        const std::string all = lower(text);
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
            else
                return std::nullopt;
        }
        const std::string& last = parts.back();
        if (last.size() == 1)
        {
            press.key = Key::Shortcut;
            press.character = static_cast<char32_t>(last[0]);
        }
        else if (last.size() <= 3 && last[0] == 'f' && std::isdigit(static_cast<unsigned char>(last[1])) != 0)
        {
            const int n = std::stoi(last.substr(1));
            if (n < 1 || n > kFunctionKeys || last[1] == '0')
                return std::nullopt;
            press.key = static_cast<Key>(static_cast<int>(Key::F1) + n - 1);
        }
        else
        {
            return std::nullopt;
        }
        return key_combo(press);
    }

} // namespace rct3::ui
