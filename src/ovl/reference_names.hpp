// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helper for item readers: which item name a reference field holds.
#pragma once

#include "ovl/overlay.hpp"

#include <cstdint>
#include <string_view>
#include <unordered_map>

namespace rct3::ovl
{

    class ReferenceNames
    {
    public:
        /// Reads `part`'s reference table. A table that cannot be read leaves the map empty, so fields just
        /// have no names.
        ReferenceNames(const Part& part, const AddressSpace& space)
        {
            if (auto table = read_references(part.file, part.bytes, space))
            {
                for (const Reference& r : *table)
                {
                    if (!r.name.empty())
                        names_.emplace(r.location, split_symbol_name(r.name));
                }
            }
        }

        /// Name, without the tag, of the item the field at `location` refers to. Empty when the field has no
        /// reference or the reference names an item of another type than `tag`.
        [[nodiscard]] std::string_view find(std::uint32_t location, std::string_view tag) const noexcept
        {
            const auto it = names_.find(location);
            if (it == names_.end() || it->second.tag != tag)
                return { };
            return it->second.name;
        }

    private:
        std::unordered_map<std::uint32_t, SymbolName> names_;
    };

} // namespace rct3::ovl
