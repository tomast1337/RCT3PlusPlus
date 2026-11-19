// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/gui_skin.hpp"

#include "core/types.hpp"
#include "ovl/reference_names.hpp"

#include <cstring>

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kSkinTag = "gsi";
        constexpr std::string_view kTextureTag = "tex";
        constexpr usize kItemSize = 16; // type, texture reference, coordinate pointer, one unused word

        u32 u32_at(std::span<const std::byte> b, usize at)
        {
            u32 v = 0;
            std::memcpy(&v, b.data() + at, sizeof v);
            return v;
        }

        // Number of 32-bit coordinates a kind stores, or nothing for kinds not seen in files.
        std::optional<usize> coordinate_count(u32 kind)
        {
            switch (static_cast<GuiSkinKind>(kind))
            {
                case GuiSkinKind::Rect:
                    return 4;
                case GuiSkinKind::WholeTexture:
                    return 0;
                case GuiSkinKind::NinePatch:
                case GuiSkinKind::Frame:
                    return 8;
                case GuiSkinKind::HorizontalThree:
                case GuiSkinKind::VerticalThree:
                    return 6;
            }
            return std::nullopt;
        }

        std::optional<GuiSkin> read_skin(const AddressSpace& space, const ReferenceNames& refs, u32 address)
        {
            const auto raw = space.bytes_at(address, kItemSize);
            if (!raw)
                return std::nullopt;

            GuiSkin skin;
            skin.kind = u32_at(*raw, 0);
            skin.texture = refs.find(address + 4, kTextureTag);

            const auto count = coordinate_count(skin.kind);
            if (!count)
                return std::nullopt;
            if (*count != 0)
            {
                const auto values = space.bytes_at(u32_at(*raw, 8), *count * 4);
                if (!values)
                    return std::nullopt;
                skin.coordinates.reserve(*count);
                for (usize i = 0; i < *count; ++i)
                {
                    skin.coordinates.push_back(static_cast<i32>(u32_at(*values, i * 4)));
                }
            }
            return skin;
        }

    } // namespace

    std::vector<GuiSkinItem> read_gui_skins(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        const ReferenceNames refs(part, space);

        std::vector<GuiSkinItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kSkinTag)
                continue;
            items.push_back({ parts.name, read_skin(space, refs, symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
