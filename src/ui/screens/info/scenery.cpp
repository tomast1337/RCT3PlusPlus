// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/info/scenery.hpp"

#include "save/model/park.hpp"
#include "strings/unicode.hpp"
#include "ui/screens/info/attraction.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>
#include <string>

namespace rct3::ui::screens::info
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
        constexpr std::string_view kRoomName = "ChangingRoomName";
        constexpr std::string_view kPeopleIn = "PeopleInPoolComplex"; ///< Followed by how many, up to 4, or X
        constexpr std::string_view kTicket = "AttractionFinancesDialogLabel_AdmissionPrice";
        constexpr std::string_view kPerHour = "AttractionNumberArea_PerHour";
        constexpr std::string_view kDirt = "PoolDirt_Label";
        constexpr std::array<std::string_view, 6> kDirtSteps { "PoolDirt_Clean", "PoolDirt_SlightlyDirty", "PoolDirt_Dirty", "PoolDirt_VeryDirty", "PoolDirt_Disgusting", "PoolDirt_HealthHazard" };
        constexpr std::string_view kFluid = "Fluid_TypeLabel";
        constexpr std::array<std::string_view, 3> kFluids { "Fluid_ZaniClenz", "Fluid_MegaKleen", "Fluid_ToxiScrub" }; // By kPoolFluidControl's value
        constexpr std::string_view kInspection = "RideMaintenanceDialogLabel_Inspection";
        constexpr std::array<std::string_view, 7> kInspections { "RideInspectionIntervalEvery10Mins", "RideInspectionIntervalEvery20Mins", "RideInspectionIntervalEvery30Mins", "RideInspectionIntervalEvery45Mins", "RideInspectionIntervalEveryHour", "RideInspectionIntervalEvery2Hours", "RideInspectionIntervalNever" };
        // The wave machines' settings: label and help, by control from kPoolWaveHeightControl on.
        constexpr std::array<std::array<std::string_view, 2>, 4> kWaveSettings { { { "WaveHeightText", "WaveHeightToolTip" }, { "WaveLengthText", "WaveLengthToolTip" }, { "WaveOnPeriodText", "WaveOnPeriodToolTip" }, { "WaveTotalPeriodText", "WaveTotalPeriodToolTip" } } };
        constexpr int kTicketStep = 10;
        constexpr float kPercent = 100;
        constexpr int kSecondsPerMinute = 60;
        constexpr float kMaintenanceWidth = 440; ///< As wide as a ride's maintenance page
        /// Slot 1 is the colours tab's, kept even when a prop takes no colours; what a prop does goes on from the next.
        constexpr int kColourSlot = 1;
        constexpr std::int32_t kColourCount = 32;
        constexpr int kFirstBehaviourSlot = 2;

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

        /// `words` with its number mark standing for `n`.
        std::u16string numbered(std::u16string_view words, std::int64_t n)
        {
            std::u16string out(words);
            if (const auto mark = out.find(u'#'); mark != std::u16string::npos)
                out.replace(mark, 1, ascii(std::to_string(n)));
            return out;
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
        // A changing room's tabs sit where an attraction's of the same kind do; its construction tab opens the pool
        // panel, as a tracked ride's opens its track's.
        if (facts.pool)
            return {
                { SceneryTab::RoomDetails, 0, "Guest_Rides", "AttractionDetails_ViewHelp", false, { } },
                { SceneryTab::RoomFinances, 1, "Attraction_Finances", "AttractionDetails_FinanceHelp", false, { } },
                { SceneryTab::RoomThoughts, 2, "Guest_Thoughts", "AttractionDetails_ThoughtsHelp", false, { } },
                { SceneryTab::RoomState, 4, facts.pool->open ? "Attraction_General_Open" : "Attraction_General_Closed", "AttractionDetails_StateChooserHelp", false, { } },
                { SceneryTab::RoomMode, 6, "Attraction_OpMode", "AttractionDetails_RideOpModeHelp", false, { } },
                { SceneryTab::RoomConstruction, 7, "Attraction_Construction", "AttractionDetails_ConstructionHelp", true, { } },
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
            const save::model::BinState& bin = *facts.bin;
            const float full = bin.capacity > 0 ? std::clamp(bin.contents / bin.capacity, 0.0f, 1.0f) : 0.0f;
            page.lines.push_back({ std::u16string(strings.get(kAmountFull)), ascii(std::format("{}%", std::lround(full * kPercent))), false, std::string(kAmountFull) });
            std::u16string emptied(strings.get(kNeverEmptied));
            if (bin.last_emptied)
            {
                const save::model::CalendarDate date = save::model::GameClock::date_at(*bin.last_emptied, facts.zero_time);
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
            const std::u16string complex = pool.name.empty() ? std::u16string(strings.get(kPoolDefaultName)) : pool.name;
            page.title = complex;
            const auto text = [&](std::string_view key) { return std::u16string(strings.get(key)); };
            const auto money = [&](std::int64_t amount) { return money_text(pool.currency, units::Money { amount }); };
            const auto line = [&](std::string_view key, std::u16string value) { page.lines.push_back({ text(key), std::move(value), false, std::string(key) }); };
            const auto button = [&](int id, std::string_view icon, std::string_view help, bool beside, bool enabled = true) { page.controls.push_back({ .id = id, .name = std::string(icon), .kind = InfoControlKind::Button, .icon = std::string(icon), .enabled = enabled, .beside = beside, .help = text(help) }); };
            const auto choice = [&](int id, std::string_view key, std::span<const std::string_view> items, int value, bool beside = false) {
                InfoControl& c = page.controls.emplace_back();
                c.id = id;
                c.name = key;
                c.kind = InfoControlKind::Choice;
                c.label = text(key);
                for (const std::string_view i : items)
                    c.items.push_back(text(i));
                c.value = value;
                c.beside = beside;
            };
            if (tab == SceneryTab::RoomDetails)
            {
                // The room's name beside the complex's, its state under them, then a row of buttons. Open, the state
                // tells how many are in the complex, as a ride's tells how many ride; the game words it for none to
                // four people, and for more.
                // shortcut: the room keeps its kind's name and number, its box greyed; let it be typed over once rooms
                // keep a name of their own in the park's file.
                const std::u16string room = scenery_name(facts, strings) + ascii(std::format(" {}", pool.number));
                page.controls.push_back({ .id = 0, .name = std::string(kRoomName), .kind = InfoControlKind::Text, .maximum = kLongestAttractionName, .text = room, .enabled = false, .width = kNameWidth });
                page.controls.push_back({ .id = kPoolNameControl, .name = std::string(kPoolName), .kind = InfoControlKind::Text, .maximum = kLongestAttractionName, .text = complex, .beside = true, .width = kNameWidth, .help = text(kPoolName) });
                const std::string people = pool.people <= 4 ? std::format("{}{}", kPeopleIn, pool.people) : std::format("{}X", kPeopleIn);
                page.controls.push_back({ .name = "status", .kind = InfoControlKind::Words, .label = pool.open ? numbered(strings.get(people), pool.people) : text("AttractionStatusClosed") });
                button(kPoolDeleteRoomControl, "Attraction_General_Delete", "TT_AttractionDialogIconDestroyChangingRoomButton", false);
                button(kPoolDeleteComplexControl, "GUI2_DeleteSelection", "TT_AttractionDialogIconDestroyPoolComplexButton", true);
                // shortcut: a complex cannot be kept as a design yet, so its button is greyed; enable it once pool
                // complexes can be saved and placed again.
                button(kPoolSaveControl, "File_SaveGame", "TT_AttractionDialogIconSaveButton", true, false);
                button(kPoolLocateControl, "CenterInViewIcon", "TT_AttractionDialogIconFocusCameraButton", true);
            }
            else if (tab == SceneryTab::RoomFinances)
            {
                // The price over the sums, as the player sets it first, shown in money with only its arrows.
                // shortcut: the room is not measured an hour at a time and has no running cost, so the projections
                // are nothing an hour; reckon them once pools keep a rate of customers and a running cost.
                const auto per_hour = [&](std::int64_t amount) {
                    std::u16string words = text(kPerHour);
                    const auto mark = words.find(u'#');
                    return mark == std::u16string::npos ? money(amount) : words.replace(mark, 1, money(amount));
                };
                const auto ticket = static_cast<int>(std::clamp<std::int64_t>(pool.ticket, 0, kMostPoolTicket));
                page.controls.push_back({ .id = kPoolTicketControl, .name = std::string(kTicket), .kind = InfoControlKind::Number, .label = text(kTicket), .value = ticket, .maximum = kMostPoolTicket, .step = kTicketStep, .decimals = 2, .shown = money(ticket) });
                page.controls_after = 0;
                line("AttractionFinancesDialogLabel_Income", per_hour(0));
                line("AttractionFinancesDialogLabel_CostPerInspection", money(pool.refill));
                line("AttractionFinancesDialogLabel_ProjectedRunningCost", per_hour(0));
                line("AttractionFinancesDialogLabel_Profit", per_hour(0));
                line("AttractionFinancesDialogLabel_TotalProfit", money(pool.income));
            }
            else if (tab == SceneryTab::RoomThoughts)
            {
                // With none yet, an empty line keeps the paper the thoughts go on.
                add_thoughts(page, pool.thoughts, strings);
                if (page.lines.empty())
                    page.lines.push_back({ });
            }
            else if (tab == SceneryTab::RoomMode)
            {
                // The wave's height and length on bars, and the machines' times, which only count with a machine in
                // the pool, in words with their arrows.
                const auto setting = [&](int id) { return kWaveSettings[static_cast<std::size_t>(id - kPoolWaveHeightControl)]; };
                const auto bar = [&](int id, int value, int least, int most) {
                    const auto [label, help] = setting(id);
                    page.controls.push_back({ .id = id, .name = std::string(label), .kind = InfoControlKind::Bar, .label = text(label), .value = std::clamp(value, least, most), .minimum = least, .maximum = most, .help = text(help) });
                };
                const auto timed = [&](int id, int value, int least, int most, int step, std::string_view one, std::string_view many) {
                    const auto [label, help] = setting(id);
                    value = std::clamp(value, least, most);
                    page.controls.push_back({ .id = id, .name = std::string(label), .kind = InfoControlKind::Number, .label = text(label), .value = value, .minimum = least, .maximum = most, .step = step, .shown = numbered(strings.get(value == 1 ? one : many), value), .enabled = pool.machines, .help = text(help) });
                };
                const auto least_frequency = static_cast<int>(park::pools::kLeastWaveFrequency);
                const auto most_frequency = static_cast<int>(park::pools::kMostWaveFrequency);
                bar(kPoolWaveHeightControl, static_cast<int>(std::lround(pool.waves.height)), static_cast<int>(park::pools::kLeastWaveHeight), static_cast<int>(park::pools::kMostWaveHeight));
                bar(kPoolWaveLengthControl, least_frequency + most_frequency - static_cast<int>(std::lround(pool.waves.frequency)), least_frequency, most_frequency);
                timed(kPoolWaveOnControl, static_cast<int>(std::lround(pool.waves.on)), 0, kMostWaveOn, kWaveOnStep, "OneSecond", "ManySeconds");
                timed(kPoolWaveEveryControl, static_cast<int>(std::lround(pool.waves.every / kSecondsPerMinute)), 1, kMostWaveEvery, 1, "OneMinute", "ManyMinutes");
            }
            else if (tab == SceneryTab::RoomCustomers)
            {
                // shortcut: rooms are not measured an hour at a time, nor rated, nor dated, so the rate, popularity
                // and satisfaction are nothing and the room was built this year; fill them in as pools gain them.
                line("AttractionCustomersDialogLabel_Customers", numbered(strings.get(kPerHour), 0));
                line("AttractionCustomersDialogLabel_Popularity", u"0%");
                line("AttractionCustomersDialogLabel_Satisfaction", u"0%");
                page.lines.push_back({ });
                line("AttractionCustomersDialogLabel_TotalCustomers", ascii(std::to_string(pool.customers)));
                line("AttractionCustomersDialogLabel_AgeYears", text("ZeroYearsAgo"));
            }
            else if (tab == SceneryTab::RoomMaintenance)
            {
                // Reliability on its bar over the inspections and the fluid, then how long since the last inspection
                // and how clean the water is, with a mechanic to call beside them.
                line("RideMaintenanceDialogLabel_Reliability", ascii(std::format("{}%", std::lround(pool.reliability * kPercent))));
                page.lines.back().bar = std::clamp(pool.reliability, 0.0f, 1.0f);
                page.controls_after = 1;
                page.width = kMaintenanceWidth;
                choice(kPoolInspectionControl, kInspection, kInspections, pool.inspection);
                choice(kPoolFluidControl, kFluid, kFluids, pool.fluid);
                button(kPoolMechanicControl, "Staff_JobIsMechanicIcon", "TT_RideMaintenanceDialog_MechanicButton", true);
                line("RideMaintenanceDialogLabel_TimeSinceLastInspection", ascii(std::to_string(std::lround(pool.since_inspection / kSecondsPerMinute))));
                line(kDirt, text(kDirtSteps[std::min(pool.dirt, kDirtSteps.size() - 1)]));
            }
        }
        return page;
    }

} // namespace rct3::ui::screens::info
