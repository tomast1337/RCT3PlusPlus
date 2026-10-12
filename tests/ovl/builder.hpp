// SPDX-License-Identifier: GPL-3.0-or-later
//
// Builds synthetic OVL bytes for tests. Values are invented and only shaped like the format.
#pragma once

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

namespace rct3::test
{

    struct Builder
    {
        std::vector<std::byte> bytes;

        void raw(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            bytes.insert(bytes.end(), b, b + n);
        }
        void u8(std::uint8_t v)
        {
            raw(&v, 1);
        }
        void u16(std::uint16_t v)
        {
            raw(&v, 2);
        }
        void u32(std::uint32_t v)
        {
            raw(&v, 4);
        }
        void str(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            raw(s.data(), s.size());
        }
    };

    /// Bytes of every block, per block type (index 0..8). Missing types are empty.
    using BlockBytes = std::vector<std::vector<std::vector<std::byte>>>;

    inline std::vector<std::byte> bytes_of(std::initializer_list<std::uint8_t> values)
    {
        std::vector<std::byte> out;
        for (auto v : values)
            out.push_back(static_cast<std::byte>(v));
        return out;
    }

    inline std::vector<std::byte> bytes_of_string(const std::string& text)
    {
        std::vector<std::byte> out;
        for (char c : text)
            out.push_back(static_cast<std::byte>(c));
        return out;
    }

    inline void append_u32(std::vector<std::byte>& out, std::uint32_t v)
    {
        const auto* p = reinterpret_cast<const std::byte*>(&v);
        out.insert(out.end(), p, p + 4);
    }

    inline void append_u16(std::vector<std::byte>& out, std::uint16_t v)
    {
        const auto* p = reinterpret_cast<const std::byte*>(&v);
        out.insert(out.end(), p, p + 2);
    }

    /// A complete file of the given version with no external references and no loaders: definitions,
    /// post-block metadata (v4: two zero words, v5: bytes {1,2,3} and words {7,8}), block data, relocations,
    /// then `tail` verbatim.
    /// (location, value) relocation pairs of each data stream.
    using StreamTables = std::vector<std::vector<std::pair<std::uint32_t, std::uint32_t>>>;

    /// `streams` gives each data stream's relocation pairs. v4 always has one table (empty when not given),
    /// v5 has as many streams as there are tables, v1 has none.
    inline std::vector<std::byte> build_file(std::uint32_t version, const BlockBytes& blocks, const std::vector<std::uint32_t>& relocations, const std::vector<std::byte>& tail = { }, const StreamTables& streams = { })
    {
        Builder b;
        b.raw("FGRK", 4);
        b.u8(0x02);
        b.u8(0), b.u8(0), b.u8(0);
        b.u32(version);
        if (version != 1)
            b.u32(0x10);
        const bool streamed = version == 5 && !streams.empty();
        if (version == 5)
        {
            b.u16(static_cast<std::uint16_t>(streams.size())), b.u16(0);
            if (streamed)
                b.u32(16), b.u32(0), b.u32(0), b.u32(0); // stream block: size, 2 reserved, empty tag
        }
        b.u32(0); // external refs
        b.u32(0); // secondary refs
        b.u32(0); // loaders

        static const std::vector<std::vector<std::byte>> none;
        for (std::size_t t = 0; t < 9; ++t)
        {
            const auto& type = t < blocks.size() ? blocks[t] : none;
            b.u32(static_cast<std::uint32_t>(type.size()));
            if (version > 1)
            {
                b.u32(0); // unknown
                if (streamed)
                    b.u32(0);
                for (const auto& block : type)
                    b.u32(static_cast<std::uint32_t>(block.size()));
            }
        }

        if (version == 4)
            b.u32(0), b.u32(0);
        if (version == 5)
        {
            b.u32(3), b.u8(1), b.u8(2), b.u8(3);
            b.u32(2), b.u32(7), b.u32(8);
        }

        for (const auto& type : blocks)
        {
            for (const auto& block : type)
            {
                if (version == 1)
                    b.u32(static_cast<std::uint32_t>(block.size()));
                b.bytes.insert(b.bytes.end(), block.begin(), block.end());
            }
        }

        b.u32(static_cast<std::uint32_t>(relocations.size()));
        for (auto r : relocations)
            b.u32(r);
        if (version == 4 || streamed)
        {
            for (std::size_t i = 0; i < (version == 4 ? 1 : streams.size()); ++i)
            {
                const auto empty = StreamTables::value_type { };
                const auto& table = i < streams.size() ? streams[i] : empty;
                b.u32(static_cast<std::uint32_t>(table.size()));
                for (const auto& [location, value] : table)
                    b.u32(location), b.u32(value);
            }
        }
        b.bytes.insert(b.bytes.end(), tail.begin(), tail.end());
        return b.bytes;
    }

} // namespace rct3::test
