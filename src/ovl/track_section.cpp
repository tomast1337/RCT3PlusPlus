// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/track_section.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "tks";

        std::size_t record_size(std::uint32_t version)
        {
            switch (version)
            {
                case 1:
                    return 228;
                case 4:
                    return 364;
                default:
                    return 392;
            }
        }

    } // namespace

    std::vector<TrackSectionItem> read_track_sections(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const std::uint32_t version = part.file.header.version;

        std::vector<TrackSectionItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            TrackSectionItem item { parts.name, std::nullopt };
            const auto size = record_size(version);
            if (const auto record = ItemRecord::open(space, refs, symbol.data, size))
            {
                TrackSection s;
                s.name = record->text(0x00);
                s.scenery_item = record->ref(0x04, "sid");
                s.join_a = record->u32(0x08);
                s.join_b = record->u32(0x0C);
                s.flags = { record->u32(0x18), record->u32(0x1C) };
                for (std::size_t i = 0; i < s.splines.size(); ++i)
                    s.splines[i] = record->ref(0x20 + i * 4, "spl");
                for (std::size_t i = 0; i < s.extra_splines.size(); ++i)
                {
                    s.extra_splines[i] = record->ref(0x30 + i * 4, "spl");
                }
                for (std::size_t i = 0; i < s.scale.size(); ++i)
                    s.scale[i] = record->real(0xBC + i * 4);
                s.references = record->references(static_cast<std::uint32_t>(size));
                s.record = record->bytes();
                item.section = std::move(s);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
