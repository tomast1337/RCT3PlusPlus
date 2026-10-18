// SPDX-License-Identifier: GPL-3.0-or-later
//
// Every string the interface shows, in our own words. The game's string tables are not read: the keys match
// theirs so their windows find text, and our own keys add text they never had. One file per language, named
// by its language and region code ("en-US.json", see LanguageInfo::tag), built into the program under
// lang/. Each is a JSON object of keys and strings; an object inside it is a group whose keys are joined to
// the group's name with '_':
//
//     { "Rct3pp": { "UsualKeys": "Usual keys", "Command": { "Copy": "Copy" } } }
//
// gives "Rct3pp_UsualKeys" and "Rct3pp_Command_Copy". Keys we add start with kOwnKeyPrefix, so they never
// meet the game's. A string may hold placeholders filled in when shown (see string_format.hpp). The game's
// copyright lines and credits are not here; they are read from the install (see about_window.hpp).
#pragma once

#include "ui/string_table.hpp"

#include <expected>
#include <string>
#include <string_view>

namespace rct3::ui
{

    inline constexpr std::string_view kOwnKeyPrefix = "Rct3pp_";
    /// The language every string has, used for keys a translation lacks.
    inline constexpr std::string_view kOwnBaseLanguage = "English";
    /// How deep groups may go.
    inline constexpr int kMaxTranslationDepth = 8;

    /// The strings in `text`. Fails when it is not a JSON object of strings and groups, saying where.
    [[nodiscard]] std::expected<StringTable, std::string> parse_translation(std::string_view text);

    /// The built-in path of `language`'s file, by its code: "English" gives "lang/en-US.json".
    [[nodiscard]] std::string translation_path(std::string_view language);

    /// Our strings in `language`, filled in from the base language where it lacks one. Empty when there are
    /// no files; a file that does not parse is skipped.
    [[nodiscard]] StringTable own_strings(std::string_view language);

} // namespace rct3::ui
