// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/career_scenarios.hpp"

#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdio>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kPlayer = "PlayerProfile_Player";
        constexpr int kMostScenarios = 99; ///< The keys have two digits
        constexpr std::string_view kPackFolder = "AOP_FSName_";
        constexpr std::string_view kParks = "Campaigns/Base";
        constexpr std::string_view kSlideshow = "Slideshow";

        /// The expansion's own folder name, with a slash after it; empty for the original game.
        std::string pack_folder(const StringTable& strings, int pack)
        {
            if (pack <= 0)
                return { };
            const std::u16string_view name = strings.get(std::string(kPackFolder) + std::to_string(pack - 1));
            return name.empty() ? std::string { } : strings::to_utf8(name) + "/";
        }

        std::string lower(std::string_view s)
        {
            std::string out(s);
            std::ranges::transform(out, out.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            return out;
        }
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

    CareerScenarioInfo career_scenario(const StringTable& strings, int pack, std::size_t index, std::string_view id)
    {
        const std::string i = std::string(id);
        const std::string number = std::to_string(index + 1);
        CareerScenarioInfo info;
        info.id = i;
        info.title = strings.get("Scenario" + i + "_Title");
        info.description = strings.get("Scenario" + i + "_Text");
        info.icon = "ScenarioIcon_" + i;
        info.picture = pack == 0 ? "CareerMap_LargeImage" + number : "CareerMap_LargeImage_" + i;
        info.picture_front = pack == 0 ? "CareerMap_LargeImageForeground" + number : "CareerMap_LargeImageFG_" + i;
        info.slideshow = std::string(kSlideshow) + "/" + pack_folder(strings, pack) + "Scenario" + number;
        info.file_part = strings::to_utf8(strings.get(i + "_Filename"));
        return info;
    }

    std::string career_park_folder(const StringTable& strings, int pack)
    {
        std::string folder = std::string(kParks) + "/" + pack_folder(strings, pack);
        if (folder.ends_with('/'))
            folder.pop_back();
        return folder;
    }

    std::optional<std::size_t> find_career_park(std::span<const std::filesystem::path> files, std::string_view part)
    {
        if (part.empty())
            return std::nullopt;
        const std::string wanted = lower(part);
        for (std::size_t i = 0; i < files.size(); ++i)
        {
            if (lower(files[i].filename().string()).find(wanted) != std::string::npos)
                return i;
        }
        return std::nullopt;
    }

    std::u16string default_career_name(const StringTable& strings, std::size_t number)
    {
        return std::u16string(strings.get(kPlayer)) + u" " + strings::to_utf16(std::to_string(number));
    }

} // namespace rct3::ui
