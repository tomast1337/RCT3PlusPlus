// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/environment.hpp"

#include "config/flag.hpp"

#include <algorithm>
#include <cstdlib>

namespace rct3::config
{

    namespace
    {

        constexpr std::array<EnvVarInfo, kEnvVarCount> kEnvVars = { {
            { EnvVar::GamePath, "RCT3PP_GAME_PATH", "Root folder of the installed original game (the one that contains gui/ and Style/)" },
            { EnvVar::Validation, "RCT3PP_VALIDATION", "Enable the graphics API's validation layer (1/0)" },
            { EnvVar::FallbackFont, "RCT3PP_FALLBACK_FONT", "TrueType font for characters the game fonts lack (default: the one `./nob fetch` downloads)" },
            { EnvVar::DataFolder, "RCT3PP_DATA", "Folder for the user's configuration, screenshots and music, instead of the default one" },
            { EnvVar::DataHome, "XDG_DATA_HOME", "Holds rct3pp/, the user's data folder, on Linux and other free systems (default ~/.local/share)", true },
            { EnvVar::Home, "HOME", "The user's home folder, for the default data folder", true },
            { EnvVar::AppData, "APPDATA", "Holds rct3pp/, the user's data folder, on Windows", true },
            { EnvVar::LcAll, "LC_ALL", "The locale; picks the interface language when the settings do not", true },
            { EnvVar::LcMessages, "LC_MESSAGES", "The locale for messages, when LC_ALL is not set", true },
            { EnvVar::Lang, "LANG", "The locale, when neither LC_ALL nor LC_MESSAGES is set", true },
        } };

        // The table is indexed by EnvVar, so its order must follow the enum.
        static_assert([] {
            for (std::size_t i = 0; i < kEnvVars.size(); ++i)
            {
                if (static_cast<std::size_t>(kEnvVars[i].var) != i)
                    return false;
            }
            return true;
        }());

        constexpr std::size_t index(EnvVar var)
        {
            return static_cast<std::size_t>(var);
        }

    } // namespace

    std::span<const EnvVarInfo> env_vars()
    {
        return kEnvVars;
    }

    std::string_view env_name(EnvVar var)
    {
        return index(var) < kEnvVars.size() ? kEnvVars[index(var)].name : std::string_view { };
    }

    Environment Environment::from_process()
    {
        Environment env;
        for (const EnvVarInfo& info : kEnvVars)
        {
            const char* value = std::getenv(std::string(info.name).c_str());
            if (value != nullptr && *value != '\0')
                env.values_[index(info.var)] = value;
        }
        return env;
    }

    Environment Environment::from_values(std::initializer_list<std::pair<EnvVar, std::string_view>> values)
    {
        Environment env;
        for (const auto& [var, value] : values)
        {
            if (index(var) < kEnvVarCount && !value.empty())
                env.values_[index(var)] = std::string(value);
        }
        return env;
    }

    std::optional<std::string_view> Environment::get(EnvVar var) const
    {
        if (index(var) >= kEnvVarCount || !values_[index(var)])
            return std::nullopt;
        return *values_[index(var)];
    }

    std::optional<std::filesystem::path> Environment::path(EnvVar var) const
    {
        const auto value = get(var);
        if (!value)
            return std::nullopt;
        return std::filesystem::path(*value);
    }

    bool Environment::flag(EnvVar var) const
    {
        const auto value = get(var);
        return value && flag_value(*value);
    }

} // namespace rct3::config
