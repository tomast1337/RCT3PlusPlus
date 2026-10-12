// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/tracked_ride.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "trr";
        constexpr std::size_t kHeaderSize = 0x28; // the version 1 fields shared by all attractions
        constexpr std::size_t kLaterSize = 0x124; // up to and including the last spline slot

    } // namespace

    std::vector<TrackedRideItem> read_tracked_rides(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const std::uint32_t version = part.file.header.version;

        std::vector<TrackedRideItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            TrackedRideItem item { parts.name, std::nullopt };
            if (version == 1)
            {
                if (const auto record = ItemRecord::open(space, refs, symbol.data, kHeaderSize))
                {
                    const detail::AttractionLayout at { .type_id = 0x00, .name_text = 0x04, .description_text = 0x08, .icon = 0x0C, .spline = 0x24, .value_a = 0x10, .value_b = 0x14, .value_c = 0x20 };
                    TrackedRide ride;
                    ride.info = detail::read_attraction(*record, at, version);
                    item.ride = ride;
                }
            }
            else if (const auto record = ItemRecord::open(space, refs, symbol.data, kLaterSize))
            {
                TrackedRide ride;
                ride.name = record->text(0x04);
                for (std::size_t i = 0; i < ride.track_sections.size(); ++i)
                {
                    ride.track_sections[i] = record->ref(0xD4 + i * 4, "tks");
                    ride.splines[i] = record->ref(0x114 + i * 4, "spl");
                }
                item.ride = ride;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
