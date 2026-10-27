// SPDX-License-Identifier: GPL-3.0-or-later
//
// The block of fields that tracked rides, animated rides, stalls and special attractions start with: what
// kind of attraction it is and how the park builder's list shows it. Internal to the item readers.
#pragma once

#include "ovl/item_record.hpp"

#include <cstdint>
#include <optional>
#include <string_view>

namespace rct3::ovl
{

    struct AttractionInfo
    {
        /// Kind of attraction within its item type. Files store it directly in version 1 and with an offset of
        /// 512 (version 4) or 768 (version 5) later, which is removed here.
        std::uint32_t type_id = 0;
        std::string_view name_text;        ///< "txt" item: name shown in the park builder
        std::string_view description_text; ///< "txt" item: description shown there
        std::string_view icon;             ///< "gsi" item: icon of the entry
        std::string_view style;            ///< "sid" item the attraction is placed as
        std::string_view spline;           ///< "spl" item
        /// Two values stored as 10000 and -7500 in most items, and other pairs (580 and -522, 30000 and
        /// -30000) in some. Meaning unconfirmed.
        std::int32_t value_a = 0;
        std::int32_t value_b = 0;
        /// 2500 or 4960 in nearly all items. Meaning unconfirmed.
        std::uint32_t value_c = 0;
        /// The add-on pack the attraction came with: 0 for the base game, 1 for the first, 2 for the second.
        /// Version 4 and 5 files only; 0 in older ones, which hold only base game attractions.
        std::uint32_t addon = 0;
    };

    namespace detail
    {

        /// Byte offsets of the fields of `AttractionInfo` in one item type and file version, -1 where it has none.
        struct AttractionLayout
        {
            int type_id = -1;
            int name_text = -1;
            int description_text = -1;
            int icon = -1;
            int style = -1;
            int spline = -1;
            int value_a = -1;
            int value_b = -1;
            int value_c = -1;
            int addon = -1; ///< Read from version 4 files on
        };

        /// Guests a ride makes room for in the park, from the small block of a ride item that holds it: its
        /// first word, unless that is -1 for "the default", which then follows 0x1C bytes in. Nothing when the
        /// block does not fit. Checked by summing it over the open rides of saved parks against the ideal guest
        /// count they keep: exact for parks of only rides, and where one ride kind is in two overlays of
        /// different versions, the one the park names is the one that fits.
        [[nodiscard]] inline std::optional<std::uint32_t> read_ride_guests(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto block = ItemRecord::open(space, refs, address, 0x20);
            if (!block)
                return std::nullopt;
            const std::uint32_t first = block->u32(0);
            return first != 0xFFFFFFFFu ? first : block->u32(0x1C);
        }

        /// How riders sit on a ride, from the same block as its guests: its second word, numbered as ride cars
        /// number their seatings (see RideCar::kind). Nothing when the block does not fit or the word is out of
        /// that range.
        [[nodiscard]] inline std::optional<std::uint8_t> read_ride_seating(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address)
        {
            const auto block = ItemRecord::open(space, refs, address, 0x20);
            if (!block || block->u32(4) > 0xFF)
                return std::nullopt;
            return static_cast<std::uint8_t>(block->u32(4));
        }

        [[nodiscard]] inline AttractionInfo read_attraction(const ItemRecord& record, const AttractionLayout& at, std::uint32_t version)
        {
            const auto word = [&](int offset) -> std::uint32_t { return offset < 0 ? 0 : record.u32(offset); };
            const auto ref = [&](int offset, std::string_view tag) -> std::string_view { return offset < 0 ? std::string_view { } : record.ref(offset, tag); };

            AttractionInfo info;
            const std::uint32_t offset = version == 4 ? 512 : version == 5 ? 768 : 0;
            const std::uint32_t stored = word(at.type_id);
            info.type_id = stored >= offset ? stored - offset : stored;
            info.name_text = ref(at.name_text, "txt");
            info.description_text = ref(at.description_text, "txt");
            info.icon = ref(at.icon, "gsi");
            info.style = ref(at.style, "sid");
            info.spline = ref(at.spline, "spl");
            info.value_a = static_cast<std::int32_t>(word(at.value_a));
            info.value_b = static_cast<std::int32_t>(word(at.value_b));
            info.value_c = word(at.value_c);
            info.addon = version >= 4 ? word(at.addon) : 0;
            return info;
        }

    } // namespace detail

} // namespace rct3::ovl
