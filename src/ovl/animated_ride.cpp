// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/animated_ride.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "anr";

        struct Variant
        {
            detail::AttractionLayout layout;
            std::size_t size;    // bytes of the item that are read
            std::size_t minimum; // bytes that must be there: the spline slots are missing from some items
            int splines;         // offset of the four spline slots, -1 for none
        };

        Variant variant(std::uint32_t version)
        {
            switch (version)
            {
                case 1:
                    return { { .type_id = 0x00, .name_text = 0x04, .description_text = 0x08, .icon = 0x0C, .spline = 0x24, .value_a = 0x10, .value_b = 0x14, .value_c = 0x20 }, 0x28, 0x28, -1 };
                case 4:
                    return { { .type_id = 0x68, .name_text = 0x6C, .description_text = 0x70, .icon = 0x74, .style = 0x18, .spline = 0x8C, .value_a = 0x78, .value_b = 0x7C, .value_c = 0x88 }, 0xB8, 0x90, 0xA8 };
                default:
                    return { { .type_id = 0x70, .name_text = 0x74, .description_text = 0x78, .icon = 0x7C, .style = 0x18, .spline = 0x94, .value_a = 0x80, .value_b = 0x84, .value_c = 0x90 }, 0xC4, 0x98, 0xB4 };
            }
        }

    } // namespace

    std::vector<AnimatedRideItem> read_animated_rides(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const std::uint32_t version = part.file.header.version;
        const Variant v = variant(version);

        std::vector<AnimatedRideItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            AnimatedRideItem item { parts.name, std::nullopt };
            if (const auto record = ItemRecord::open_up_to(space, refs, symbol.data, v.size, v.minimum))
            {
                AnimatedRide ride;
                ride.info = detail::read_attraction(*record, v.layout, version);
                if (v.splines >= 0)
                {
                    for (std::size_t i = 0; i < ride.splines.size(); ++i)
                    {
                        ride.splines[i] = record->ref(static_cast<std::size_t>(v.splines) + i * 4, "spl");
                    }
                }
                item.ride = ride;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
