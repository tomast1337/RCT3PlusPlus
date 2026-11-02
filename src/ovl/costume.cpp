// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/costume.hpp"

#include "ovl/field_values.hpp"
#include "ovl/pointer_fields.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTag = "ent";
        constexpr std::size_t kSize = 0x18;
        constexpr std::uint32_t kMaxSections = 16; ///< Costumes in the install have five

        std::optional<Costume> read_costume(const AddressSpace& space, const PointerFields& pointers, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kSize);
            if (!raw)
                return std::nullopt;
            const auto b = *raw;
            const std::uint32_t count = fields::u32(b, 0x0c);
            const std::uint32_t slots = fields::u32(b, 0x10);
            if (count > kMaxSections || (count != 0 && !space.bytes_at(slots, std::size_t { count } * 4)))
                return std::nullopt;
            Costume c;
            c.name = fields::string_field(space, pointers, address + 0x14, fields::u32(b, 0x14));
            for (std::uint32_t i = 0; i < count; ++i)
            {
                const std::uint32_t at = slots + i * 4;
                c.sections.push_back(fields::string_field(space, pointers, at, fields::u32(*space.bytes_at(at, 4), 0)));
            }
            return c;
        }

    } // namespace

    std::vector<CostumeEntry> read_costumes(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const PointerFields pointers(overlay);
        std::vector<CostumeEntry> out;
        for (const auto& symbol : part.symbols)
        {
            const SymbolName parts = split_symbol_name(symbol.name);
            if (symbol.is_pointer && parts.tag == kTag)
                out.push_back({ parts.name, read_costume(space, pointers, symbol.data) });
        }
        return out;
    }

} // namespace rct3::ovl
