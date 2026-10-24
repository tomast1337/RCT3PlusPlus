// SPDX-License-Identifier: GPL-3.0-or-later
//
// Track sections: the pieces a tracked ride's track is built from (straights, curves, slopes, loops and
// so on). Each ties a piece to its scenery item and to the splines the cars follow along it.
#pragma once

#include "ovl/item_record.hpp"
#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// Names one end of a section offers to, needs from and forbids in the end it meets. Version 4 and 5 files
    /// only.
    struct JoinTags
    {
        std::vector<std::string_view> offers;
        std::vector<std::string_view> needs;
        std::vector<std::string_view> forbids;
    };

    struct TrackSection
    {
        std::string_view name; ///< Name string stored in the item, the same as the symbol name in all files
        /// "sid" item. Has the same name as the section in most items, and is empty or another name in the
        /// rest.
        std::string_view scenery_item;
        /// Two small numbers that go together: both 0, both 7, or a value of 1 to 6 with the other being the
        /// value three steps on (1 and 4, 2 and 5, 3 and 6, then back). Meaning unconfirmed.
        std::uint32_t join_a = 0;
        std::uint32_t join_b = 0;
        /// What the piece does on its ride: 1 brakes, 2 block brakes, 3 a station platform; other values name
        /// the piece within its own file.
        std::uint32_t role = 0;
        /// Bit sets. Version 1 files use small numbers, later files have 0x400000 set in nearly all items.
        /// Bits of the first: 0x1 the piece may start a track, 0x2000 laid backwards too; 0x2 it has a lift
        /// chain; 0x40 it is a station; 0x80 a station platform ends at it, whatever its neighbours; 0x4 a
        /// piece pattern never lays it backwards. Others unconfirmed.
        std::array<std::uint32_t, 2> flags { };
        /// "spl" items in file order. Named like `<Piece>L_spline` and `<Piece>R_spline`, so left, right, left,
        /// right. The second pair repeats the first in all but a few items.
        std::array<std::string_view, 4> splines;
        /// Further "spl" items, empty in most pieces.
        std::array<std::string_view, 2> extra_splines;
        /// "spl" items, left then right, named `<Piece>L_WaterSpline` and `<Piece>R_WaterSpline`: the edges of
        /// the water running down the piece, on slides and water rides. Empty on dry pieces.
        std::array<std::string_view, 2> water_splines;
        std::array<float, 3> scale { }; ///< 1, 1, 1 in nearly all items
        /// Names the start and the end of the piece join by: an end only meets an end of the same name,
        /// ignoring case, and an unnamed end only an unnamed one. Nearly all are unnamed; a tower's base
        /// starts "bottom" and its top ends "top", which no piece starts with. Version 4 and 5 files only.
        std::string_view start_join;
        std::string_view end_join;
        /// 1 at an end as wide as the wide water rides' channel, 0 for the rest. Version 4 and 5 files only.
        std::array<std::uint32_t, 2> widths { };
        /// Pieces of side by side tracks: 1 for the left track's, 2 for the right's, 0 for the rest. Ends
        /// only meet ends of the same side. Version 5 files only.
        std::uint32_t side = 0;
        /// Of side by side tracks: the section of the other half of a joined piece, laid with it on the same
        /// tiles on the other track; empty for a piece of one track alone. Version 5 files only.
        std::string_view partner;
        /// The group the piece is in, such as "station", for the ride's piece patterns. Version 4 and 5 files only.
        std::string_view group;
        /// Join tags of the start and of the end.
        std::array<JoinTags, 2> tags;
        /// Every reference in the record, in file order, including those above.
        std::vector<ItemReference> references;
        /// The whole record: 228 bytes in version 1 files, 364 in version 4 and 392 in version 5. Fields not
        /// named above are unconfirmed.
        std::span<const std::byte> record;
    };

    struct TrackSectionItem
    {
        std::string_view name;               ///< Symbol name without the tag
        std::optional<TrackSection> section; ///< Nothing when the record does not fit its block
    };

    /// Track sections of one part of an overlay: pointer symbols tagged "tks". Views in the result point
    /// into the overlay's bytes.
    [[nodiscard]] std::vector<TrackSectionItem> read_track_sections(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
