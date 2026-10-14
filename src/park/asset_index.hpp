// SPDX-License-Identifier: GPL-3.0-or-later
//
// Which overlay holds an asset. A park names each placed object's asset by symbol and by the overlay it was
// built from, but that overlay is only a hint: it is spelled with either kind of slash in any case, and
// some name files that do not exist, whose assets live in other files of the same folder. An asset's
// visuals, shapes and textures are named by symbol alone and often sit in other files nearby, and the same
// name can be used by unrelated files elsewhere. So the index keeps every overlay's symbols and, for a
// name in several files, picks the one nearest to where the asking file lives. Names are matched whatever
// their case: an item can ask for its visual spelled differently from how the visual's own file stores it.
//
// An overlay can also name other overlays it takes assets from, such as a shared folder of textures
// elsewhere in the game. The index remembers those names so the caller can find and add the files.
#pragma once

#include "strings/ascii.hpp"

#include <filesystem>
#include <map>
#include <optional>
#include <set>
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

        /// Indexes the overlay whose common file is `file`, below the root. False when it cannot be read.
        bool add_file(const std::filesystem::path& file);

        /// Overlays that indexed ones take assets from but that are not indexed themselves, as paths relative
        /// to the root in the form `add` takes, though perhaps not in the files' case.
        [[nodiscard]] std::vector<std::string> missing_references() const;

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
        std::map<std::string, std::vector<std::string>, strings::LessIgnoringCase> symbols_;
        std::map<std::string, std::string, std::less<>> overlays_; ///< By lower-case path
        std::set<std::string, std::less<>> references_;            ///< Lower-case paths named by indexed overlays
    };

    /// The overlay path that a reference written in the overlay at `from` names: relative to `from`'s folder,
    /// with either kind of slash, "." and ".." steps resolved. Nothing when it leaves the root or is empty.
    [[nodiscard]] std::optional<std::string> referenced_overlay(std::string_view from, std::string_view reference);

} // namespace rct3::park
