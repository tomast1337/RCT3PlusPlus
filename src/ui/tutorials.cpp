// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/tutorials.hpp"

#include "strings/unicode.hpp"

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kStart = "Tutorial";
        constexpr std::string_view kExpansionStart = "Tutorial_AOP";
        constexpr std::string_view kName = "List_Name";
        constexpr std::string_view kDescription = "List_Description";
        constexpr std::string_view kIcon = "Icon";
        constexpr std::string_view kFolder = "Tutorials/";
        constexpr std::string_view kAlternate = "_Alternate";
        constexpr std::string_view kExtension = ".dat";
        constexpr int kMostTutorials = 99;
        constexpr std::size_t kExtras = 8;          ///< The original game's tutorial the expansions change
        constexpr std::size_t kAlternateExtras = 0; ///< The expansion that gives it another park
        constexpr std::size_t kWithoutExtras = 1;   ///< The one that leaves it out

        bool installed(std::span<const bool> expansions, std::size_t n)
        {
            return n < expansions.size() && expansions[n];
        }
    } // namespace

    std::vector<TutorialInfo> tutorials(const StringTable& strings, int pack, std::span<const bool> expansions)
    {
        const std::string start = pack <= 0 ? std::string(kStart) : std::string(kExpansionStart) + std::to_string(pack - 1) + "_";
        std::vector<TutorialInfo> out;
        for (int n = 1; n <= kMostTutorials; ++n)
        {
            const std::string number = std::to_string(n);
            const std::u16string_view name = strings.get(start + std::string(kName) + number);
            if (name.empty())
                break;
            const auto count = static_cast<std::size_t>(n);
            const bool extras = pack <= 0 && count == kExtras;
            if (extras && installed(expansions, kWithoutExtras))
                break;
            TutorialInfo t;
            t.name = name;
            t.description = strings.get(start + std::string(kDescription) + number);
            t.icon = strings::to_utf8(strings.get(start + std::string(kIcon) + number));
            t.park = std::string(kFolder) + start + number + (extras && installed(expansions, kAlternateExtras) ? std::string(kAlternate) : std::string { }) + std::string(kExtension);
            out.push_back(std::move(t));
        }
        return out;
    }

} // namespace rct3::ui
