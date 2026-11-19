// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helper for item readers whose items are one fixed-size record of 32-bit fields: bounds
// checked access to the record and to the references stored in it.
#pragma once

#include "core/types.hpp"
#include "ovl/overlay.hpp"

#include <algorithm>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <optional>
#include <span>
#include <string_view>
#include <vector>

namespace rct3::ovl
{

    /// A reference field found inside an item: the field at `offset` names the item `name` of type `tag`.
    struct ItemReference
    {
        u32 offset = 0; ///< Byte offset of the field from the start of the item
        std::string_view tag;
        std::string_view name;
    };

    /// All references of one part, ordered by location.
    class ReferenceIndex
    {
    public:
        ReferenceIndex(const Part& part, const AddressSpace& space)
        {
            if (auto table = read_references(part.file, part.bytes, space))
            {
                for (const Reference& r : *table)
                {
                    if (!r.name.empty())
                        rows_.push_back({ r.location, split_symbol_name(r.name) });
                }
            }
            // Not stable_sort: its temporary buffer trips an allocator mismatch report under the sanitizer.
            std::ranges::sort(rows_, [](const Row& a, const Row& b) {
                if (a.location != b.location)
                    return a.location < b.location;
                if (a.name.tag != b.name.tag)
                    return a.name.tag < b.name.tag;
                return a.name.name < b.name.name;
            });
        }

        /// Name of the item the field at `location` refers to, when it is of type `tag`. Empty otherwise.
        [[nodiscard]] std::string_view name_at(u32 location, std::string_view tag) const noexcept
        {
            const auto it = std::ranges::lower_bound(rows_, location, { }, &Row::location);
            if (it == rows_.end() || it->location != location || it->name.tag != tag)
                return { };
            return it->name.name;
        }

        /// References whose field lies in `[address, address + size)`, with offsets relative to `address`.
        [[nodiscard]] std::vector<ItemReference> within(u32 address, u32 size) const
        {
            std::vector<ItemReference> out;
            auto it = std::ranges::lower_bound(rows_, address, { }, &Row::location);
            for (; it != rows_.end() && it->location - address < size; ++it)
            {
                out.push_back({ it->location - address, it->name.tag, it->name.name });
            }
            return out;
        }

    private:
        struct Row
        {
            u32 location;
            SymbolName name;
        };
        std::vector<Row> rows_;
    };

    /// Read access to the record at one address. Offsets are byte offsets from the start of the record and
    /// must lie inside the size the record was opened with, otherwise values come back as zero or empty.
    class ItemRecord
    {
    public:
        /// Opens `size` bytes at `address`. Nothing when they do not lie inside one block.
        [[nodiscard]] static std::optional<ItemRecord> open(const AddressSpace& space, const ReferenceIndex& refs, u32 address, usize size)
        {
            const auto bytes = space.bytes_at(address, size);
            if (!bytes)
                return std::nullopt;
            return ItemRecord(space, refs, address, *bytes);
        }

        /// Opens as many bytes as lie inside the block, at most `size` and at least `minimum`. For items whose
        /// last fields are missing from some files.
        [[nodiscard]] static std::optional<ItemRecord> open_up_to(const AddressSpace& space, const ReferenceIndex& refs, u32 address, usize size, usize minimum)
        {
            for (usize n = size; n >= minimum; n -= 4)
            {
                if (auto record = open(space, refs, address, n))
                    return record;
                if (n < 4)
                    break;
            }
            return std::nullopt;
        }

        [[nodiscard]] std::span<const std::byte> bytes() const noexcept
        {
            return bytes_;
        }

        [[nodiscard]] u32 read_u32(usize offset) const noexcept
        {
            u32 v = 0;
            if (offset + 4 <= bytes_.size())
                std::memcpy(&v, bytes_.data() + offset, sizeof v);
            return v;
        }
        [[nodiscard]] i32 read_i32(usize offset) const noexcept
        {
            return static_cast<i32>(read_u32(offset));
        }
        [[nodiscard]] f32 real(usize offset) const noexcept
        {
            return std::bit_cast<f32>(read_u32(offset));
        }

        /// The string the pointer field at `offset` points at. Empty for a null or bad pointer.
        [[nodiscard]] std::string_view text(usize offset) const noexcept
        {
            const u32 pointer = read_u32(offset);
            if (pointer == 0)
                return { };
            return space_->cstring_at(pointer).value_or(std::string_view { });
        }

        /// Name of the item the reference field at `offset` holds, when it is of type `tag`.
        [[nodiscard]] std::string_view ref(usize offset, std::string_view tag) const noexcept
        {
            if (offset + 4 > bytes_.size())
                return { };
            return refs_->name_at(address_ + static_cast<u32>(offset), tag);
        }

        /// Every reference stored in the first `size` bytes.
        [[nodiscard]] std::vector<ItemReference> references(u32 size) const
        {
            return refs_->within(address_, std::min<u32>(size, static_cast<u32>(bytes_.size())));
        }

    private:
        ItemRecord(const AddressSpace& space, const ReferenceIndex& refs, u32 address, std::span<const std::byte> bytes)
            : space_(&space)
            , refs_(&refs)
            , address_(address)
            , bytes_(bytes)
        {
        }

        const AddressSpace* space_;
        const ReferenceIndex* refs_;
        u32 address_;
        std::span<const std::byte> bytes_;
    };

} // namespace rct3::ovl
