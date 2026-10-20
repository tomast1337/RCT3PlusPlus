// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/language.hpp"

#include "core/maybe.hpp"

#include <algorithm>
#include <system_error>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kTextSuffix = ".common.ovl";

        char lower(char c)
        {
            return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
        }

        bool same_ignoring_case(std::string_view a, std::string_view b)
        {
            return std::ranges::equal(a, b, [](char x, char y) { return lower(x) == lower(y); });
        }

        bool starts_ignoring_case(std::string_view text, std::string_view prefix)
        {
            return text.size() >= prefix.size() && same_ignoring_case(text.substr(0, prefix.size()), prefix);
        }

    } // namespace

    std::optional<const LanguageInfo&> find_language(std::string_view name)
    {
        const auto it = std::ranges::find_if(kLanguages, [&](const LanguageInfo& l) { return same_ignoring_case(l.name, name); });
        return it != kLanguages.end() ? maybe(&*it) : std::nullopt;
    }

    std::vector<std::string> languages_in(std::span<const std::string> file_names)
    {
        std::vector<std::string> found;
        for (const LanguageInfo& l : kLanguages)
        {
            const bool present = std::ranges::any_of(file_names, [&](const std::string& f) { return f.size() == l.name.size() + kTextSuffix.size() && same_ignoring_case(f, std::string(l.name) + std::string(kTextSuffix)); });
            if (present)
                found.emplace_back(l.name);
        }
        return found;
    }

    std::vector<std::string> installed_languages(const std::filesystem::path& gui)
    {
        std::vector<std::string> names;
        std::error_code ec;
        for (const auto& entry : std::filesystem::directory_iterator(gui, ec))
        {
            if (entry.is_regular_file(ec))
                names.push_back(entry.path().filename().string());
        }
        return languages_in(names);
    }

    std::string_view language_for_locale(std::string_view locale)
    {
        // "de_DE.UTF-8@euro": only the language and territory count.
        locale = locale.substr(0, std::min(locale.find('.'), locale.find('@')));
        if (locale.empty() || locale == "C" || locale == "POSIX")
            return { };
        const LanguageInfo* best = nullptr;
        for (const LanguageInfo& l : kLanguages)
        {
            // A prefix matches the whole language part, or the language and territory.
            const bool whole = same_ignoring_case(locale, l.locale);
            const bool prefix = locale.size() > l.locale.size() && starts_ignoring_case(locale, l.locale) && (locale[l.locale.size()] == '_' || locale[l.locale.size()] == '-');
            if ((whole || prefix) && (best == nullptr || l.locale.size() > best->locale.size()))
                best = &l;
        }
        // Norwegian locales come as "nb", "nn" or the older "no".
        if (best == nullptr && (starts_ignoring_case(locale, "nn") || starts_ignoring_case(locale, "no")))
            return "Norwegian";
        return best != nullptr ? best->name : std::string_view { };
    }

    std::string choose_language(std::span<const std::string> installed, std::string_view wanted, std::string_view locale)
    {
        const auto installed_as = [&](std::string_view name) -> const std::string* {
            const auto it = std::ranges::find_if(installed, [&](const std::string& l) { return !name.empty() && same_ignoring_case(l, name); });
            return it != installed.end() ? &*it : nullptr;
        };
        for (const std::string_view candidate : { wanted, language_for_locale(locale), kDefaultLanguage })
        {
            if (const std::string* l = installed_as(candidate))
                return *l;
        }
        return installed.empty() ? std::string { } : installed.front();
    }

    std::string text_overlay(std::string_view language)
    {
        return "gui/" + std::string(language) + std::string(kTextSuffix);
    }

} // namespace rct3::ui
