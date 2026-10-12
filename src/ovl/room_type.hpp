// SPDX-License-Identifier: GPL-3.0-or-later
//
// Rooms that guests walk into: changing rooms and viewing galleries. Each is one small fixed structure
// that names its texts, icon and a spline.
#pragma once

#include "ovl/overlay.hpp"

#include <array>
#include <cstdint>
#include <optional>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// A changing room ("chg"), structure of 76 bytes. The structure starts with a pointer to one of its own
    /// later fields; that pointer is not exposed.
    struct ChangingRoom
    {
        std::string_view scenery_item;     ///< "sid" item
        std::string_view spline;           ///< "spl" item
        std::uint32_t unknown_0c = 0;      ///< 530 in every file seen. Unconfirmed.
        std::string_view name_text;        ///< "txt" item
        std::string_view description_text; ///< "txt" item, the same item as the name in files seen
        std::string_view icon;             ///< "gsi" item, empty in files seen
        std::uint32_t unknown_2c = 0;      ///< 4960 in every file seen. Unconfirmed.
        std::string_view second_spline;    ///< "spl" item, empty in files seen
        std::int32_t unknown_40 = 0;       ///< -1 in files seen. Unconfirmed.
        std::uint32_t unknown_44 = 0;      ///< 1 in files seen. Unconfirmed.
        std::uint32_t unknown_48 = 0;      ///< 0 in files seen. Unconfirmed.
        /// The seven words between the named fields (offsets 0x1c to 0x28 and 0x34 to 0x3c), zero in every
        /// file seen.
        std::array<std::uint32_t, 7> reserved { };
    };

    struct ChangingRoomEntry
    {
        std::string_view name; ///< Symbol name without the tag
        std::optional<ChangingRoom> room;
    };

    /// A viewing gallery ("vwg"), structure of 72 bytes, second expansion.
    struct ViewingGallery
    {
        std::uint32_t unknown_04 = 0;      ///< 788 in the file seen. Unconfirmed.
        std::string_view name_text;        ///< "txt" item
        std::string_view description_text; ///< "txt" item
        std::string_view icon;             ///< "gsi" item, empty in the file seen
        std::uint32_t unknown_24 = 0;      ///< 4960 in the file seen. Unconfirmed.
        std::string_view spline;           ///< "spl" item, empty in the file seen
        std::int32_t unknown_38 = 0;       ///< -1 in the file seen. Unconfirmed.
        std::uint32_t unknown_3c = 0;      ///< 2 in the file seen. Unconfirmed.
        /// The words between and after the named fields (0x14 to 0x20, 0x2c to 0x34, 0x40 and 0x44), zero in
        /// the file seen.
        std::array<std::uint32_t, 9> reserved { };
    };

    struct ViewingGalleryEntry
    {
        std::string_view name;
        std::optional<ViewingGallery> gallery;
    };

    /// Changing rooms of one part of an overlay: pointer symbols tagged "chg". Views point into the overlay.
    [[nodiscard]] std::vector<ChangingRoomEntry> read_changing_rooms(const Overlay& overlay, const Part& part);
    /// Viewing galleries: pointer symbols tagged "vwg".
    [[nodiscard]] std::vector<ViewingGalleryEntry> read_viewing_galleries(const Overlay& overlay, const Part& part);

} // namespace rct3::ovl
