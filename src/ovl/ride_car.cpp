// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ride_car.hpp"

#include "core/minus_one.hpp"
#include "core/types.hpp"

#include <algorithm>
#include <utility>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "ric";

        // Bytes in one ride car record, per file layout: later layouts add fields at the end.
        constexpr usize kNoStreamSize = 240;
        constexpr usize kOneStreamSize = 292;
        constexpr usize kManyStreamsSize = 336;

        usize record_size(OvlLayout layout)
        {
            switch (layout)
            {
                case OvlLayout::NoStream:
                    return kNoStreamSize;
                case OvlLayout::OneStream:
                    return kOneStreamSize;
                case OvlLayout::ManyStreams:
                    return kManyStreamsSize;
            }
            std::unreachable();
        }

    } // namespace

    std::vector<RideCarItem> read_ride_cars(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const usize size = record_size(part.file.header.layout);

        std::vector<RideCarItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            RideCarItem item { parts.name, std::nullopt };
            if (const auto record = ItemRecord::open(space, refs, symbol.data, size))
            {
                RideCar car;
                car.name = record->text(0x00);
                car.display_name = record->text(0x04);
                car.kind = record->read_u32(0x08);
                // The body's weight, and each moving part's where the car has one (-1 where not).
                car.mass = record->real(0x10) + std::max(record->real(0x1C), 0.0f) + std::max(record->real(0xE0), 0.0f);
                car.visual = record->ref(0x0C, "svd");
                car.part = record->ref(0x14, "svd");
                car.second_part = record->ref(0xD8, "svd");
                for (usize i = 0; i < car.wheels.size(); ++i)
                {
                    car.wheels[i] = record->ref(0xA4 + 8 * i, "svd");
                    car.wheels_turned[i] = record->read_u32(0xA8 + 8 * i) != 0;
                }
                car.axles = { record->ref(0xC4, "svd"), record->ref(0xC8, "svd") };
                for (usize i = 0; i < car.rowing.size(); ++i)
                    car.rowing[i] = core::none_if_negative(record->read_i32(0x7C + 4 * i));
                // Byte 9, which later cars set to 2 or more, says which of the fields below they have.
                const u32 later = (car.kind >> 8) & 0xFF;
                car.spin_stops = later >= 2 ? std::max(record->read_u32(0x104), 1u) : 1;
                car.spin_settles = record->read_u32(0x3C) != 0;
                car.spin_held_on_chains = later >= 2 ? (record->read_u32(0x110) & 0x10) != 0 : car.spin_settles;
                // Each moving part's block: the second's fields lie at the first's plus this.
                constexpr std::array<usize, 2> kPartBlocks { 0, 0xC4 };
                for (usize i = 0; i < car.moves.size(); ++i)
                {
                    const usize at = kPartBlocks[i];
                    const u32 motion = record->read_u32(at + 0x18);
                    if (motion < std::to_underlying(PartMotion::Swings) || motion > std::to_underlying(PartMotion::Spins))
                        continue;
                    car.moves[i] = PartMoves {
                        .motion = static_cast<PartMotion>(motion),
                        .damping = { record->real(at + 0x20) },
                        .centring = { record->real(at + 0x24) },
                        .fastest = core::none_if_negative(record->real(at + 0x28)).transform([](f32 v) { return units::TurnRate { v }; }),
                        .pull = { record->real(at + 0x2C) },
                        .spring = record->real(at + 0x30),
                        .friction = record->real(at + 0x34),
                        .furthest = core::none_if_negative(record->real(at + 0x38)).transform([](f32 v) { return units::Radians { v }; }),
                        .weight = { std::max(record->real(at + 0x1C), 0.0f) },
                        // Of gravity, the share each part feels: in the latest cars, by part, from byte 308.
                        .gravity = { later > 2 && size >= kManyStreamsSize ? record->real(0x134 + 4 * i) : 1.0f },
                    };
                }
                car.references = record->references(static_cast<u32>(size));
                car.record = record->bytes();
                if ((record->read_u32(0x110) & 0x200) != 0)
                    car.trough = TroughRide { .damping = record->real(0x20), .centring = record->real(0x24), .friction = record->real(0x34), .pull = record->real(0x2C) };
                item.car = std::move(car);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
