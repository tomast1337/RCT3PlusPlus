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
        GamePath,   ///< RCT3PP_GAME_PATH: root folder of the installed original game
        Validation, ///< RCT3PP_VALIDATION: enable the graphics API's validation layer

        Count_, ///< Number of variables; not a variable
    };

    inline constexpr std::size_t kEnvVarCount = static_cast<std::size_t>(EnvVar::Count_);

    struct EnvVarInfo
    {
        EnvVar var;
        std::string_view name;
        std::string_view description;
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
