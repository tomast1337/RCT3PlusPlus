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
            { C::Cancel, "cancel", "Rct3pp_Command_Cancel", CommandPress { K::Escape }, { key(K::Escape) } },
            { C::DefaultButton, "default_button", "Rct3pp_Command_DefaultButton", CommandPress { K::Enter }, { key(K::Enter) } },
            { C::NextControl, "next_control", "Rct3pp_Command_NextControl", CommandPress { K::Tab }, { key(K::Tab) } },
            { C::PreviousControl, "previous_control", "Rct3pp_Command_PreviousControl", CommandPress { K::Tab, true }, { key(K::Tab, false, true) } },
            { C::NextPage, "next_page", "Rct3pp_Command_NextPage", CommandPress { K::Tab, false, true }, { key(K::Tab, true), key(K::PageDown, true) } },
            { C::PreviousPage, "previous_page", "Rct3pp_Command_PreviousPage", CommandPress { K::Tab, true, true }, { key(K::Tab, true, true), key(K::PageUp, true) } },
            { C::Copy, "copy", "Rct3pp_Command_Copy", CommandPress { K::Copy }, { control(U'c'), key(K::Insert, true) } },
            { C::Cut, "cut", "Rct3pp_Command_Cut", CommandPress { K::Cut }, { control(U'x'), key(K::Delete, false, true) } },
            { C::Paste, "paste", "Rct3pp_Command_Paste", CommandPress { K::Paste }, { control(U'v'), key(K::Insert, false, true) } },
            { C::SelectAll, "select_all", "Rct3pp_Command_SelectAll", CommandPress { K::Shortcut, false, true, U'a' }, { control(U'a') } },
            { C::DeveloperWindows, "developer_windows", "Rct3pp_Command_DeveloperWindows", std::nullopt, { key(K::F12) } },
        } };

        using P = PlayCommand;
        using G = PlayKeyGroup;

        constexpr KeyCombo letter(char32_t c, bool control = false, bool shift = false)
        {
            return { Key::Letter, c, control, shift };
        }

        const std::array<PlayCommandInfo, kPlayCommandCount> kPlayCommands = { {
            { P::ExitGame, G::Universal, "exit_game", "KeySetting_Exit", key(K::Escape) },
            { P::ScreenSnapshot, G::Universal, "screen_snapshot", "KeySetting_TakeScreenSnapshot", key(K::F10) },
            { P::ScreenMovie, G::Universal, "screen_movie", "KeySetting_TakeScreenMovie", key(K::F11, true, true) },
            { P::NextCoasterCamType, G::DuringPlay, "next_coaster_cam_type", "KeySetting_RCNextType", letter(U'b') },
            { P::NextRideCamera, G::DuringPlay, "next_ride_camera", "KeySetting_RCNextCam", letter(U'n') },
            { P::PreviousRideCamera, G::DuringPlay, "previous_ride_camera", "KeySetting_RCPrevCam", letter(U'm') },
            { P::NextRide, G::DuringPlay, "next_ride", "KeySetting_RCNextRide", letter(U'n', false, true) },
            { P::PreviousRide, G::DuringPlay, "previous_ride", "KeySetting_RCPrevRide", letter(U'm', false, true) },
            { P::ToggleRideCamera, G::DuringPlay, "toggle_ride_camera", "KeySetting_ToggleRideCam", letter(U'c', true) },
            { P::ResetCamera, G::DuringPlay, "reset_camera", "KeySetting_CamReset", letter(U'r') },
            { P::MoveLeft, G::DuringPlay, "move_left", "KeySetting_SlideLeft", key(K::Left) },
            { P::MoveRight, G::DuringPlay, "move_right", "KeySetting_SlideRight", key(K::Right) },
            { P::MoveLeft2, G::DuringPlay, "move_left_2", "KeySetting_SlideLeft", letter(U'a') },
            { P::MoveRight2, G::DuringPlay, "move_right_2", "KeySetting_SlideRight", letter(U'd') },
            { P::MoveForwards, G::DuringPlay, "move_forwards", "KeySetting_SlideForwards", key(K::Up) },
            { P::MoveBackwards, G::DuringPlay, "move_backwards", "KeySetting_SlideBackwards", key(K::Down) },
            { P::MoveForwards2, G::DuringPlay, "move_forwards_2", "KeySetting_SlideForwards", letter(U'w') },
            { P::MoveBackwards2, G::DuringPlay, "move_backwards_2", "KeySetting_SlideBackwards", letter(U's') },
            { P::ZoomIn, G::DuringPlay, "zoom_in", "KeySetting_SlideUp", key(K::PageUp) },
            { P::ZoomOut, G::DuringPlay, "zoom_out", "KeySetting_SlideDown", key(K::PageDown) },
            { P::RotateLeft, G::DuringPlay, "rotate_left", "KeySetting_RotLeft", letter(U'q') },
            { P::RotateRight, G::DuringPlay, "rotate_right", "KeySetting_RotRight", letter(U'e') },
            { P::RotateUp, G::DuringPlay, "rotate_up", "KeySetting_RotUp", key(K::Home) },
            { P::RotateDown, G::DuringPlay, "rotate_down", "KeySetting_RotDown", key(K::End) },
            { P::TogglePointerLight, G::DuringPlay, "toggle_pointer_light", "KeySetting_TogglePointerLight", letter(U'l', false, true) },
            { P::RotateIsometric, G::DuringPlay, "rotate_isometric", "KeySetting_RotateIsoCam", key(K::Enter) },
            { P::ReverseIsometric, G::DuringPlay, "reverse_isometric", "KeySetting_RotateIsoCamReverser", key(K::Enter, false, true) },
            { P::FlattenTerrainSmooth, G::DuringPlay, "flatten_terrain_smooth", "KeySetting_FlattenTerrainSmooth", std::nullopt, u"Alt+Ctrl" },
            { P::FlattenTerrain, G::DuringPlay, "flatten_terrain", "KeySetting_FlattenTerrain", std::nullopt, u"Alt" },
            { P::TogglePause, G::DuringPlay, "toggle_pause", "KeySetting_TogglePaused", letter(U'p') },
            { P::CloseTopmostWindow, G::DuringPlay, "close_topmost_window", "KeySetting_CloseTopmostWindow", key(K::Backspace) },
        } };

        std::size_t index(Command c)
        {
            return static_cast<std::size_t>(c);
        }

        std::size_t index(PlayCommand c)
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

    std::span<const PlayCommandInfo> play_commands_table()
    {
        return kPlayCommands;
    }

    const PlayCommandInfo& play_command_info(PlayCommand command)
    {
        return kPlayCommands.at(index(command));
    }

    std::string_view play_key_group_key(PlayKeyGroup group)
    {
        return group == PlayKeyGroup::Universal ? "KeyGroup_Universal" : "KeyGroup_DuringPlay";
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

    const std::optional<KeyCombo>& KeyMap::play_key(PlayCommand command) const
    {
        return play_.at(index(command));
    }

    void KeyMap::bind_play(PlayCommand command, std::optional<KeyCombo> combo)
    {
        if (!play_command_info(command).fixed.empty())
            return;
        if (combo)
        {
            for (auto& other : play_)
            {
                if (other == combo)
                    other.reset();
            }
        }
        play_.at(index(command)) = combo;
    }

    void KeyMap::reset()
    {
        for (const PlayCommandInfo& info : kPlayCommands)
            play_.at(index(info.command)) = info.usual;
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
            if (const auto played = play_key_combo(press))
            {
                for (std::size_t i = 0; i < play_.size(); ++i)
                {
                    if (play_[i] == played)
                        out.play.push_back(static_cast<PlayCommand>(i));
                }
            }
            if (press.key == Key::Letter)
                continue; // Only for the game: the text it types comes on its own
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

    KeyMap KeyMap::parse(std::string_view text, std::optional<std::vector<config::FileProblem>&> problems)
    {
        KeyMap map;
        const auto json = config::parse_json_object(text, problems);
        if (!json)
            return map;
        for (const auto& [name, value] : json->items())
        {
            const auto info = std::ranges::find(kCommands, name, &CommandInfo::name);
            const auto play = std::ranges::find(kPlayCommands, name, &PlayCommandInfo::name);
            if (info == kCommands.end() && play == kPlayCommands.end())
            {
                config::report(problems, "unknown command " + name);
                continue;
            }
            if (!value.is_array())
            {
                config::report(problems, name + ": expected a list of keys");
                continue;
            }
            if (play != kPlayCommands.end())
            {
                const auto combo = value.size() == 1 && value[0].is_string() ? parse_play_key_combo(value[0].get<std::string>()) : std::nullopt;
                if (combo || value.empty())
                    map.bind_play(play->command, combo);
                else
                    config::report(problems, name + ": not one key that can be bound: " + value.dump());
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
        for (const PlayCommandInfo& info : kPlayCommands)
        {
            const auto& combo = play_key(info.command);
            if (combo == usual.play_key(info.command))
                continue;
            config::Json& names = json[info.name] = config::Json::array();
            if (combo)
                names.push_back(to_string(*combo));
        }
        return config::json_text(json);
    }

    std::expected<KeyMap, std::string> KeyMap::load(const std::filesystem::path& path, std::optional<std::vector<config::FileProblem>&> problems)
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
