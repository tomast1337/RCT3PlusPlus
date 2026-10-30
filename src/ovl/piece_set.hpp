// SPDX-License-Identifier: GPL-3.0-or-later
//
// Piece sets: path types, queue types, pool types and enclosure types. Each is a short header that
// names the pieces, followed by one scenery item structure per piece, stored back to back.
#pragma once

#include "ovl/overlay.hpp"
#include "ovl/scenery_item.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// A path type ("ptd"). Two layouts exist: the base game's, and one of the first expansion that has a
    /// longer name table. Both end with the piece items.
    struct PathType
    {
        /// Top byte 0 for the base layout and 1 for the longer one. The low byte is 1 or 3 (unconfirmed
        /// meaning).
        std::uint32_t header_word = 0;
        std::string_view name;      ///< The type's own name as stored in the structure
        std::string_view name_text; ///< "txt" item
        std::string_view icon;      ///< "gsi" item
        /// Piece names as stored: two ground names, then four consecutive slots per piece (the four are the
        /// same string in files seen). 78 slots in the base layout, 147 in the longer one, where unused slots
        /// are empty. Slots that are not string pointers are empty.
        std::vector<std::string_view> piece_names;
        /// Base layout only: research links of the type.
        std::vector<SceneryItemResearch> research;
        std::vector<SceneryItem> items; ///< 19 or 36 seen
    };

    struct PathTypeEntry
    {
        std::string_view name; ///< Symbol name without the tag
        std::optional<PathType> path;
    };

    /// A queue type ("qtd"): seven piece slots (the last two are the same piece in files seen).
    struct QueueType
    {
        std::string_view name;
        std::string_view name_text; ///< "txt" item
        std::string_view icon;      ///< "gsi" item
        std::string_view ground;    ///< "ftx" item used for the ground
        std::array<std::string_view, 7> piece_names;
        std::vector<SceneryItemResearch> research;
        std::vector<SceneryItem> items; ///< 5 or 6 seen
    };

    struct QueueTypeEntry
    {
        std::string_view name;
        std::optional<QueueType> queue;
    };

    /// A pool type ("pty"), first expansion.
    struct PoolType
    {
        std::string_view name;
        std::string_view name_text; ///< "txt" item
        std::string_view icon;      ///< "gsi" item
        /// Piece names as stored, 89 slots. A name can appear several times.
        std::vector<std::string_view> piece_names;
        /// The price list after the names, in hundredths: what a square costs to lay, and what taking it up
        /// again gives back (below zero). By index: 0 and 1 unknown, zero in the only file seen; 2 a water
        /// square, plus 3 for each unit of its depth; 4 and 5 the same when taken up; 6 a paved square (also
        /// a spa pool's, a ramp's and a high dive's), plus 7 per unit of depth; 8 and 9 their refund; 10 a
        /// square of pool path, 11 its refund; 12 a spa pool on top of its paved square, 13 its refund; 14 a
        /// ramp for each step it climbs, 15 its refund; 16 a high dive for each step, 17 its refund.
        std::array<std::int32_t, 18> prices { };
        std::array<float, 4> unknown_floats { }; ///< 0.1, 0.2, 1.0, 0.9 in the only file seen. Unconfirmed.
        std::vector<std::string_view> splines;   ///< 36 "spl" items, some repeated
        std::vector<SceneryItem> items;          ///< 58 seen
    };

    struct PoolTypeEntry
    {
        std::string_view name;
        std::optional<PoolType> pool;
    };

    /// An enclosure type ("enc"), second expansion: nine piece names (full, end, corner, edge, inner corner,
    /// gate and three viewing pieces in the order seen).
    struct EnclosureType
    {
        std::string_view name;
        std::string_view name_text; ///< "txt" item
        std::string_view icon;      ///< "gsi" item
        std::array<std::string_view, 9> piece_names;
        /// Four integers that look like two price and refund pairs. Unconfirmed.
        std::array<std::int32_t, 4> unknown_values { };
        std::array<std::string_view, 4> splines; ///< "spl" items
        std::array<float, 3> unknown_floats { }; ///< Unconfirmed.
        std::vector<SceneryItem> items;          ///< 9 seen
    };

    struct EnclosureTypeEntry
    {
        std::string_view name;
        std::optional<EnclosureType> enclosure;
    };

    /// Path types of one part of an overlay: pointer symbols tagged "ptd". Views point into the overlay.
    [[nodiscard]] std::vector<PathTypeEntry> read_path_types(const Overlay& overlay, const Part& part);
    /// Queue types: pointer symbols tagged "qtd".
    [[nodiscard]] std::vector<QueueTypeEntry> read_queue_types(const Overlay& overlay, const Part& part);
    /// Pool types: pointer symbols tagged "pty".
    [[nodiscard]] std::vector<PoolTypeEntry> read_pool_types(const Overlay& overlay, const Part& part);
    /// Enclosure types: pointer symbols tagged "enc".
    [[nodiscard]] std::vector<EnclosureTypeEntry> read_enclosure_types(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
