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

        /// The strings of the list of `count` string pointers at `address`; empty when it does not fit.
        std::vector<std::string_view> strings(const AddressSpace& space, const ReferenceIndex& refs, std::uint32_t count, std::uint32_t address)
        {
            std::vector<std::string_view> out;
            const auto list = count == 0 ? std::nullopt : ItemRecord::open(space, refs, address, std::size_t { count } * 4);
            for (std::uint32_t i = 0; list && i < count; ++i)
                out.push_back(list->text(i * 4));
            return out;
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
                s.role = record->u32(0x10);
                s.flags = { record->u32(0x18), record->u32(0x1C) };
                for (std::size_t i = 0; i < s.splines.size(); ++i)
                    s.splines[i] = record->ref(0x20 + i * 4, "spl");
                for (std::size_t i = 0; i < s.extra_splines.size(); ++i)
                {
                    s.extra_splines[i] = record->ref(0x30 + i * 4, "spl");
                }
                for (std::size_t i = 0; i < s.water_splines.size(); ++i)
                    s.water_splines[i] = record->ref(0xD8 + i * 4, "spl");
                for (std::size_t i = 0; i < s.scale.size(); ++i)
                    s.scale[i] = record->real(0xBC + i * 4);
                if (version >= 4)
                {
                    s.start_join = record->text(0x50);
                    s.end_join = record->text(0x6C);
                    s.widths = { record->u32(0x114), record->u32(0x118) };
                }
                // Later records number their own layout, 2 in version 4 files and 3 in version 5: the group and
                // tags come with 2, the side and the other half with 3.
                const std::uint32_t layout = version >= 4 ? record->u32(0xE4) : 0;
                if (layout >= 2)
                {
                    s.group = record->text(0x104);
                    s.ground = record->u32(0x128);
                    for (std::size_t end = 0; end < 2; ++end)
                    {
                        const auto list = [&](std::size_t at) { return strings(space, refs, record->u32(at + end * 8), record->u32(at + end * 8 + 4)); };
                        s.tags[end] = { list(0x13C), list(0x14C), list(0x15C) };
                    }
                }
                if (layout >= 3)
                {
                    s.side = record->u32(0x16C);
                    s.partner = record->ref(0x170, kTag);
                }
                s.references = record->references(static_cast<std::uint32_t>(size));
                s.record = record->bytes();
                item.section = std::move(s);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
