// SPDX-License-Identifier: GPL-3.0-or-later
//
// The languages the interface can be shown in. Each installed language is an overlay pair named after it
// in the install's gui folder, holding every interface string under the same keys as the others. The game
// recognises a fixed set of names; a pair with another name is not a language.
//
// Chinese, Japanese and Korean bring their own fonts, one set for each screen width (see font_sets.hpp),
// and break lines between characters rather than at spaces (see text_layout.hpp).
//
// Which one is shown: the one the user set, else the one the system's locale names, else English, else
// the first installed.
#pragma once

#include <array>
#include <filesystem>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    struct LanguageInfo
    {
        std::string_view name;   ///< As the overlay files are named, e.g. "German"
        std::string_view locale; ///< The locale prefix that picks it, e.g. "de" or "pt_BR"
        bool cjk = false;        ///< Brings its own fonts and breaks lines between characters
    };

    /// Every language the game knows by name, installed or not. Longer locale prefixes come first, so
    /// "zh_TW" is matched before "zh".
    inline constexpr std::array<LanguageInfo, 15> kLanguages = { {
        { "English", "en", false },
        { "Dutch", "nl", false },
        { "Finnish", "fi", false },
        { "French", "fr", false },
        { "Italian", "it", false },
        { "German", "de", false },
        { "Swedish", "sv", false },
        { "BrazilianPortuguese", "pt", false },
        { "Norwegian", "nb", false },
        { "Danish", "da", false },
        { "Spanish", "es", false },
        { "Chinese", "zh_TW", true },
        { "Korean", "ko", true },
        { "SChinese", "zh", true },
        { "Japanese", "ja", true },
    } };

    inline constexpr std::string_view kDefaultLanguage = "English";

    /// The language called `name`, ignoring case, or nullptr.
    [[nodiscard]] const LanguageInfo* find_language(std::string_view name);

    /// The languages among `file_names` (names in the gui folder, e.g. "German.common.ovl"), each once, in
    /// the order of kLanguages and with their canonical names.
    [[nodiscard]] std::vector<std::string> languages_in(std::span<const std::string> file_names);
    /// The languages installed in the gui folder at `gui`; none when it cannot be read.
    [[nodiscard]] std::vector<std::string> installed_languages(const std::filesystem::path& gui);

    /// The language a locale such as "de_DE.UTF-8" or "zh_TW" names, or an empty view. "C" and "POSIX"
    /// name none.
    [[nodiscard]] std::string_view language_for_locale(std::string_view locale);

    /// Of the `installed` languages: `wanted` when installed, else the one `locale` names, else English,
    /// else the first. Empty when none is installed.
    [[nodiscard]] std::string choose_language(std::span<const std::string> installed, std::string_view wanted, std::string_view locale);

    /// The overlay with the language's strings, relative to the install root.
    [[nodiscard]] std::string text_overlay(std::string_view language);

} // namespace rct3::ui
