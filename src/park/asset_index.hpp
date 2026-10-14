// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which overlay holds an asset. A park names each placed object's asset by symbol and by the overlay it was
// built from, but that overlay is only a hint: it is spelled with either kind of slash in any case, and
// some name files that do not exist, whose assets live in other files of the same folder. An asset's
// visuals, shapes and textures are named by symbol alone and often sit in other files nearby, and the same
// name can be used by unrelated files elsewhere. So the index keeps every overlay's symbols and, for a
// name in several files, picks the one nearest to where the asking file lives.
#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace rct3::park
{

    class AssetIndex
    {
    public:
        /// Indexes every overlay below each of `folders` under `root`. Files that cannot be read are skipped.
        [[nodiscard]] static AssetIndex scan(const std::filesystem::path& root, std::span<const std::string_view> folders);

        /// Records that the overlay `path` (a path relative to the root, without ".common.ovl", slashes
        /// forward) holds `symbol`, the full stored name with its tag ("Bench:sid").
        void add(std::string_view symbol, std::string_view path);

        /// The overlay holding `symbol`, nearest to `near` (an overlay path in the same form) when several do.
        [[nodiscard]] std::optional<std::string> find(std::string_view symbol, std::string_view near) const;

        /// `hint` as an indexed overlay path: slashes made forward, case matched to the files. Nothing when
        /// no such overlay is indexed.
        [[nodiscard]] std::optional<std::string> overlay(std::string_view hint) const;

        /// The file of overlay `path` on disk.
        [[nodiscard]] std::filesystem::path file(std::string_view path) const;

        [[nodiscard]] std::size_t symbol_count() const
        {
            return symbols_.size();
        }

    private:
        std::filesystem::path root_;
        std::map<std::string, std::vector<std::string>, std::less<>> symbols_;
        std::map<std::string, std::string, std::less<>> overlays_; ///< By lower-case path
    };

} // namespace rct3::park
