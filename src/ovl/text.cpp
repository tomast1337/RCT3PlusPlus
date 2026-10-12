// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/text.hpp"

namespace rct3::ovl
{

    namespace
    {

        constexpr std::string_view kTextTag = "txt";

    } // namespace

    std::vector<TextItem> read_texts(const Overlay& overlay, const Part& part)
    {
        const AddressSpace space = overlay.space();
        std::vector<TextItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (!symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kTextTag)
                continue;
            items.push_back({ parts.name, space.utf16_at(symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
