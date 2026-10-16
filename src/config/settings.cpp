// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/settings.hpp"

#include "config/flag.hpp"
#include "config/json_text.hpp"
#include "strings/ascii.hpp"

#include <charconv>

namespace rct3::config
{

    namespace
    {

        namespace fs = std::filesystem;

        constexpr std::array<SettingInfo, kSettingCount> kSettings = { {
            { Setting::GamePath, "game_path", SettingKind::Text, "Folder the original game is installed in (the one that contains gui/ and Style/). RCT3PP_GAME_PATH overrides it.", "" },
            { Setting::SkipIntro, "skip_intro", SettingKind::Flag, "Start without the logos (true/false). --no-intro skips them too.", "no" },
            { Setting::InterfaceScale, "interface_scale", SettingKind::Scale, "Pixels per interface unit: auto, or a whole number from 1 to 8. auto picks one from the window height.", "auto" },
            { Setting::Language, "language", SettingKind::Text, "Interface language as the game names it (English, German, SChinese, ...). Unset: the locale's, else English. --language sets it too.", "" },
            { Setting::NoAudio, "no_audio", SettingKind::Flag, "Start without any sound (true/false). --no-audio sets it, --audio overrides it for one run.", "no" },
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

    Settings Settings::parse(std::string_view text, std::vector<FileProblem>* problems)
    {
        Settings settings;
        const auto json = parse_json_object(text, problems);
        if (!json)
            return settings;
        for (const auto& [name, value] : json->items())
        {
            const SettingInfo* info = find_setting(name);
            if (info == nullptr)
            {
                // Perhaps from a newer version: kept for writing back.
                settings.unknown_.emplace_back(name, value.dump());
                report(problems, "unknown setting " + name);
                continue;
            }
            if (value.is_null())
                settings.set(info->setting, "");
            else if (value.is_string())
                settings.set(info->setting, value.get<std::string>());
            else if (info->kind == SettingKind::Flag && value.is_boolean())
                settings.set(info->setting, value.get<bool>() ? "yes" : "no");
            else if (info->kind == SettingKind::Scale && value.is_number_integer())
                settings.set(info->setting, std::to_string(value.get<long long>()));
            else
                report(problems, name + ": " + (info->kind == SettingKind::Flag ? "expected true or false" : info->kind == SettingKind::Scale ? "expected \"auto\" or a whole number" : "expected text"));
        }
        return settings;
    }

    std::expected<Settings, std::string> Settings::load(const fs::path& path, std::vector<FileProblem>* problems)
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
        Json json = Json::object();
        for (const SettingInfo& info : kSettings)
        {
            const auto& value = values_[index(info.setting)];
            Json& out = json[info.name];
            if (!value)
                continue; // null
            int scale = 0;
            const auto [end, ec] = std::from_chars(value->data(), value->data() + value->size(), scale);
            if (info.kind == SettingKind::Flag)
                out = flag_value(*value);
            else if (info.kind == SettingKind::Scale && ec == std::errc { } && end == value->data() + value->size())
                out = scale;
            else
                out = *value;
        }
        for (const auto& [name, value] : unknown_)
            json[name] = Json::parse(value);
        return json_text(json);
    }

    std::expected<void, std::string> Settings::save(const fs::path& path) const
    {
        return write_text_file(path, text());
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
