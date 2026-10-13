// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/key_map.hpp"

#include "config/json_text.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ui
{

    namespace
    {

        using C = Command;
        using K = Key;

        constexpr KeyCombo key(Key k, bool control = false, bool shift = false)
        {
            return { k, 0, control, shift };
        }
        constexpr KeyCombo control(char32_t c)
        {
            return { Key::Shortcut, c, true, false };
        }

        const std::array<CommandInfo, kCommandCount> kCommands = { {
            { C::Cancel, "cancel", u"Close, cancel, go back", CommandPress { K::Escape }, { key(K::Escape) } },
            { C::DefaultButton, "default_button", u"Default button", CommandPress { K::Enter }, { key(K::Enter) } },
            { C::NextControl, "next_control", u"Next control", CommandPress { K::Tab }, { key(K::Tab) } },
            { C::PreviousControl, "previous_control", u"Previous control", CommandPress { K::Tab, true }, { key(K::Tab, false, true) } },
            { C::NextPage, "next_page", u"Next page", CommandPress { K::Tab, false, true }, { key(K::Tab, true), key(K::PageDown, true) } },
            { C::PreviousPage, "previous_page", u"Previous page", CommandPress { K::Tab, true, true }, { key(K::Tab, true, true), key(K::PageUp, true) } },
            { C::Copy, "copy", u"Copy", CommandPress { K::Copy }, { control(U'c'), key(K::Insert, true) } },
            { C::Cut, "cut", u"Cut", CommandPress { K::Cut }, { control(U'x'), key(K::Delete, false, true) } },
            { C::Paste, "paste", u"Paste", CommandPress { K::Paste }, { control(U'v'), key(K::Insert, false, true) } },
            { C::SelectAll, "select_all", u"Select all text", CommandPress { K::Shortcut, false, true, U'a' }, { control(U'a') } },
            { C::DeveloperWindows, "developer_windows", u"Developer windows", std::nullopt, { key(K::F12) } },
        } };

        std::size_t index(Command c)
        {
            return static_cast<std::size_t>(c);
        }

        bool same(const CommandPress& c, const KeyPress& p)
        {
            return c.key == p.key && c.shift == p.shift && c.control == p.control && c.character == p.character;
        }

        /// Whether `press` is a command's own press or usual keys, which do nothing unless bound.
        bool reserved(const KeyPress& press)
        {
            const auto combo = key_combo(press);
            return std::ranges::any_of(kCommands, [&](const CommandInfo& info) {
                if (info.press && same(*info.press, press))
                    return true;
                return combo && std::ranges::any_of(info.usual, [&](const auto& u) { return u && *u == *combo; });
            });
        }

    } // namespace

    std::span<const CommandInfo> commands_table()
    {
        return kCommands;
    }

    const CommandInfo& command_info(Command command)
    {
        return kCommands.at(index(command));
    }

    KeyMap::KeyMap()
    {
        reset();
    }

    const std::vector<KeyCombo>& KeyMap::keys(Command command) const
    {
        return keys_.at(index(command));
    }

    void KeyMap::bind(Command command, KeyCombo combo)
    {
        set_keys(command, { combo });
    }

    void KeyMap::set_keys(Command command, std::vector<KeyCombo> combos)
    {
        for (const KeyCombo& c : combos)
        {
            for (auto& list : keys_)
                std::erase(list, c);
        }
        keys_.at(index(command)) = std::move(combos);
    }

    void KeyMap::reset()
    {
        for (const CommandInfo& info : kCommands)
        {
            auto& list = keys_.at(index(info.command));
            list.clear();
            for (const auto& u : info.usual)
            {
                if (u)
                    list.push_back(*u);
            }
        }
    }

    std::optional<Command> KeyMap::command_of(const KeyCombo& combo) const
    {
        for (std::size_t i = 0; i < keys_.size(); ++i)
        {
            if (std::ranges::find(keys_[i], combo) != keys_[i].end())
                return static_cast<Command>(i);
        }
        return std::nullopt;
    }

    KeyMap::Translated KeyMap::translate(const KeyboardState& keyboard) const
    {
        Translated out;
        out.keyboard.composition = keyboard.composition;
        for (const KeyPress& press : keyboard.presses)
        {
            const auto combo = key_combo(press);
            const auto command = combo ? command_of(*combo) : std::nullopt;
            if (command)
            {
                const CommandInfo& info = command_info(*command);
                if (info.press)
                    out.keyboard.presses.push_back({ info.press->key, info.press->shift, info.press->control, info.press->character });
                else
                    out.commands.push_back(*command);
            }
            else if (!reserved(press))
            {
                out.keyboard.presses.push_back(press);
            }
        }
        return out;
    }

    KeyMap KeyMap::parse(std::string_view text, std::vector<config::FileProblem>* problems)
    {
        KeyMap map;
        const auto json = config::parse_json_object(text, problems);
        if (!json)
            return map;
        for (const auto& [name, value] : json->items())
        {
            const auto info = std::ranges::find(kCommands, name, &CommandInfo::name);
            if (info == kCommands.end())
            {
                config::report(problems, "unknown command " + name);
                continue;
            }
            if (!value.is_array())
            {
                config::report(problems, name + ": expected a list of keys");
                continue;
            }
            std::vector<KeyCombo> combos;
            for (const auto& item : value)
            {
                const auto combo = item.is_string() ? parse_key_combo(item.get<std::string>()) : std::nullopt;
                if (combo)
                    combos.push_back(*combo);
                else
                    config::report(problems, name + ": not keys that can be bound: " + item.dump());
            }
            map.set_keys(info->command, std::move(combos));
        }
        return map;
    }

    std::string KeyMap::text() const
    {
        const KeyMap usual;
        config::Json json = config::Json::object();
        for (const CommandInfo& info : kCommands)
        {
            const auto& list = keys(info.command);
            if (list == usual.keys(info.command))
                continue;
            config::Json& names = json[info.name] = config::Json::array();
            for (const KeyCombo& combo : list)
                names.push_back(to_string(combo));
        }
        return config::json_text(json);
    }

    std::expected<KeyMap, std::string> KeyMap::load(const std::filesystem::path& path, std::vector<config::FileProblem>* problems)
    {
        auto text = config::read_text_file(path);
        if (!text)
            return std::unexpected(text.error());
        return *text ? parse(**text, problems) : KeyMap { };
    }

    std::expected<void, std::string> KeyMap::save(const std::filesystem::path& path) const
    {
        return config::write_text_file(path, text());
    }

} // namespace rct3::ui
