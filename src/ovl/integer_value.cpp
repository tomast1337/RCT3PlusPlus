// SPDX-License-Identifier: GPL-3.0-or-later
#include "ovl/integer_value.hpp"

namespace rct3::ovl
{

    namespace
    {
        constexpr std::string_view kIntegerTag = "int";
    }

    std::vector<IntegerItem> read_integers(const Overlay& /*overlay*/, const Part& part)
    {
        std::vector<IntegerItem> items;
        for (const auto& symbol : part.symbols)
        {
            if (symbol.is_pointer)
                continue;
            const SymbolName parts = split_symbol_name(symbol.name);
            if (parts.tag != kIntegerTag)
                continue;
            items.push_back({ parts.name, static_cast<std::int32_t>(symbol.data) });
        }
        return items;
    }

} // namespace rct3::ovl
