// SPDX-License-Identifier: GPL-3.0-or-later
//
// The interface's translated strings by key, read from a language overlay. A key that is missing reads as
// the empty string, as in the game; there is no fallback to another language per key.
#pragma once

#include <expected>
#include <filesystem>
#include <map>
#include <string>
#include <string_view>

namespace rct3::ui
{

    class StringTable
    {
    public:
        /// Reads every text item of the overlay at `path` (either file of the pair, or the stem).
        [[nodiscard]] static std::expected<StringTable, std::string> load(const std::filesystem::path& path);

        void add(std::string key, std::u16string value);
        /// Adds the strings of `other` whose keys this table does not have yet.
        void merge(const StringTable& other);

        /// The string for `key`, or an empty view when there is none.
        [[nodiscard]] std::u16string_view get(std::string_view key) const;
        [[nodiscard]] bool contains(std::string_view key) const;
        [[nodiscard]] std::size_t size() const
        {
            return strings_.size();
        }

    private:
        std::map<std::string, std::u16string, std::less<>> strings_;
    };

} // namespace rct3::ui
