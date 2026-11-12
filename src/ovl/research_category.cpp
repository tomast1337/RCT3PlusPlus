// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/research_category.hpp"

#include "ovl/field_values.hpp"
#include "ovl/reference_names.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kCategoryTag = "rcg";
        constexpr std::string_view kTextTag = "txt";
        constexpr std::size_t kSize = 40;
        constexpr std::size_t kRowSize = 12;
        // Real categories have at most two prerequisites.
        constexpr std::uint32_t kMaxPrerequisites = 1u << 8;

        std::optional<ResearchCategory> read_category(const AddressSpace& space, const ReferenceNames& refs, const PointerFields& pointers, std::uint32_t address)
        {
            const auto raw = space.bytes_at(address, kSize);
            if (!raw)
                return std::nullopt;
            const auto b = *raw;

            ResearchCategory c;
            c.kind = fields::u32(b, 0x00);
            c.name = fields::string_field(space, pointers, address + 4, fields::u32(b, 0x04));
            c.name_text = refs.find(address + 8, kTextTag);
            c.start_level = fields::f32(b, 0x0c);
            c.develop_cost = fields::f32(b, 0x10);

            const std::uint32_t count = fields::u32(b, 0x14);
            if (count > kMaxPrerequisites)
                return std::nullopt;
            if (count != 0)
            {
                // Each row: the category, a word that is -1 in files seen, and the level as a float.
                const std::uint32_t rows = fields::u32(b, 0x18);
                const auto all = space.bytes_at(rows, std::size_t { count } * kRowSize);
                if (!all)
                    return std::nullopt;
                c.prerequisites.reserve(count);
                for (std::uint32_t i = 0; i < count; ++i)
                {
                    const std::uint32_t at = rows + i * static_cast<std::uint32_t>(kRowSize);
                    const std::size_t off = std::size_t { i } * kRowSize;
                    c.prerequisites.push_back(
                        {
                            .category = refs.find(at, kCategoryTag),
                            .unknown = fields::i32(*all, off + 4),
                            .level = fields::f32(*all, off + 8),
                        });
                }
            }
            return c;
        }

    } // namespace

    std::vector<ResearchCategoryEntry> read_research_categories(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);
        const PointerFields pointers(overlay);

        std::vector<ResearchCategoryEntry> categories;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kCategoryTag)
                continue;
            categories.push_back({ parts.name, read_category(space, refs, pointers, symbol.data) });
        }
        return categories;
    }

} // namespace rct3::ovl
