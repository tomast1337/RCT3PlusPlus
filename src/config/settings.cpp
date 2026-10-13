// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/settings.hpp"

#include "config/flag.hpp"
#include "config/text_file.hpp"
#include "strings/ascii.hpp"

#include <charconv>

namespace rct3::config
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::array<SettingInfo, kSettingCount> kSettings = { {
            { Setting::GamePath, "game_path", "Folder the original game is installed in (the one that contains gui/ and Style/). RCT3PP_GAME_PATH overrides it.", "" },
            { Setting::SkipIntro, "skip_intro", "Start without the logos (yes/no). --no-intro skips them too.", "no" },
            { Setting::InterfaceScale, "interface_scale", "Pixels per interface unit: auto, or a whole number from 1 to 8. auto picks one from the window height.", "auto" },
            { Setting::Language, "language", "Interface language as the game names it (English, German, SChinese, ...). Unset: the locale's, else English. --language sets it too.", "" },
        } };

        // The table is indexed by Setting, so its order must follow the enum.
        static_assert([] {
            for (std::size_t i = 0; i < kSettings.size(); ++i)
            {
                if (static_cast<std::size_t>(kSettings[i].setting) != i)
                    return false;
            }
            return true;
        }());

        constexpr std::string_view kFileName = "settings.ini";
        constexpr int kMaxScale = 8;

        constexpr std::size_t index(Setting setting)
        {
            return static_cast<std::size_t>(setting);
        }

        constexpr bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\r';
        }

        std::string_view trimmed(std::string_view s)
        {
            while (!s.empty() && is_space(s.front()))
                s.remove_prefix(1);
            while (!s.empty() && is_space(s.back()))
                s.remove_suffix(1);
            return s;
        }

        const SettingInfo* find_setting(std::string_view name)
        {
            for (const SettingInfo& info : kSettings)
            {
                if (info.name == name)
                    return &info;
            }
            return nullptr;
        }

    } // namespace

    std::span<const SettingInfo> settings_table()
    {
        return kSettings;
    }

    std::string_view setting_name(Setting setting)
    {
        return index(setting) < kSettings.size() ? kSettings[index(setting)].name : std::string_view { };
    }

    Settings Settings::parse(std::string_view text, std::vector<SettingsProblem>* problems)
    {
        Settings settings;
        const auto report = [&](std::size_t line, std::string message) {
            if (problems != nullptr)
                problems->push_back({ line, std::move(message) });
        };
        std::size_t number = 0;
        while (!text.empty())
        {
            const std::size_t end = text.find('\n');
            const std::string_view raw = text.substr(0, end);
            text = end == std::string_view::npos ? std::string_view { } : text.substr(end + 1);
            ++number;

            const std::string_view line = trimmed(raw);
            if (line.empty() || line.front() == '#')
                continue;
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos)
            {
                report(number, "expected name = value");
                continue;
            }
            const std::string_view name = trimmed(line.substr(0, equals));
            const std::string_view value = trimmed(line.substr(equals + 1));
            if (const SettingInfo* info = find_setting(name))
            {
                settings.set(info->setting, value);
            }
            else
            {
                // Perhaps from a newer version: kept for writing back.
                settings.unknown_.emplace_back(line);
                report(number, "unknown setting " + std::string(name));
            }
        }
        return settings;
    }

    std::expected<Settings, std::string> Settings::load(const fs::path& path, std::vector<SettingsProblem>* problems)
    {
        auto text = read_text_file(path);
        if (!text)
            return std::unexpected(text.error());
        return *text ? parse(**text, problems) : Settings { };
    }

    std::optional<std::string_view> Settings::get(Setting setting) const
    {
        if (index(setting) >= kSettingCount)
            return std::nullopt;
        if (values_[index(setting)])
            return *values_[index(setting)];
        const std::string_view fallback = kSettings[index(setting)].default_value;
        if (fallback.empty())
            return std::nullopt;
        return fallback;
    }

    bool Settings::is_set(Setting setting) const
    {
        return index(setting) < kSettingCount && values_[index(setting)].has_value();
    }

    std::optional<fs::path> Settings::path(Setting setting) const
    {
        const auto value = get(setting);
        if (!value)
            return std::nullopt;
        return fs::path(*value);
    }

    bool Settings::flag(Setting setting) const
    {
        const auto value = get(setting);
        return value && flag_value(*value);
    }

    void Settings::set(Setting setting, std::string_view value)
    {
        if (index(setting) >= kSettingCount)
            return;
        if (value.empty())
            values_[index(setting)].reset();
        else
            values_[index(setting)] = std::string(value);
    }

    std::string Settings::text() const
    {
        std::string out = "# rct3++ settings. Lines are name = value; lines starting with # are ignored.\n";
        for (const SettingInfo& info : kSettings)
        {
            out += "\n# ";
            out += info.description;
            out += '\n';
            const auto& value = values_[index(info.setting)];
            if (!value)
                out += "# ";
            out += info.name;
            out += " =";
            const std::string_view shown = value ? std::string_view(*value) : info.default_value;
            if (!shown.empty())
            {
                out += ' ';
                out += shown;
            }
            out += '\n';
        }
        if (!unknown_.empty())
        {
            out += "\n# Not known to this version\n";
            for (const std::string& line : unknown_)
            {
                out += line;
                out += '\n';
            }
        }
        return out;
    }

    std::expected<void, std::string> Settings::save(const fs::path& path) const
    {
        return write_text_file(path, text());
    }

    std::optional<fs::path> config_file_path(const Environment& env, std::string_view name)
    {
        const auto settings = settings_path(env);
        if (!settings || !settings->has_parent_path())
            return std::nullopt;
        return settings->parent_path() / name;
    }

    std::optional<fs::path> settings_path(const Environment& env)
    {
        if (const auto file = env.path(EnvVar::SettingsFile))
            return *file;
#ifdef _WIN32
        if (const auto app_data = env.path(EnvVar::AppData))
            return *app_data / "rct3pp" / kFileName;
#endif
        // The XDG base directory rules ignore a relative XDG_CONFIG_HOME.
        if (const auto config = env.path(EnvVar::ConfigHome); config && config->is_absolute())
            return *config / "rct3pp" / kFileName;
        if (const auto home = env.path(EnvVar::Home))
            return *home / ".config" / "rct3pp" / kFileName;
        return std::nullopt;
    }

    std::expected<std::optional<float>, std::string> parse_interface_scale(std::string_view value)
    {
        value = trimmed(value);
        if (value.empty() || strings::equals_ignoring_case(value, "auto"))
            return std::optional<float> { };
        int scale = 0;
        const auto [end, ec] = std::from_chars(value.data(), value.data() + value.size(), scale);
        if (ec != std::errc { } || end != value.data() + value.size() || scale < 1 || scale > kMaxScale)
            return std::unexpected("interface scale must be auto or a whole number from 1 to " + std::to_string(kMaxScale) + ", not \"" + std::string(value) + "\"");
        return std::optional<float>(static_cast<float>(scale));
    }

} // namespace rct3::config
