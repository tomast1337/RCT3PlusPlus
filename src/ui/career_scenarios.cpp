// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/career_scenarios.hpp"

#include "strings/unicode.hpp"

#include <array>
#include <cstdio>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kPlayer = "PlayerProfile_Player";
        constexpr int kMostScenarios = 99; ///< The keys have two digits
    } // namespace

    std::vector<std::string> career_scenario_ids(const StringTable& strings, int pack)
    {
        std::vector<std::string> ids;
        if (pack < 0)
            return ids;
        const std::string prefix = pack == 0 ? std::string { } : "AOP" + std::to_string(pack - 1) + "_";
        for (int i = 1; i <= kMostScenarios; ++i)
        {
            std::array<char, 32> key { };
            std::snprintf(key.data(), key.size(), "Scenario%02d_Lookup", i);
            const std::u16string_view id = strings.get(prefix + key.data());
            if (id.empty())
                break;
            ids.push_back(strings::to_utf8(id));
        }
        return ids;
    }

    std::u16string default_career_name(const StringTable& strings, std::size_t number)
    {
        return std::u16string(strings.get(kPlayer)) + u" " + strings::to_utf16(std::to_string(number));
    }

} // namespace rct3::ui
