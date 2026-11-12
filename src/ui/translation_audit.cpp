// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/translation_audit.hpp"

#include "ui/translation_file.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <format>
#include <fstream>
#include <iterator>

namespace rct3::ui
{

    namespace
    {

        // With the data tables of ours that name string keys, as the ride improvements do.
        constexpr std::array<std::string_view, 4> kFolders = { "src", "app", "tools", "assets/simulation" };
        constexpr std::array<std::string_view, 4> kExtensions = { ".cpp", ".hpp", ".h", ".json" };
        /// What comes right before a literal that is looked up as a key.
        constexpr std::array<std::string_view, 6> kLookups = { "strings.get(", "strings->get(", "strings_.get(", "get_ignoring_case(", "str(strings,", "text(strings," };

        bool ends_with_lookup(std::string_view before)
        {
            while (!before.empty() && std::isspace(static_cast<unsigned char>(before.back())))
                before.remove_suffix(1);
            return std::ranges::any_of(kLookups, [&](std::string_view l) { return before.ends_with(l); });
        }

        // Whether `key` fits `pattern`, each '{...}' in it standing for one character or more, "{:d}" for digits.
        bool fits(std::string_view pattern, std::string_view key)
        {
            std::size_t open = pattern.find('{');
            if (!key.starts_with(pattern.substr(0, open)))
                return false;
            if (open == std::string_view::npos)
                return key.size() == pattern.size();
            std::size_t at = open;
            while (true)
            {
                const std::size_t close = std::min(pattern.find('}', open), pattern.size() - 1);
                const bool digits = pattern.substr(open, close + 1 - open) == "{:d}";
                pattern.remove_prefix(close + 1);
                open = pattern.find('{');
                const std::string_view part = pattern.substr(0, open);
                std::size_t found = key.find(part, at + 1);
                if (open == std::string_view::npos)
                    found = key.size() >= at + 1 + part.size() && key.ends_with(part) ? key.size() - part.size() : std::string_view::npos;
                if (found == std::string_view::npos)
                    return false;
                if (digits && !std::ranges::all_of(key.substr(at, found - at), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; }))
                    return false;
                if (open == std::string_view::npos)
                    return true;
                at = found + part.size();
            }
        }

        // The literal's text as written: escapes are left alone, keys have none.
        void add_literal(CodeKeys& out, std::string_view text, std::string_view before, std::string_view rest_of_line, const SourcePlace& place)
        {
            std::string literal(text);
            // As a format string: printf's number conversions ("%02d") become digit holes, a trailing '_' gets a hole.
            std::string pattern;
            for (std::size_t i = 0; i < literal.size(); ++i)
            {
                std::size_t end = i + 1;
                while (literal[i] == '%' && end < literal.size() && (std::isdigit(static_cast<unsigned char>(literal[end])) || literal[end] == 'z' || literal[end] == 'l'))
                    ++end;
                if (literal[i] == '%' && end < literal.size() && (literal[end] == 'd' || literal[end] == 'u' || literal[end] == 'i'))
                {
                    pattern += "{:d}";
                    i = end;
                }
                else
                    pattern += literal[i];
            }
            if (!pattern.contains('{') && pattern.ends_with('_'))
                pattern += "{}";
            const std::string_view start = std::string_view(pattern).substr(0, pattern.find('{'));
            // One starting with a hole counts only when the rest has none: "{}Help" after a key in code.
            const std::string_view after = std::string_view(pattern).substr(std::min(pattern.find('}') + 1, pattern.size()));
            const bool usable = start.empty() ? pattern.starts_with("{}") && after.size() >= 2 && !after.contains('{') && std::ranges::any_of(after, [](char c) { return std::isalpha(static_cast<unsigned char>(c)) != 0; }) : start.size() >= 2 && std::isalpha(static_cast<unsigned char>(start.front())) && start != kOwnKeyPrefix;
            if (pattern.contains('{') && usable)
            {
                const std::size_t comment = rest_of_line.find("//");
                std::string_view note = comment == std::string_view::npos ? std::string_view { } : rest_of_line.substr(comment);
                while (!note.empty() && (note.front() == '/' || note.front() == '<' || note.front() == ' '))
                    note.remove_prefix(1);
                out.prefixes.push_back({ .text = pattern, .place = place, .note = std::string(note) });
            }
            if (ends_with_lookup(before))
                out.lookups.emplace_back(literal, place);
            out.literals.try_emplace(std::move(literal), place);
        }

        void scan_file(CodeKeys& out, std::string_view text, const std::string& file)
        {
            int line = 1;
            std::size_t line_start = 0;
            for (std::size_t i = 0; i < text.size(); ++i)
            {
                const char c = text[i];
                if (c == '\n')
                {
                    ++line;
                    line_start = i + 1;
                }
                else if (text.substr(i).starts_with("//"))
                    i = std::min(text.find('\n', i), text.size()) - 1;
                else if (text.substr(i).starts_with("/*"))
                {
                    const std::size_t end = std::min(text.find("*/", i + 2), text.size());
                    line += static_cast<int>(std::count(text.begin() + static_cast<std::ptrdiff_t>(i), text.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
                    line_start = text.rfind('\n', end) + 1;
                    i = end + 1;
                }
                else if (c == '\'' && !(i > 0 && std::isalnum(static_cast<unsigned char>(text[i - 1])))) // Not a digit separator
                {
                    for (++i; i < text.size() && text[i] != '\'' && text[i] != '\n'; ++i)
                        i += text[i] == '\\' ? 1 : 0;
                }
                else if (c == 'R' && text.substr(i).starts_with("R\"") && !(i > 0 && (std::isalnum(static_cast<unsigned char>(text[i - 1])) || text[i - 1] == '_')))
                {
                    // Raw strings hold no keys; skip to the end, counting lines.
                    const std::size_t open = std::min(text.find('(', i), text.size());
                    const std::string close = std::format("){}\"", text.substr(i + 2, open - i - 2));
                    const std::size_t end = std::min(text.find(close, open), text.size());
                    line += static_cast<int>(std::count(text.begin() + static_cast<std::ptrdiff_t>(i), text.begin() + static_cast<std::ptrdiff_t>(end), '\n'));
                    line_start = text.rfind('\n', end) + 1;
                    i = end + close.size() - 1;
                }
                else if (c == '"')
                {
                    std::size_t end = i + 1;
                    while (end < text.size() && text[end] != '"' && text[end] != '\n')
                        end += text[end] == '\\' ? 2 : 1;
                    const std::size_t line_end = std::min(text.find('\n', end), text.size());
                    add_literal(out, text.substr(i + 1, end - i - 1), text.substr(line_start, i - line_start), text.substr(std::min(end + 1, line_end), line_end - std::min(end + 1, line_end)), { .file = file, .line = line });
                    i = end;
                }
            }
        }

    } // namespace

    CodeKeys scan_code(const std::filesystem::path& root)
    {
        CodeKeys out;
        for (const std::string_view folder : kFolders)
        {
            std::error_code error;
            std::vector<std::filesystem::path> files;
            for (auto it = std::filesystem::recursive_directory_iterator(root / folder, error); !error && it != std::filesystem::recursive_directory_iterator(); it.increment(error))
            {
                if (it->is_regular_file() && std::ranges::contains(kExtensions, it->path().extension().string()))
                    files.push_back(it->path());
            }
            std::ranges::sort(files); // So a literal's first place does not depend on the file system
            for (const auto& path : files)
            {
                std::ifstream in(path, std::ios::binary);
                const std::string text((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
                scan_file(out, text, path.lexically_relative(root).generic_string());
            }
        }
        return out;
    }

    std::vector<std::pair<std::string, SourcePlace>> missing_keys(const CodeKeys& code, const StringTable& strings)
    {
        std::vector<std::pair<std::string, SourcePlace>> out;
        for (const auto& [literal, place] : code.literals)
        {
            const bool built = literal.ends_with('_') || literal.contains('{'); // A prefix, not a key
            if (literal.starts_with(kOwnKeyPrefix) && !built && !strings.contains(literal))
                out.emplace_back(literal, place);
        }
        for (const auto& [literal, place] : code.lookups)
        {
            if (!literal.starts_with(kOwnKeyPrefix) && !strings.contains(literal))
                out.emplace_back(literal, place);
        }
        return out;
    }

    std::optional<const KeyPrefix&> prefix_of(const CodeKeys& code, std::string_view key)
    {
        std::optional<const KeyPrefix&> best;
        for (const KeyPrefix& p : code.prefixes)
        {
            // A leading hole is filled by another literal: the key without the rest must be one.
            const std::string_view rest = std::string_view(p.text).substr(p.text.find('}') + 1);
            const bool fitting = p.text.starts_with('{') ? key.ends_with(rest) && key.size() > rest.size() && code.literals.contains(key.substr(0, key.size() - rest.size())) : fits(p.text, key);
            if (fitting && (!best || p.text.size() > best->text.size()))
                best = p;
        }
        return best;
    }

} // namespace rct3::ui
