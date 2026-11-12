// SPDX-License-Identifier: GPL-3.0-or-later
//
// How our code reaches translation keys, read from the source tree. The lang-audit tool reports it for every
// key, and a test uses it to guard the files. Only C++ under src/, app/ and tools/ counts: tests and scripts
// name keys to check them, not to show them. Comments are skipped.
//
// A key is reached by a string literal that equals it, or by a literal it is built from: a format string
// ("Scenario{}_Title"), each '{...}' standing for any text, or one ending in '_' ("SceneryItem_"), followed by
// any text. A pattern that starts with a hole ("{}Help") counts only where the key without its end is a
// literal in code. printf's number conversions ("%02d") stand for digits. kOwnKeyPrefix alone is not a
// pattern, so each of our keys must be named in full.
#pragma once

#include "core/maybe.hpp"
#include "ui/string_table.hpp"

#include <filesystem>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::ui
{

    struct SourcePlace
    {
        std::string file; ///< Relative to the root scanned, '/' separated
        int line = 0;
    };

    struct KeyPrefix
    {
        std::string text; ///< As a format string: "SceneryItem_" is kept as "SceneryItem_{}"
        SourcePlace place;
        std::string note; ///< The comment on its line, which usually says what follows it
    };

    struct CodeKeys
    {
        std::map<std::string, SourcePlace, std::less<>> literals; ///< Every string literal, at its first place
        std::vector<KeyPrefix> prefixes;
        /// Literals handed straight to a string table lookup (`strings.get("X")`, `text(strings, "X")`): keys
        /// for certain, so a file must have them.
        std::vector<std::pair<std::string, SourcePlace>> lookups;
    };

    /// The keys the sources and data tables under `root` (the repository) name.
    [[nodiscard]] CodeKeys scan_code(const std::filesystem::path& root);

    /// Literals in `code` that are keys for certain but `strings` lacks: our own keys (kOwnKeyPrefix) in full,
    /// and those handed straight to a lookup.
    [[nodiscard]] std::vector<std::pair<std::string, SourcePlace>> missing_keys(const CodeKeys& code, const StringTable& strings);

    /// The pattern in `code` that `key` fits, the longest when several do.
    [[nodiscard]] std::optional<const KeyPrefix&> prefix_of(const CodeKeys& code, std::string_view key);

} // namespace rct3::ui
