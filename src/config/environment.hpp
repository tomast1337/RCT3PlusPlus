// SPDX-License-Identifier: GPL-3.0-or-later
//
// Environment variables the program reads. Every variable is registered once in the table in
// environment.cpp (name, description), and code refers to it by its EnvVar id, never by a string.
//
// The process environment is read once, at startup, into an Environment value that is passed to whatever
// needs it. Tests build one from plain values instead, so nothing depends on the real environment.
//
// Adding a variable: add an EnvVar id, add its row to the table, then read it through an Environment.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <initializer_list>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>

namespace rct3::config
{

    enum class EnvVar : std::uint8_t
    {
        GamePath,     ///< RCT3PP_GAME_PATH: root folder of the installed original game
        Validation,   ///< RCT3PP_VALIDATION: enable the graphics API's validation layer
        FallbackFont, ///< RCT3PP_FALLBACK_FONT: TrueType font for characters the game fonts lack
        DataFolder,   ///< RCT3PP_DATA: the folder for the user's data, instead of the default one
        DataHome,     ///< XDG_DATA_HOME: the user's data folder
        Home,         ///< HOME: the user's home folder
        AppData,      ///< APPDATA: the user's application data folder on Windows
        LcAll,        ///< LC_ALL: the locale for everything, before LC_MESSAGES and LANG
        LcMessages,   ///< LC_MESSAGES: the locale for messages, before LANG
        Lang,         ///< LANG: the user's locale, e.g. de_DE.UTF-8

        Count_, ///< Number of variables; not a variable
    };

    inline constexpr std::size_t kEnvVarCount = static_cast<std::size_t>(EnvVar::Count_);

    struct EnvVarInfo
    {
        EnvVar var;
        std::string_view name;
        std::string_view description;
        bool standard = false; ///< Set by the system or other programs; ours all start with RCT3PP_
    };

    /// Every registered variable, in EnvVar order. For help output and documentation.
    [[nodiscard]] std::span<const EnvVarInfo> env_vars();

    [[nodiscard]] std::string_view env_name(EnvVar var);

    /// A snapshot of the registered variables. An empty value counts as unset.
    class Environment
    {
    public:
        /// No variable set.
        Environment() = default;

        /// Reads every registered variable from the process environment.
        [[nodiscard]] static Environment from_process();

        /// For tests and embedding: exactly the given values.
        [[nodiscard]] static Environment from_values(std::initializer_list<std::pair<EnvVar, std::string_view>> values);

        [[nodiscard]] std::optional<std::string_view> get(EnvVar var) const;

        [[nodiscard]] std::optional<std::filesystem::path> path(EnvVar var) const;

        /// True when set to anything but 0, false, no or off (any case).
        [[nodiscard]] bool flag(EnvVar var) const;

    private:
        std::array<std::optional<std::string>, kEnvVarCount> values_;
    };

} // namespace rct3::config
