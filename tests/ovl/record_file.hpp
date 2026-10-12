// SPDX-License-Identifier: GPL-3.0-or-later
//
// Test helper: a one-part overlay holding a string pool, a single item record and the symbol and
// reference rows that go with it. For readers of items that are one record of 32-bit fields.
#pragma once

#include "builder.hpp"
#include "ovl/overlay.hpp"

#include <bit>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::test
{

    /// NUL-terminated strings at the start of the address space, after four bytes of padding. `add` first, then ask for `base()`, the
    /// address where the record block starts.
    class Pool
    {
    public:
        std::uint32_t add(std::string_view text)
        {
            if (const auto at = text_.find(std::string(text) + '\0'); at != std::string::npos)
            {
                return static_cast<std::uint32_t>(at);
            }
            const auto at = static_cast<std::uint32_t>(text_.size());
            text_.append(text);
            text_.push_back('\0');
            return at;
        }
        [[nodiscard]] std::uint32_t base() const
        {
            return static_cast<std::uint32_t>((text_.size() + 3) & ~std::size_t { 3 });
        }
        [[nodiscard]] std::vector<std::byte> bytes() const
        {
            std::string padded = text_;
            padded.resize(base(), '\0');
            return bytes_of_string(padded);
        }

    private:
        std::string text_ = std::string(4, '\0'); // address 0 is a null pointer, so nothing lives there
    };

    /// A reference field of the record: its byte offset in the record and the symbol name it holds.
    struct FieldReference
    {
        std::uint32_t offset;
        std::string name;
    };

    /// Builds an overlay of the given file version with one pointer symbol `symbol` ("Name:tag") that
    /// points at the start of `record`. `pool` must hold the symbol and every reference name already.
    inline rct3::ovl::Overlay make_record_overlay(std::uint32_t version, Pool& pool, const std::string& symbol, const std::vector<std::byte>& record, const std::vector<FieldReference>& references = { })
    {
        const std::uint32_t base = pool.base();
        const std::uint32_t symbol_name = pool.add(symbol);
        const bool wide = version != 1;

        std::vector<std::byte> symbols;
        append_u32(symbols, symbol_name);
        append_u32(symbols, base);
        if (wide)
        {
            append_u16(symbols, 1);
            append_u16(symbols, 0xFFFF);
            append_u32(symbols, 0);
        }
        else
        {
            append_u32(symbols, 1);
        }

        std::vector<std::byte> refs;
        for (const auto& r : references)
        {
            append_u32(refs, base + r.offset);
            append_u32(refs, pool.add(r.name));
            append_u32(refs, 0);
            if (wide)
                append_u32(refs, 0);
        }

        REQUIRE(pool.base() == base); // a name was missing from the pool, so the record moved
        auto overlay = rct3::ovl::Overlay::from_bytes(build_file(version, { { pool.bytes() }, { record }, { symbols, { }, refs } }, { }), std::nullopt);
        REQUIRE(overlay.has_value());
        return std::move(*overlay);
    }

    /// Appends 32-bit words, a float as its bit pattern.
    inline void put(std::vector<std::byte>& out, std::initializer_list<std::uint32_t> values)
    {
        for (auto v : values)
            append_u32(out, v);
    }
    inline std::uint32_t bits(float v)
    {
        return std::bit_cast<std::uint32_t>(v);
    }

} // namespace rct3::test
