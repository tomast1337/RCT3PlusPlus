// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/track_section.hpp"

#include "core/types.hpp"

#include <utility>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "tks";

        // Bytes in one track piece record, per file layout: later layouts add fields at the end.
        constexpr usize kNoStreamSize = 228;
        constexpr usize kOneStreamSize = 364;
        constexpr usize kManyStreamsSize = 392;

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

        /// The strings of the list of `count` string pointers at `address`; empty when it does not fit.
        std::vector<std::string_view> strings(const AddressSpace& space, const ReferenceIndex& refs, u32 count, u32 address)
        {
            std::vector<std::string_view> out;
            const auto list = count == 0 ? std::nullopt : ItemRecord::open(space, refs, address, usize { count } * 4);
            for (u32 i = 0; list && i < count; ++i)
                out.push_back(list->text(i * 4));
            return out;
        }

    } // namespace

    std::vector<TrackSectionItem> read_track_sections(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceIndex refs(part, space);
        const OvlLayout layout = part.file.header.layout;

        std::vector<TrackSectionItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTag)
                continue;

            TrackSectionItem item { parts.name, std::nullopt };
            const auto size = record_size(layout);
            if (const auto record = ItemRecord::open(space, refs, symbol.data, size))
            {
                TrackSection s;
                s.name = record->text(0x00);
                s.scenery_item = record->ref(0x04, "sid");
                s.join_a = record->read_u32(0x08);
                s.join_b = record->read_u32(0x0C);
                s.role = record->read_u32(0x10);
                s.flags = { record->read_u32(0x18), record->read_u32(0x1C) };
                for (usize i = 0; i < s.splines.size(); ++i)
                    s.splines[i] = record->ref(0x20 + i * 4, "spl");
                for (usize i = 0; i < s.extra_splines.size(); ++i)
                {
                    s.extra_splines[i] = record->ref(0x30 + i * 4, "spl");
                }
                for (usize i = 0; i < s.water_splines.size(); ++i)
                    s.water_splines[i] = record->ref(0xD8 + i * 4, "spl");
                s.platform_run = record->text(0xE0);
                for (usize i = 0; i < s.scale.size(); ++i)
                    s.scale[i] = record->real(0xBC + i * 4);
                if (layout != OvlLayout::NoStream)
                {
                    s.start_join = record->text(0x50);
                    s.end_join = record->text(0x6C);
                    s.widths = { record->read_u32(0x114), record->read_u32(0x118) };
                }
                // Entries of the least and most speed and the force, and with the layout below a span of the piece too.
                const bool spans = (s.flags[0] & 0x400000) != 0 && (layout != OvlLayout::NoStream ? record->read_u32(0xE4) : 0) >= 2;
                const usize entry = spans ? 20 : 12;
                const u32 pushes = record->read_u32(0x70);
                if (const auto list = pushes == 0 ? std::nullopt : ItemRecord::open(space, refs, record->read_u32(0x74), usize { pushes } * entry))
                    for (u32 i = 0; i < pushes; ++i)
                    {
                        const usize at = i * entry;
                        s.pushes.push_back({ list->real(at), list->real(at + 4), list->real(at + 8), spans ? list->real(at + 12) : 0, spans ? list->real(at + 16) : -1 });
                    }
                // Later records number their own layout, 2 in one-stream files and 3 in many-stream files: the group and
                // tags come with 2, the side and the other half with 3.
                const u32 record_layout = layout != OvlLayout::NoStream ? record->read_u32(0xE4) : 0;
                if (record_layout >= 2)
                {
                    s.group = record->text(0x104);
                    s.ground = record->read_u32(0x128);
                    if (const f32 drop_off = record->real(0x124); drop_off >= 0)
                        s.drop_off = drop_off;
                    const u32 count = record->read_u32(0xF4);
                    if (const auto list = count == 0 ? std::nullopt : ItemRecord::open(space, refs, record->read_u32(0xF8), usize { count } * 16))
                        for (u32 i = 0; i < count; ++i)
                            s.squares.push_back({ list->read_i32(i * 16), list->read_i32(i * 16 + 4), list->read_i32(i * 16 + 8), list->read_u32(i * 16 + 12) });
                    for (usize end = 0; end < 2; ++end)
                    {
                        const auto list = [&](usize at) { return strings(space, refs, record->read_u32(at + end * 8), record->read_u32(at + end * 8 + 4)); };
                        s.tags[end] = { list(0x13C), list(0x14C), list(0x15C) };
                    }
                }
                if (record_layout >= 3)
                {
                    s.side = record->read_u32(0x16C);
                    s.partner = record->ref(0x170, kTag);
                }
                s.references = record->references(static_cast<u32>(size));
                s.record = record->bytes();
                item.section = std::move(s);
            }
            items.push_back(std::move(item));
        }
        return items;
    }

} // namespace rct3::ovl
