// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/string_format.hpp"

#include <algorithm>

namespace rct3::ui
{

    std::u16string format_string(std::u16string_view pattern, std::span<const FormatArg> args)
    {
        std::u16string out;
        out.reserve(pattern.size());
        for (std::size_t i = 0; i < pattern.size(); ++i)
        {
            const char16_t c = pattern[i];
            if ((c == u'{' || c == u'}') && i + 1 < pattern.size() && pattern[i + 1] == c)
            {
                out += c;
                ++i;
                continue;
            }
            if (c == u'{')
            {
                const std::size_t close = pattern.find(u'}', i + 1);
                if (close != std::u16string_view::npos)
                {
                    const std::u16string_view name = pattern.substr(i + 1, close - i - 1);
                    const auto arg = std::ranges::find(args, name, &FormatArg::name);
                    if (arg != args.end())
                    {
                        out += arg->value;
                        i = close;
                        continue;
                    }
                }
            }
            out += c;
        }
        return out;
    }

} // namespace rct3::ui
