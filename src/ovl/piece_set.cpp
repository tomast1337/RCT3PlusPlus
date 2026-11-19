// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/piece_set.hpp"

#include "core/types.hpp"
#include "ovl/field_values.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kPathTag = "ptd";
        constexpr std::string_view kQueueTag = "qtd";
        constexpr std::string_view kPoolTag = "pty";
        constexpr std::string_view kEnclosureTag = "enc";
        constexpr std::string_view kTextTag = "txt";
        constexpr std::string_view kIconTag = "gsi";
        constexpr std::string_view kGroundTag = "ftx";
        constexpr std::string_view kSplineTag = "spl";

        constexpr usize kResearchRowSize = 12;

        // Sizes of the headers in bytes. The path header is the part before the research link rows.
        constexpr usize kPathNamesAt = 0x10;
        constexpr usize kPathBaseNames = 78;
        constexpr usize kPathLongNames = 147;
        constexpr usize kPathBaseHeader = 0x150; // names, research count, research pointer
        constexpr usize kPathLongHeader = 0x25c;
        constexpr usize kQueueHeader = 0x34;
        constexpr usize kPoolNames = 89;
        constexpr usize kPoolHeader = 0x258;
        constexpr usize kEnclosureHeader = 0x5c;

        /// Everything the four readers share: the item reader, which owns the address space and the tables.
        struct Context
        {
            const SceneryItemReader reader;
            Context(const Overlay& overlay, const Part& part)
                : reader(overlay, part)
            {
            }

            [[nodiscard]] std::string_view string_at(u32 location, u32 value) const noexcept
            {
                return fields::string_field(reader.space(), reader.pointers(), location, value);
            }
            [[nodiscard]] std::string_view reference(u32 location, std::string_view tag) const noexcept
            {
                return reader.references().find(location, tag);
            }
        };

        /// `count` string pointer slots starting at `first` (an address), read out of `raw` at `offset`.
        void read_names(const Context& ctx, std::span<const std::byte> raw, u32 address, usize offset, usize count, std::vector<std::string_view>& out)
        {
            out.reserve(count);
            for (usize i = 0; i < count; ++i)
            {
                const usize at = offset + i * 4;
                out.push_back(ctx.string_at(address + static_cast<u32>(at), fields::read_u32(raw, at)));
            }
        }

        /// Research count and pointer at `offset`; the rows may follow the header right away, in which case the
        /// returned value is how many bytes of rows to skip.
        std::optional<usize> read_research(const Context& ctx, std::span<const std::byte> raw, u32 address, usize offset, usize header_size, std::vector<SceneryItemResearch>& out)
        {
            const u32 count = fields::read_u32(raw, offset);
            const u32 rows = fields::read_u32(raw, offset + 4);
            if (!ctx.reader.read_research(rows, count, out))
                return std::nullopt;
            const bool inline_rows = count != 0 && rows == address + header_size;
            return inline_rows ? usize { count } * kResearchRowSize : 0;
        }

        std::optional<std::vector<SceneryItem>> read_items(const Context& ctx, u32 address, usize offset)
        {
            if (offset > UINT32_MAX - address)
                return std::nullopt;
            const auto end = ctx.reader.block_end(address);
            if (!end)
                return std::nullopt;
            if (address + offset == *end)
                return std::vector<SceneryItem> { }; // header only
            return ctx.reader.read_run(address + static_cast<u32>(offset));
        }

        std::optional<PathType> read_path(const Context& ctx, u32 address)
        {
            const auto head = ctx.reader.space().bytes_at(address, 4);
            if (!head)
                return std::nullopt;
            PathType p;
            p.header_word = fields::read_u32(*head, 0);
            const u32 expansion = p.header_word >> 24;
            if (expansion > 1)
                return std::nullopt;

            const usize header = expansion == 0 ? kPathBaseHeader : kPathLongHeader;
            const auto raw = ctx.reader.space().bytes_at(address, header);
            if (!raw)
                return std::nullopt;
            p.name = ctx.string_at(address + 4, fields::read_u32(*raw, 4));
            p.name_text = ctx.reference(address + 8, kTextTag);
            p.icon = ctx.reference(address + 0xc, kIconTag);

            usize items_at = header;
            if (expansion == 0)
            {
                read_names(ctx, *raw, address, kPathNamesAt, kPathBaseNames, p.piece_names);
                const auto skip = read_research(ctx, *raw, address, kPathNamesAt + kPathBaseNames * 4, header, p.research);
                if (!skip)
                    return std::nullopt;
                items_at += *skip;
            }
            else
            {
                read_names(ctx, *raw, address, kPathNamesAt, kPathLongNames, p.piece_names);
            }
            auto items = read_items(ctx, address, items_at);
            if (!items)
                return std::nullopt;
            p.items = std::move(*items);
            return p;
        }

        std::optional<QueueType> read_queue(const Context& ctx, u32 address)
        {
            const auto raw = ctx.reader.space().bytes_at(address, kQueueHeader);
            if (!raw)
                return std::nullopt;
            QueueType q;
            q.name = ctx.string_at(address, fields::read_u32(*raw, 0));
            q.name_text = ctx.reference(address + 4, kTextTag);
            q.icon = ctx.reference(address + 8, kIconTag);
            q.ground = ctx.reference(address + 0xc, kGroundTag);
            for (usize i = 0; i < q.piece_names.size(); ++i)
            {
                const usize at = 0x10 + i * 4;
                q.piece_names[i] = ctx.string_at(address + static_cast<u32>(at), fields::read_u32(*raw, at));
            }
            const auto skip = read_research(ctx, *raw, address, 0x2c, kQueueHeader, q.research);
            if (!skip)
                return std::nullopt;
            auto items = read_items(ctx, address, kQueueHeader + *skip);
            if (!items)
                return std::nullopt;
            q.items = std::move(*items);
            return q;
        }

        std::optional<PoolType> read_pool(const Context& ctx, u32 address)
        {
            const auto raw = ctx.reader.space().bytes_at(address, kPoolHeader);
            if (!raw)
                return std::nullopt;
            PoolType p;
            p.name = ctx.string_at(address, fields::read_u32(*raw, 0));
            p.name_text = ctx.reference(address + 4, kTextTag);
            p.icon = ctx.reference(address + 8, kIconTag);
            read_names(ctx, *raw, address, 0xc, kPoolNames, p.piece_names);
            usize at = 0xc + kPoolNames * 4;
            for (auto& v : p.prices)
                v = fields::read_i32(*raw, at), at += 4;
            for (auto& v : p.unknown_floats)
                v = fields::read_f32(*raw, at), at += 4;
            const usize spline_count = (kPoolHeader - at) / 4;
            p.splines.reserve(spline_count);
            for (usize i = 0; i < spline_count; ++i)
            {
                p.splines.push_back(ctx.reference(address + static_cast<u32>(at + i * 4), kSplineTag));
            }
            auto items = read_items(ctx, address, kPoolHeader);
            if (!items)
                return std::nullopt;
            p.items = std::move(*items);
            return p;
        }

        std::optional<EnclosureType> read_enclosure(const Context& ctx, u32 address)
        {
            const auto raw = ctx.reader.space().bytes_at(address, kEnclosureHeader);
            if (!raw)
                return std::nullopt;
            EnclosureType e;
            e.name = ctx.string_at(address, fields::read_u32(*raw, 0));
            e.name_text = ctx.reference(address + 4, kTextTag);
            e.icon = ctx.reference(address + 8, kIconTag);
            for (usize i = 0; i < e.piece_names.size(); ++i)
            {
                const usize at = 0xc + i * 4;
                e.piece_names[i] = ctx.string_at(address + static_cast<u32>(at), fields::read_u32(*raw, at));
            }
            for (usize i = 0; i < e.unknown_values.size(); ++i)
                e.unknown_values[i] = fields::read_i32(*raw, 0x30 + i * 4);
            for (usize i = 0; i < e.splines.size(); ++i)
            {
                e.splines[i] = ctx.reference(address + 0x40 + static_cast<u32>(i * 4), kSplineTag);
            }
            for (usize i = 0; i < e.unknown_floats.size(); ++i)
                e.unknown_floats[i] = fields::read_f32(*raw, 0x50 + i * 4);
            auto items = read_items(ctx, address, kEnclosureHeader);
            if (!items)
                return std::nullopt;
            e.items = std::move(*items);
            return e;
        }

    } // namespace

    std::vector<PathTypeEntry> read_path_types(const Overlay& overlay, const Part& part)
    {
        const Context ctx(overlay, part);
        std::vector<PathTypeEntry> out;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kPathTag)
                out.push_back({ parts.name, read_path(ctx, symbol.data) });
        }
        return out;
    }

    std::vector<QueueTypeEntry> read_queue_types(const Overlay& overlay, const Part& part)
    {
        const Context ctx(overlay, part);
        std::vector<QueueTypeEntry> out;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kQueueTag)
                out.push_back({ parts.name, read_queue(ctx, symbol.data) });
        }
        return out;
    }

    std::vector<PoolTypeEntry> read_pool_types(const Overlay& overlay, const Part& part)
    {
        const Context ctx(overlay, part);
        std::vector<PoolTypeEntry> out;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kPoolTag)
                out.push_back({ parts.name, read_pool(ctx, symbol.data) });
        }
        return out;
    }

    std::vector<EnclosureTypeEntry> read_enclosure_types(const Overlay& overlay, const Part& part)
    {
        const Context ctx(overlay, part);
        std::vector<EnclosureTypeEntry> out;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag == kEnclosureTag)
                out.push_back({ parts.name, read_enclosure(ctx, symbol.data) });
        }
        return out;
    }

} // namespace rct3::ovl
