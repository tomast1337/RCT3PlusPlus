// SPDX-License-Identifier: GPL-3.0-or-later
//
// The keys the user binds to the interface's commands: closing a window, the default button, moving between
// controls and pages, the clipboard, selecting all text and showing the developer windows. Each command has
// any number of key combinations (see key_combo.hpp), the game's usual ones to start with.
//
// Widgets know commands by the presses they arrive as: closing by Escape, the default button by Enter, the
// next control by Tab and so on. translate() turns the keys pressed into those presses, so the widgets read
// the user's keys without knowing them. A command's usual keys only work while bound to it: once closing is
// on F4, Escape no longer closes anything. Commands for the program rather than the interface, such as the
// developer windows, come out as a list instead.
//
// The arrow keys, Home, End, Backspace and Delete, and Control with the arrows to move by a word, are not
// commands: they always do what they do.
//
// The game's own commands, for the camera, pausing and the like, have a key each, bound as the game binds
// them: any key with any modifiers (see key_combo.hpp), letters and arrows too. They come out of translate()
// besides the presses, which still reach the interface, so typing in a box is not taken by them; whoever
// acts on them decides whether to while a box has the keys. Binding a game command takes its keys from any
// other game command, not from the interface's, and the other way round. A few of the game's keys are fixed
// and only shown.
//
// Kept in a file in the user's data folder (user_data.hpp): a JSON object with a list of keys for each
// command whose keys differ from the usual ones, written as in key_combo.hpp, e.g. "next_page": ["F6",
// "Ctrl+N"]. An empty list leaves a command without keys. Game commands are in the same object, with at most
// one key: "reset_camera": ["Shift+R"].
#pragma once

#include "config/text_file.hpp"
#include "ui/key_combo.hpp"
#include "ui/keyboard.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    enum class Command : std::uint8_t
    {
        Cancel, ///< Close a window or menu, cancel, go back
        DefaultButton,
        NextControl,
        PreviousControl,
        NextPage,
        PreviousPage,
        Copy,
        Cut,
        Paste,
        SelectAll,
        DeveloperWindows, ///< For the program: show or hide the developer windows

        Count_, ///< Number of commands; not a command
    };
    inline constexpr std::size_t kCommandCount = static_cast<std::size_t>(Command::Count_);

    /// The press a command arrives as.
    struct CommandPress
    {
        Key key = Key::Escape;
        bool shift = false;
        bool control = false;
        char32_t character = 0;
    };

    struct CommandInfo
    {
        Command command;
        std::string_view name;                        ///< In the file
        std::u16string_view label;                    ///< Shown to the user
        std::optional<CommandPress> press;            ///< Nothing for a command of the program
        std::array<std::optional<KeyCombo>, 2> usual; ///< The keys it starts with
    };

    [[nodiscard]] std::span<const CommandInfo> commands_table();
    [[nodiscard]] const CommandInfo& command_info(Command command);

    /// The game's commands, in the order the game lists them.
    enum class PlayCommand : std::uint8_t
    {
        ExitGame,
        ScreenSnapshot,
        ScreenMovie,
        NextCoasterCamType,
        NextRideCamera,
        PreviousRideCamera,
        NextRide,
        PreviousRide,
        ToggleRideCamera,
        ResetCamera,
        MoveLeft,
        MoveRight,
        MoveLeft2, ///< A second key for the same move
        MoveRight2,
        MoveForwards,
        MoveBackwards,
        MoveForwards2,
        MoveBackwards2,
        ZoomIn,
        ZoomOut,
        RotateLeft,
        RotateRight,
        RotateUp,
        RotateDown,
        TogglePointerLight,
        RotateIsometric,
        ReverseIsometric,
        FlattenTerrainSmooth,
        FlattenTerrain,
        TogglePause,
        CloseTopmostWindow,

        Count_, ///< Number of commands; not a command
    };
    inline constexpr std::size_t kPlayCommandCount = static_cast<std::size_t>(PlayCommand::Count_);

    enum class PlayKeyGroup : std::uint8_t
    {
        Universal,  ///< Anywhere in the game
        DuringPlay, ///< In a park
    };

    struct PlayCommandInfo
    {
        PlayCommand command;
        PlayKeyGroup group;
        std::string_view name;         ///< In the file
        std::string_view label_key;    ///< Its name in the string table
        std::optional<KeyCombo> usual; ///< The key it starts with
        std::u16string_view fixed { }; ///< Keys that cannot be changed, as shown; empty for keys the user sets
    };

    [[nodiscard]] std::span<const PlayCommandInfo> play_commands_table();
    [[nodiscard]] const PlayCommandInfo& play_command_info(PlayCommand command);
    /// The name of a group in the string table.
    [[nodiscard]] std::string_view play_key_group_key(PlayKeyGroup group);

    class KeyMap
    {
    public:
        /// Every command on its usual keys.
        KeyMap();

        [[nodiscard]] const std::vector<KeyCombo>& keys(Command command) const;
        /// Puts `command` on `combo` alone, taking `combo` from any other command.
        void bind(Command command, KeyCombo combo);
        void set_keys(Command command, std::vector<KeyCombo> combos);
        /// Back to the usual keys, the game's commands too.
        void reset();

        [[nodiscard]] const std::optional<KeyCombo>& play_key(PlayCommand command) const;
        /// Puts `command` on `combo`, taking it from any other game command; nothing leaves it without a key.
        /// Fixed keys are not changed.
        void bind_play(PlayCommand command, std::optional<KeyCombo> combo);
        /// The command `combo` is bound to.
        [[nodiscard]] std::optional<Command> command_of(const KeyCombo& combo) const;

        struct Translated
        {
            KeyboardState keyboard;        ///< For the interface; Paste presses still need the clipboard's text
            std::vector<Command> commands; ///< For the program, in the order pressed
            std::vector<PlayCommand> play; ///< The game's, in the order pressed
        };
        [[nodiscard]] Translated translate(const KeyboardState& keyboard) const;

        [[nodiscard]] static KeyMap parse(std::string_view text, std::vector<config::FileProblem>* problems = nullptr);
        [[nodiscard]] std::string text() const;
        /// A file that is not there gives the usual keys.
        [[nodiscard]] static std::expected<KeyMap, std::string> load(const std::filesystem::path& path, std::vector<config::FileProblem>* problems = nullptr);
        [[nodiscard]] std::expected<void, std::string> save(const std::filesystem::path& path) const;

        friend bool operator==(const KeyMap&, const KeyMap&) = default;

    private:
        std::array<std::vector<KeyCombo>, kCommandCount> keys_;
        std::array<std::optional<KeyCombo>, kPlayCommandCount> play_;
    };

} // namespace rct3::ui
