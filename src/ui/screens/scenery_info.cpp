// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/scenery_info.hpp"

#include "save/model/park.hpp"
#include "strings/unicode.hpp"
#include "ui/screens/attraction_info.hpp"

#include <algorithm>
#include <array>
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
        constexpr std::string_view kPoolDefaultName = "PoolComplexDefaultName";
        constexpr std::string_view kPoolName = "PoolComplexName";
        constexpr std::string_view kPeopleIn = "PeopleInPoolComplex"; ///< Followed by how many, up to 4, or X
        constexpr std::string_view kTicket = "AttractionFinancesDialogLabel_AdmissionPrice";
        constexpr std::array<std::string_view, 2> kPoolStates { "AttractionStatusOpen", "AttractionStatusClosed" }; // By kPoolStateControl's value
        constexpr int kTicketStep = 10;
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
        // A changing room's tabs sit where an attraction's of the same kind do.
        if (facts.pool)
            return {
                { SceneryTab::RoomDetails, 0, "Guest_Rides", "AttractionDetails_ViewHelp", false, { } },
                { SceneryTab::RoomFinances, 1, "Attraction_Finances", "AttractionDetails_FinanceHelp", false, { } },
                { SceneryTab::RoomMode, 6, "Attraction_OpMode", "AttractionDetails_RideOpModeHelp", false, { } },
                { SceneryTab::RoomCustomers, 8, "Attraction_Customers", "AttractionDetails_CustomersHelp", false, { } },
                { SceneryTab::RoomMaintenance, 10, "Attraction_Maintenance", "AttractionDetails_RideMaintenanceHelp", false, { } },
            };
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
            page.lines.push_back({ std::u16string(strings.get(kAmountFull)), ascii(std::format("{}%", std::lround(full * kPercent))), false, std::string(kAmountFull) });
            std::u16string emptied(strings.get(kNeverEmptied));
            if (bin.last_emptied)
            {
                const save::CalendarDate date = save::GameClock::date_at(*bin.last_emptied, facts.zero_time);
                emptied = std::u16string(strings.get(std::format("{}{}", kMonth, date.month)));
                emptied += u", ";
                emptied += strings.get(kYear);
                emptied += ascii(std::format(" {}", date.year + 1));
            }
            page.lines.push_back({ std::u16string(strings.get(kLastEmptied)), std::move(emptied), false, std::string(kLastEmptied) });
        }
        else if (tab == SceneryTab::Sign && facts.sign != nullptr)
        {
            page.heading = strings.get(kSignOptions);
            const std::u16string words = facts.sign->kind == 1 ? std::u16string(strings.get(kNoEntry)) : strings::to_utf16(facts.sign->text);
            page.lines.push_back({ words, { }, false });
        }
        else if (facts.pool)
        {
            const PoolComplexFacts& pool = *facts.pool;
            page.title = pool.name.empty() ? std::u16string(strings.get(kPoolDefaultName)) : pool.name;
            const auto button = [&](int id, std::string_view icon, std::string_view help, bool beside, bool enabled = true) { page.controls.push_back({ .id = id, .name = std::string(icon), .kind = InfoControlKind::Button, .icon = std::string(icon), .enabled = enabled, .beside = beside, .help = std::u16string(strings.get(help)) }); };
            if (tab == SceneryTab::RoomDetails)
            {
                page.controls.push_back({ .id = kPoolNameControl, .name = std::string(kPoolName), .kind = InfoControlKind::Text, .maximum = kLongestAttractionName, .text = page.title, .width = kNameWidth, .help = std::u16string(strings.get(kPoolName)) });
                button(kPoolDeleteRoomControl, "Attraction_General_Delete", "TT_AttractionDialogIconDestroyChangingRoomButton", false);
                button(kPoolDeleteComplexControl, "GUI2_DeleteSelection", "TT_AttractionDialogIconDestroyPoolComplexButton", true);
                button(kPoolLocateControl, "CenterInViewIcon", "TT_AttractionDialogIconFocusCameraButton", true);
            }
            else if (tab == SceneryTab::RoomMode)
            {
                InfoControl& state = page.controls.emplace_back();
                state.id = kPoolStateControl;
                state.name = kPoolStates[0];
                state.kind = InfoControlKind::Choice;
                state.label = strings.get(kPoolStates[pool.open ? 0 : 1]);
                for (const std::string_view s : kPoolStates)
                    state.items.emplace_back(strings.get(s));
                state.value = pool.open ? 0 : 1;
            }
            else if (tab == SceneryTab::RoomFinances)
            {
                InfoControl& ticket = page.controls.emplace_back();
                ticket.id = kPoolTicketControl;
                ticket.name = kTicket;
                ticket.label = strings.get(kTicket);
                ticket.value = static_cast<int>(std::clamp<std::int64_t>(pool.ticket, 0, kMostPoolTicket));
                ticket.maximum = kMostPoolTicket;
                ticket.step = kTicketStep;
                ticket.decimals = 2;
            }
            else if (tab == SceneryTab::RoomCustomers)
            {
                // The game words it for none to four people, and for more.
                // shortcut: guests do not swim yet, so the count stays at none; count them once the simulation
                // takes guests into pool complexes.
                const std::string key = pool.people <= 4 ? std::format("{}{}", kPeopleIn, pool.people) : std::format("{}X", kPeopleIn);
                std::u16string words(strings.get(key));
                if (const auto mark = words.find(u'#'); mark != std::u16string::npos)
                    words.replace(mark, 1, ascii(std::to_string(pool.people)));
                page.lines.push_back({ std::move(words), { }, false, key });
            }
            else if (tab == SceneryTab::RoomMaintenance)
            {
                // shortcut: pools take no upkeep yet (dirt, cleaning fluids, inspections), so the mechanic's button
                // is greyed; show their state and enable it once the simulation keeps pools clean.
                button(kPoolMechanicControl, "Staff_JobIsMechanicIcon", "TT_RideMaintenanceDialog_MechanicButton", false, false);
            }
        }
        return page;
    }

} // namespace rct3::ui
