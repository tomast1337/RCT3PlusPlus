// SPDX-License-Identifier: GPL-3.0-or-later
//
// Internal helper for item readers: which 32-bit fields of a part are patched with an address when the
// file loads. A pointer field holding 0 is a real pointer to the first byte of the shared address
// space only when it is in this set, otherwise it is a null pointer.
#pragma once

#include "ovl/overlay.hpp"

#include <cstdint>
#include <unordered_set>

namespace rct3::ovl
{

    class PointerFields
    {
    public:
        /// Collects the relocated fields of both parts: a pointer can sit in either one, whichever part the
        /// structure it belongs to is in.
        explicit PointerFields(const Overlay& overlay)
        {
            add(overlay.common());
            if (const auto unique = overlay.unique())
                add(*unique);
        }

        /// True when the field at `address` is a relocated pointer.
        [[nodiscard]] bool contains(std::uint32_t address) const noexcept
        {
            return fields_.contains(address);
        }

    private:
        void add(const Part& part)
        {
            fields_.reserve(fields_.size() + part.file.relocations.size());
            for (const std::uint32_t location : part.file.relocations)
                fields_.insert(part.base + location);
            // Stream relocations name locations in the shared address space already.
            for (const auto& table : part.file.stream_relocations)
            {
                for (const StreamRelocation& r : table)
                    fields_.insert(r.location);
            }
        }

        std::unordered_set<std::uint32_t> fields_;
    };

} // namespace rct3::ovl
