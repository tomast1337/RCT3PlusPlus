// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/tracked_ride.hpp"

#include <algorithm>
#include <cstring>

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
            d.platform = r->text(0x38);
            constexpr std::uint32_t kMostTrains = 64; // Our own bound against a stray count
            const std::uint32_t list = r->u32(0x0C);
            for (std::uint32_t i = 0; i < std::min(r->u32(0x08), kMostTrains) && list != 0; ++i)
            {
                const auto entry = space.bytes_at(list + i * 4, 4);
                if (!entry)
                    break;
                std::uint32_t name = 0;
                std::memcpy(&name, entry->data(), 4);
                if (const auto text = space.cstring_at(name); text && !text->empty())
                    d.trains.push_back(*text);
            }
            return d;
        }

        /// A piece test as an entry stores it: a word whose low byte is the kind and next byte the flags (0x1
        /// negate, 0x2 forward, 0x4 backward, 0x8 first, 0x10 last), then the group's name or the piece.
        PieceTest piece_test(const ItemRecord& entry, std::size_t at)
        {
            const std::uint32_t word = entry.u32(at);
            PieceTest t;
            t.kind = static_cast<std::uint8_t>(word & 0xFF);
            t.name = t.kind == 0 ? entry.text(at + 4) : t.kind == 1 ? entry.ref(at + 4, "tks") : std::string_view { };
            const std::uint32_t f = word >> 8;
            t.negate = (f & 0x1) != 0;
            t.forward = (f & 0x2) != 0;
            t.backward = (f & 0x4) != 0;
            t.first = (f & 0x8) != 0;
            t.last = (f & 0x10) != 0;
            return t;
        }

        /// The rules of a later item at `address`, its attraction fields block at `fields` (0 when it has none).
        TrackRules read_rules(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t address, std::uint32_t fields, std::uint32_t version)
        {
            TrackRules rules;
            if (const auto doors = fields == 0 ? std::nullopt : ItemRecord::open(space, refs, fields, 0x30))
            {
                rules.entrances = doors->u32(0x28);
                rules.exits = doors->u32(0x2C);
            }
            if (const auto block = fields == 0 ? std::nullopt : ItemRecord::open(space, refs, fields + attraction_offset(version), 0x34))
                rules.flags = block->u32(0x30);
            const auto r = ItemRecord::open(space, refs, address, 0x184);
            if (!r)
                return rules;
            if (const std::int32_t least = r->i32(0x17C); least >= 0)
                rules.min_pieces = static_cast<std::uint32_t>(least);
            rules.circuit_only = r->u32(0x180) != 0;
            // A list of (count, pointer) pairs, each pointing at that many 16-byte entries: the piece test,
            // then what the piece becomes.
            const std::uint32_t count = r->u32(0x148);
            const auto list = count == 0 ? std::nullopt : ItemRecord::open(space, refs, r->u32(0x14C), std::size_t { count } * 8);
            for (std::uint32_t i = 0; list && i < count; ++i)
            {
                const std::uint32_t n = list->u32(i * 8);
                const auto entries = n == 0 ? std::nullopt : ItemRecord::open(space, refs, list->u32(i * 8 + 4), std::size_t { n } * 16);
                if (!entries)
                    continue;
                auto& pattern = rules.patterns.emplace_back();
                for (std::uint32_t k = 0; k < n; ++k)
                    pattern.push_back({ piece_test(*entries, k * 16), piece_test(*entries, k * 16 + 8) });
            }
            return rules;
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
                    ride.guests = detail::read_ride_guests(space, refs, symbol.data + 0x38);
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
                // The guests lead the block the attraction fields are in.
                if (fields != 0)
                    ride.guests = detail::read_ride_guests(space, refs, fields);
                ride.operating = read_operating(space, refs, symbol.data, version);
                ride.rules = read_rules(space, refs, symbol.data, fields, version);
                item.ride = std::move(ride);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
