// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/translation_file.hpp"

#include "assets/embedded_files.hpp"
#include "config/json_text.hpp"
#include "strings/unicode.hpp"
#include "ui/language.hpp"

#include <format>

namespace rct3::ui
{

    namespace
    {

        std::expected<void, std::string> add_group(StringTable& table, const config::Json& group, const std::string& prefix, int depth)
        {
            if (depth > kMaxTranslationDepth)
                return std::unexpected(std::format("{}: groups nested too deep", prefix));
            for (const auto& [name, value] : group.items())
            {
                if (name.empty())
                    return std::unexpected(std::format("{}: empty name", prefix.empty() ? std::string("top") : prefix));
                const std::string key = prefix.empty() ? name : std::format("{}_{}", prefix, name);
                if (value.is_string())
                {
                    if (table.contains(key))
                        return std::unexpected(std::format("{}: given twice", key));
                    table.add(key, strings::to_utf16(value.get_ref<const std::string&>()));
                }
                else if (value.is_object())
                {
                    if (auto added = add_group(table, value, key, depth + 1); !added)
                        return added;
                }
                else
                    return std::unexpected(std::format("{}: expected a string or a group", key));
            }
            return { };
        }

        // Adds the strings in `text`. A key `table` already has fails: it is in another file, or twice in this one.
        std::expected<void, std::string> add_translation(StringTable& table, std::string_view text)
        {
            std::vector<config::FileProblem> problems;
            const auto json = config::parse_json_object(text, problems);
            if (!json)
                return std::unexpected(problems.empty() ? std::string("not JSON") : problems.front().message);
            return add_group(table, *json, { }, 0);
        }

    } // namespace

    std::expected<StringTable, std::string> parse_translation(std::string_view text)
    {
        StringTable table;
        if (auto added = add_translation(table, text); !added)
            return std::unexpected(added.error());
        return table;
    }

    std::string translation_path(std::string_view language)
    {
        const auto info = find_language(language);
        return std::format("lang/{}/", info ? info->tag : language);
    }

    std::expected<StringTable, std::string> load_translation(std::string_view language)
    {
        const std::string folder = translation_path(language);
        StringTable table;
        for (const assets::EmbeddedFile& file : assets::embedded_files())
        {
            if (!file.path.starts_with(folder) || !file.path.ends_with(".json"))
                continue;
            if (auto added = add_translation(table, assets::as_text(file.bytes)); !added)
                return std::unexpected(std::format("{}: {}", file.path, added.error()));
        }
        return table;
    }

    std::expected<StringTable, std::string> own_strings(std::string_view language)
    {
        auto table = load_translation(language);
        if (!table)
            return table;
        auto base = load_translation(kOwnBaseLanguage);
        if (!base)
            return base;
        table->merge(*base);
        return table;
    }

} // namespace rct3::ui
