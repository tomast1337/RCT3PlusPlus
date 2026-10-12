// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/ride_car.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "ric";

        std::size_t record_size(std::uint32_t version)
        {
            switch (version)
            {
                case 1:
                    return 240;
                case 4:
                    return 292;
                default:
                    return 336;
            }
        }

    } // namespace

    std::vector<RideCarItem> read_ride_cars(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const std::size_t size = record_size(part.file.header.version);

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
                car.kind = record->u32(0x08);
                car.visual = record->ref(0x0C, "svd");
                car.references = record->references(static_cast<std::uint32_t>(size));
                car.record = record->bytes();
                item.car = std::move(car);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
