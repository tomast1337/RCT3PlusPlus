// SPDX-License-Identifier: GPL-3.0-or-later
//
// The user's settings: a small text file of `name = value` lines that the program reads at startup. Every
// setting is registered once in the table in settings.cpp (name, description, default), and code refers to
// it by its Setting id, never by a string. Lines starting with '#' are comments.
//
// What the user sets, in order of precedence: the command line, then an environment variable where one
// exists, then this file, then the default. The program only reads the file; `rct3pp --write-settings`
// writes it out with every setting and its description. Values already there and lines naming settings it
// does not know are kept; other comments are not.
//
// Adding a setting: add a Setting id, add its row to the table, then read it through a Settings value.
#pragma once

#include "config/environment.hpp"

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

namespace rct3::config
{

    enum class Setting : std::uint8_t
    {
        GamePath,       ///< game_path: root folder of the installed original game
        SkipIntro,      ///< skip_intro: start without the logos
        InterfaceScale, ///< interface_scale: auto, or how many pixels per interface unit
        Language,       ///< language: the interface language, by the game's name for it; unset picks one

        Count_, ///< Number of settings; not a setting
    };

    inline constexpr std::size_t kSettingCount = static_cast<std::size_t>(Setting::Count_);

    struct SettingInfo
    {
        Setting setting;
        std::string_view name;
        std::string_view description;
        std::string_view default_value; ///< Empty: unset
    };

    /// Every registered setting, in Setting order.
    [[nodiscard]] std::span<const SettingInfo> settings_table();

    [[nodiscard]] std::string_view setting_name(Setting setting);

    /// A line of the file that could not be used.
    struct SettingsProblem
    {
        std::size_t line = 0; ///< From 1
        std::string message;
    };

    class Settings
    {
    public:
        /// Every setting at its default.
        Settings() = default;

        /// Reads settings from file text. Bad lines are reported and skipped; the rest still apply.
        [[nodiscard]] static Settings parse(std::string_view text, std::vector<SettingsProblem>* problems = nullptr);

        /// Reads the file at `path`. A file that is not there gives the defaults; one that cannot be read is
        /// an error.
        [[nodiscard]] static std::expected<Settings, std::string> load(const std::filesystem::path& path, std::vector<SettingsProblem>* problems = nullptr);

        /// The value set in the file, or the default; nothing when neither gives one.
        [[nodiscard]] std::optional<std::string_view> get(Setting setting) const;
        /// Whether the file sets it, as opposed to the default applying.
        [[nodiscard]] bool is_set(Setting setting) const;
        [[nodiscard]] std::optional<std::filesystem::path> path(Setting setting) const;
        /// On for anything but 0, false, no or off (any case); off when unset.
        [[nodiscard]] bool flag(Setting setting) const;

        /// An empty value unsets it, so the default applies again.
        void set(Setting setting, std::string_view value);

        /// The file text: every setting with its description, commented out when it is at its default, then
        /// the lines this version did not know, unchanged.
        [[nodiscard]] std::string text() const;

        /// Writes text() to `path`, creating its folder.
        [[nodiscard]] std::expected<void, std::string> save(const std::filesystem::path& path) const;

    private:
        std::array<std::optional<std::string>, kSettingCount> values_;
        std::vector<std::string> unknown_; // Lines naming settings this version does not have
    };

    /// Where the settings file is: RCT3PP_SETTINGS, else the user's configuration folder (XDG_CONFIG_HOME,
    /// ~/.config, or APPDATA on Windows) + rct3pp/settings.ini. Nothing when no folder is known.
    [[nodiscard]] std::optional<std::filesystem::path> settings_path(const Environment& env);

    /// Another configuration file, `name`, in the settings file's folder.
    [[nodiscard]] std::optional<std::filesystem::path> config_file_path(const Environment& env, std::string_view name);

    /// The interface scale a value asks for: nothing for "auto" or empty (choose from the window size), else
    /// a whole number from 1 to 8. Anything else is an error.
    [[nodiscard]] std::expected<std::optional<float>, std::string> parse_interface_scale(std::string_view value);

} // namespace rct3::config
