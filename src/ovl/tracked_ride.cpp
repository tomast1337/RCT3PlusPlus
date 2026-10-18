// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/tracked_ride.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "trr";
        constexpr std::size_t kHeaderSize = 0x28; // the version 1 fields shared by all attractions
        constexpr std::size_t kLaterSize = 0x130; // up to and including the pointer to the attraction fields
        constexpr detail::AttractionLayout kFields { .type_id = 0x00, .name_text = 0x04, .description_text = 0x08, .icon = 0x0C, .spline = 0x24, .value_a = 0x10, .value_b = 0x14, .value_c = 0x20, .addon = 0x38 };
        constexpr std::size_t kLaterFieldsSize = 0x3C; // later items' fields run on to the add-on pack

        /// Later items point at 0x12C to a block whose attraction fields start 0x3C bytes in (version 4) or
        /// 0x44 bytes in (version 5).
        std::uint32_t attraction_offset(std::uint32_t version)
        {
            return version == 4 ? 0x3C : 0x44;
        }

        /// The ride parameters sit at the start of later items and 0x50 bytes in in version 1 items, after
        /// the attraction fields; from there the layout is the same.
        constexpr std::size_t kOperatingSize = 0x110;

        std::optional<RideOperatingDefaults> read_operating(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address, std::uint32_t version)
        {
            const auto r = ItemRecord::open(space, refs, address + (version == 1 ? 0x50 : 0), kOperatingSize);
            if (!r)
                return std::nullopt;
            RideOperatingDefaults d;
            d.mode = r->u32(0x48);
            d.allowed_modes = r->u32(0x4C);
            d.launch_speed = r->real(0x5C);
            d.lift_speed = r->real(0x64);
            d.brake_speed = r->real(0x6C);
            d.block_brake_speed = r->real(0x74);
            if (const float fixed = r->real(0x7C); fixed > 0)
                d.fixed_speed = fixed;
            d.lift_min = r->real(0xF0);
            d.lift_max = r->real(0xF4);
            d.cost = r->i32(0xFC);
            d.launch_min = r->real(0x108);
            d.launch_max = r->real(0x10C);
            return d;
        }

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
                    TrackedRide ride;
                    ride.info = detail::read_attraction(*record, kFields, version);
                    ride.operating = read_operating(space, refs, symbol.data, version);
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
                const std::uint32_t fields = record->u32(0x12C);
                if (const auto block = fields == 0 ? std::nullopt : ItemRecord::open_up_to(space, refs, fields + attraction_offset(version), kLaterFieldsSize, kHeaderSize))
                    ride.info = detail::read_attraction(*block, kFields, version);
                ride.operating = read_operating(space, refs, symbol.data, version);
                item.ride = ride;
            }
            items.push_back(item);
        }
        return items;
    }

} // namespace rct3::ovl
