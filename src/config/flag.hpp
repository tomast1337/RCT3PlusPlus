// SPDX-License-Identifier: GPL-3.0-or-later
//
// How a yes/no value is written, wherever it comes from: anything but 0, false, no or off (any case) is on.
#pragma once

#include "strings/ascii.hpp"

#include <string_view>

namespace rct3::config
{

    [[nodiscard]] inline bool flag_value(std::string_view value)
    {
        for (std::string_view off : { "0", "false", "no", "off" })
        {
            if (strings::equals_ignoring_case(value, off))
                return false;
        }
        return true;
    }

} // namespace rct3::config
