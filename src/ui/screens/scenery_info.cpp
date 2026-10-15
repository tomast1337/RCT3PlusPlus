// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/scenery_info.hpp"

#include "save/model/park.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <cmath>
#include <format>
#include <string>

namespace rct3::ui
{

    namespace
    {

        constexpr std::string_view kNamePrefix = "SceneryItem_"; ///< Followed by the asset's symbol
        constexpr std::string_view kMonth = "Month";             ///< Followed by the month's number from 0
        constexpr std::string_view kYear = "Year";
        constexpr std::string_view kAmountFull = "Bin_AmountFull";
        constexpr std::string_view kLastEmptied = "Bin_LastEmptied";
        constexpr std::string_view kNeverEmptied = "Bin_NeverEmptied";
        constexpr std::string_view kSignOptions = "Sign_Options";
        constexpr std::string_view kNoEntry = "Sign_NoEntry";
        constexpr float kPercent = 100;
        /// Slot 1 is the colours tab's, kept even when a prop takes no colours; what a prop does goes on from the next.
        constexpr int kColourSlot = 1;
        constexpr std::int32_t kColourCount = 32;
        constexpr int kFirstBehaviourSlot = 2;

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

    } // namespace

    std::vector<std::size_t> scenery_paint_slots(const SceneryFacts& facts)
    {
        std::vector<std::size_t> slots;
        for (std::size_t s = 0; s < facts.colours.size(); ++s)
        {
            if ((facts.colour_slots & (1U << s)) != 0)
                slots.push_back(s);
        }
        return slots;
    }

    std::vector<SceneryTabInfo> scenery_tabs(const SceneryFacts& facts)
    {
        std::vector<SceneryTabInfo> tabs { { SceneryTab::Destroy, 0, "Attraction_General_Delete", "ATE_TT_Demolish", true, { } } };
        if (const std::vector<std::size_t> slots = scenery_paint_slots(facts); !slots.empty())
        {
            tabs.push_back({ SceneryTab::Colour, kColourSlot, "Attraction_Colour", "Scenery_SetFlexiColours", false, { } });
            SceneryTabInfo& colour = tabs.back();
            for (const std::size_t s : slots)
            {
                // A colour out of the range is shown as the first.
                const std::int32_t c = facts.colours.at(s);
                colour.paints.push_back(c >= 0 && c < kColourCount ? static_cast<std::size_t>(c) : 0);
            }
        }
        int slot = kFirstBehaviourSlot;
        if (facts.bin != nullptr)
            tabs.push_back({ SceneryTab::Bin, slot++, "Guest_Stats", "Bin_Statistics", false, { } });
        if (facts.sign != nullptr)
            tabs.push_back({ SceneryTab::Sign, slot++, "Obj_Scenery", kSignOptions, false, { } });
        return tabs;
    }

    std::u16string scenery_name(const SceneryFacts& facts, const StringTable& strings)
    {
        if (facts.symbol.empty())
            return { };
        return std::u16string(strings.get_ignoring_case(std::string(kNamePrefix) + std::string(facts.symbol)));
    }

    InfoPage scenery_page(std::optional<SceneryTab> tab, const SceneryFacts& facts, const StringTable& strings)
    {
        InfoPage page;
        page.title = scenery_name(facts, strings);
        if (tab == SceneryTab::Bin && facts.bin != nullptr)
        {
            const save::BinState& bin = *facts.bin;
            const float full = bin.capacity > 0 ? std::clamp(bin.contents / bin.capacity, 0.0f, 1.0f) : 0.0f;
            page.lines.push_back({ std::u16string(strings.get(kAmountFull)), ascii(std::format("{}%", std::lround(full * kPercent))), false });
            std::u16string emptied(strings.get(kNeverEmptied));
            if (bin.last_emptied)
            {
                const save::CalendarDate date = save::GameClock::split(*bin.last_emptied);
                emptied = std::u16string(strings.get(std::format("{}{}", kMonth, date.month)));
                emptied += u", ";
                emptied += strings.get(kYear);
                emptied += ascii(std::format(" {}", date.year + 1));
            }
            page.lines.push_back({ std::u16string(strings.get(kLastEmptied)), std::move(emptied), false });
        }
        else if (tab == SceneryTab::Sign && facts.sign != nullptr)
        {
            page.heading = strings.get(kSignOptions);
            const std::u16string words = facts.sign->kind == 1 ? std::u16string(strings.get(kNoEntry)) : strings::to_utf16(facts.sign->text);
            page.lines.push_back({ words, { }, false });
        }
        return page;
    }

} // namespace rct3::ui
