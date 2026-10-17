// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/attraction_info.hpp"

#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <span>

namespace rct3::ui
{

    namespace
    {
        constexpr std::string_view kStateClosedIcon = "Attraction_General_Closed";
        constexpr std::string_view kStateTestingIcon = "Attraction_General_Testing";
        constexpr std::string_view kStateOpenIcon = "Attraction_General_Open";

        constexpr std::array<AttractionTabInfo, 5> kCommonTabs { {
            { AttractionTab::View, 0, "Guest_Rides", "AttractionDetails_ViewHelp" },
            { AttractionTab::Finances, 1, "Attraction_Finances", "AttractionDetails_FinanceHelp" },
            { AttractionTab::Thoughts, 2, "Guest_Thoughts", "AttractionDetails_ThoughtsHelp" },
            { AttractionTab::State, 4, kStateClosedIcon, "AttractionDetails_StateChooserHelp" },
            { AttractionTab::Customers, 8, "Attraction_Customers", "AttractionDetails_CustomersHelp" },
        } };
        constexpr std::array<AttractionTabInfo, 4> kRideTabs { {
            { AttractionTab::Vehicles, 3, "Attraction_Vehicles", "AttractionDetails_RideVehiclesHelp" },
            { AttractionTab::Tests, 5, "Attraction_TestResults", "AttractionDetails_RideTestHelp" },
            { AttractionTab::Mode, 6, "Attraction_OpMode", "AttractionDetails_RideOpModeHelp" },
            { AttractionTab::Maintenance, 11, "Attraction_Maintenance", "AttractionDetails_RideMaintenanceHelp" },
        } };
        constexpr std::array<AttractionTabInfo, 2> kTrackTabs { {
            { AttractionTab::Construction, 7, "Attraction_Construction", "AttractionDetails_ConstructionHelp", true },
            { AttractionTab::Graphs, 10, "Attraction_Graphs", "AttractionDetails_GraphsHelp" },
        } };
        constexpr std::array<AttractionTabInfo, 2> kStallTabs { {
            { AttractionTab::Items, 3, "Stall_ItemsIcon", "AttractionDetails_StallItemsHelp" },
            { AttractionTab::Extras, 5, "Stall_ExtrasIcon", "AttractionDetails_StallExtrasHelp" },
        } };

        constexpr std::string_view kUnnamed = "TT_AttractionInstance_UnnamedAttraction";
        constexpr char kKeyMark = '#';                 ///< Round a name that is a string's key
        constexpr std::string_view kNumbered = "%ME%"; ///< Before the number of a name made from the kind's
        constexpr std::string_view kNameSuffix = "Name";
        /// Tried between a kind's asset name and kNameSuffix to find the string of its name.
        constexpr std::array<std::string_view, 3> kNameInfixes = { "", "RC", "Stall" };
        constexpr std::string_view kStallSuffix = "stall"; ///< Left off a stall's asset name as well

        /// The highest rating each word is used up to, then the word for anything higher.
        constexpr std::array<float, 6> kRatingSteps = { 2.55f, 5.11f, 7.67f, 10.23f, 12.79f, 30.0f };
        constexpr std::string_view kRatingWord = "RideRating";
        /// Thoughts shown at most, the latest first: as many as the page has room for.
        constexpr std::size_t kThoughtLines = 6;

        constexpr std::array<StateChoice, 3> kStateChoices { {
            { save::AttractionState::Open, "Attraction_Button_Open", "Attraction_Button_Open_Disabled", "TT_AttractionDialogIconOpenButton" },
            { save::AttractionState::Testing, "Attraction_Button_Testing", "Attraction_Button_Testing_Disabled", "TT_AttractionDialogIconTestingButton" },
            { save::AttractionState::Closed, "Attraction_Button_Closed", "Attraction_Button_Closed_Disabled", "TT_AttractionDialogIconClosedButton" },
        } };

        constexpr std::array<std::string_view, 4> kTestSections = { "RideTest_Basic", "RideTest_Speed", "RideTest_Force", "RideTest_Drop" };
        enum class TestSection : std::uint8_t
        {
            Basic,
            Speed,
            Force,
            Drop,
        };

        constexpr std::array<std::string_view, 16> kModeNames = {
            "RideOpModeDialog_OpModeContinuousCircuit", "RideOpModeDialog_OpModePoweredLaunch", "RideOpModeDialog_OpModeReverseInclinedLaunch", "RideOpModeDialog_OpModeBoat",      "RideOpModeDialog_OpModeRace", "RideOpModeDialog_OpModeShuttle", "RideOpModeDialog_OpModeUpwardsLaunch", "RideOpModeDialog_OpModeFreefallDrop", "RideOpModeDialog_OpModeUpAndDown", "RideOpModeDialog_OpModeMiniGolf", "RideOpModeDialog_OpModeSlide", "RideOpModeDialog_OpModeReverseCableLiftHill",
            "RideOpModeDialog_OpModeWaterTransport",    "RideOpModeDialog_OpModeAquarium",      "RideOpModeDialog_OpModeMultiplePoweredLaunch", "RideOpModeDialog_OpModeLazyRiver",
        };

        /// The inspection intervals in the order of the maintenance page's list, which is the order the save
        /// numbers them in.
        constexpr std::array<std::string_view, 7> kInspectionIntervals = {
            "RideInspectionIntervalEvery10Mins", "RideInspectionIntervalEvery20Mins", "RideInspectionIntervalEvery30Mins", "RideInspectionIntervalEvery45Mins", "RideInspectionIntervalEveryHour", "RideInspectionIntervalEvery2Hours", "RideInspectionIntervalNever",
        };
        /// The loads a train can wait for, in the order of the operating page's list, which is the order the
        /// save numbers them in.
        constexpr std::array<std::string_view, 5> kLoads = { "RideOpModeDialog_FullLoad", "RideOpModeDialog_3_4Load", "RideOpModeDialog_1_2Load", "RideOpModeDialog_1_4Load", "RideOpModeDialog_AnyLoad" };
        constexpr std::array<std::string_view, 2> kModeSections = { "AttractionDetails_RideOpModeHelp", "RideOpModeDialog_WaitFor" };
        constexpr std::string_view kSoldName = "%NAME%";

        constexpr float kGravity = 9.81f;
        constexpr float kKmhPerMs = 3.6f;
        constexpr float kMphPerMs = 2.2369363f;
        constexpr float kFeetPerMetre = 3.28084f;
        constexpr int kSecondsPerMinute = 60;
        constexpr float kPercent = 100;

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        std::u16string kind_name(std::string_view symbol, const StringTable& strings)
        {
            const std::string_view base = symbol.substr(0, symbol.find(':'));
            std::vector<std::string_view> bases { base };
            if (base.size() > kStallSuffix.size() && strings::equals_ignoring_case(base.substr(base.size() - kStallSuffix.size()), kStallSuffix))
                bases.push_back(base.substr(0, base.size() - kStallSuffix.size()));
            for (const std::string_view b : bases)
            {
                for (const std::string_view infix : kNameInfixes)
                {
                    if (const auto found = strings.get_ignoring_case(std::format("{}{}{}", b, infix, kNameSuffix)); !found.empty())
                        return std::u16string(found);
                }
            }
            return { };
        }

        std::u16string duration(float seconds)
        {
            const auto whole = static_cast<long>(std::lround(std::max(seconds, 0.0f)));
            return ascii(std::format("{}:{:02}", whole / kSecondsPerMinute, whole % kSecondsPerMinute));
        }

        std::u16string speed(float metres_per_second, DistanceUnits units)
        {
            switch (units)
            {
                case DistanceUnits::Imperial:
                    return ascii(std::format("{:.1f} mph", metres_per_second * kMphPerMs));
                case DistanceUnits::SI:
                    return ascii(std::format("{:.1f} m/s", metres_per_second));
                case DistanceUnits::Metric:
                    break;
            }
            return ascii(std::format("{:.1f} km/h", metres_per_second * kKmhPerMs));
        }

        std::u16string length(float metres, DistanceUnits units)
        {
            if (units == DistanceUnits::Imperial)
                return ascii(std::format("{:.0f} ft", metres * kFeetPerMetre));
            return ascii(std::format("{:.0f} m", metres));
        }

        std::u16string rating(float value, const StringTable& strings)
        {
            return ascii(std::format("{:.2f} (", value)) + rating_word(value, strings) + u")";
        }

        std::u16string force(float acceleration)
        {
            return ascii(std::format("{:.2f} g", acceleration / kGravity));
        }

        std::u16string yes_no(bool value, const StringTable& strings)
        {
            return text(strings, value ? "Yes" : "No");
        }

        std::u16string percent(float value)
        {
            return ascii(std::format("{} %", std::lround(value)));
        }

        std::u16string status(const save::AttractionCommon& a, const StringTable& strings)
        {
            if (a.repair_status != save::RepairStatus::Fine && a.repair_status != save::RepairStatus::AwaitingBreakdown)
                return text(strings, "AttractionStatusBrokenDown");
            switch (a.state)
            {
                case save::AttractionState::Open:
                    return text(strings, "AttractionStatusOpen");
                case save::AttractionState::Testing:
                    return text(strings, "AttractionStatusTesting");
                case save::AttractionState::Closed:
                    break;
            }
            return text(strings, "AttractionStatusClosed");
        }

        std::u16string breakdown(save::BreakdownKind kind, const StringTable& strings)
        {
            constexpr std::array<std::string_view, 9> kKeys = {
                "RideBreakdownTypeNone", "RideBreakdownTypeEmergencyStop", "RideBreakdownTypeCarRestraintsFailToOpen", "RideBreakdownTypeCarRestraintsFailToClose", "RideBreakdownTypeCarDoorsFailToOpen", "RideBreakdownTypeCarDoorsFailToClose", "RideBreakdownTypeTrainStuckLeavingStation", "RideBreakdownTypeStationBrakesFail", "RideBreakdownTypeControlFailure",
            };
            const auto i = static_cast<std::size_t>(kind);
            return i < kKeys.size() ? text(strings, kKeys[i]) : std::u16string { };
        }

        void add(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value)
        {
            page.lines.push_back({ text(strings, label), std::move(value) });
        }

        /// A line sharing its row with the next such line.
        void add_half(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value)
        {
            page.lines.push_back({ text(strings, label), std::move(value), true });
        }

        std::u16string missing()
        {
            return std::u16string(kMissingValue);
        }

        std::u16string count(std::int64_t n)
        {
            return ascii(std::to_string(n));
        }

        /// The pots of the attraction's colours, when it has any.
        void paints(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            if (f.colours.empty())
                return;
            InfoControl c;
            c.id = control_id(AttractionControl::Colours);
            c.kind = InfoControlKind::Paint;
            c.label = text(strings, "AttractionVehicleDialogHelpPaintAttraction");
            c.paints = f.colours;
            page.controls.push_back(std::move(c));
        }

        InfoControl price(int id, std::u16string label, std::int64_t value, int most)
        {
            InfoControl c;
            c.id = id;
            c.kind = InfoControlKind::Number;
            c.label = std::move(label);
            c.value = static_cast<int>(std::clamp<std::int64_t>(value, 0, most));
            c.maximum = most;
            c.step = kPriceStep;
            c.decimals = 2;
            return c;
        }

        void finances(InfoPage& page, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            // The projections are the game's own reckoning per hour, which the save does not keep.
            add(page, strings, "AttractionFinancesDialogLabel_Income", missing());
            add(page, strings, "AttractionFinancesDialogLabel_Profit", missing());
            add(page, strings, "AttractionFinancesDialogLabel_TotalProfit", money_text(units.currency, f.common->total_profit));
            add(page, strings, f.ride ? "AttractionFinancesDialogLabel_ProjectedRunningCost" : "AttractionFinancesDialogLabel_RunningCost", money_text(units.currency, f.common->running_cost));
            if (f.ride == nullptr)
                return;
            const bool ride = f.ride->kind != save::RideKind::Special;
            if (ride)
                add(page, strings, "AttractionFinancesDialogLabel_CostPerInspection", missing());
            page.controls.push_back(price(control_id(AttractionControl::Price), text(strings, "AttractionFinancesDialogLabel_AdmissionPrice"), f.ride->ticket_price, ride ? kMostRidePrice : kMostPrice));
        }

        std::u16string item_name(const save::StallItem& item, const save::Stall& stall, const StringTable& strings)
        {
            return item.name.starts_with(kNumbered) || item.name.empty() ? kind_name(stall.base.symbol, strings) : strings::to_utf16(item.name);
        }

        void customers(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::AttractionCommon& a = *f.common;
            // Customers an hour and the age are reckoned by the game from its clock; neither is saved.
            add_half(page, strings, "AttractionCustomersDialogLabel_Customers", missing());
            add_half(page, strings, "AttractionCustomersDialogLabel_Popularity", percent(a.popularity * kPercent));
            add_half(page, strings, "AttractionCustomersDialogLabel_Satisfaction", percent(a.satisfaction * kPercent));
            add_half(page, strings, "AttractionCustomersDialogLabel_TotalCustomers", count(a.total_guests));
            add_half(page, strings, "AttractionCustomersDialogLabel_AgeYears", missing());
            if (f.ride && !f.ride->stations.empty())
            {
                float longest = 0;
                for (const save::Station& s : f.ride->stations)
                    longest = std::max(longest, s.queue_time);
                add_half(page, strings, "AttractionCustomersDialogLabel_QueueTime", duration(longest));
                add_half(page, strings, "AttractionCustomersDialogLabel_FavouriteRide", missing());
            }
            if (f.stall == nullptr)
                return;
            const std::u16string sold = text(strings, "AttractionCustomersDialogLabel_Sold");
            for (const save::StallItem& item : f.stall->items)
            {
                std::u16string label = sold;
                if (const auto at = label.find(ascii(kSoldName)); at != std::u16string::npos)
                    label.replace(at, kSoldName.size(), item_name(item, *f.stall, strings));
                page.lines.push_back({ std::move(label), count(item.total_sold), true });
            }
        }

        /// Whether a ride's results go past the basic ones: only rides that run along a track and are measured
        /// over a lap.
        bool measured(const save::Ride& ride)
        {
            using enum save::OperatingMode;
            return ride.kind == save::RideKind::Tracked && ride.mode != Boat && ride.mode != Race && ride.mode != MiniGolf && ride.mode != Aquarium;
        }

        void tests(InfoPage& page, const save::Ride& ride, std::size_t section, const StringTable& strings, const InfoUnits& units)
        {
            if (measured(ride))
            {
                for (const std::string_view key : kTestSections)
                    page.sections.push_back(text(strings, key));
                page.section = std::min(section, kTestSections.size() - 1);
            }
            const auto shown = static_cast<TestSection>(page.section);
            if (shown != TestSection::Basic)
            {
                const auto& lap = ride.stats.last_lap;
                if (!lap || !lap->valid)
                {
                    page.note = text(strings, "AttractionResultsDialogLabel_NoTestResults");
                    return;
                }
                switch (shown)
                {
                    case TestSection::Speed:
                        add(page, strings, "AttractionResultsDialogLabel_MaxSpeed", speed(lap->max_speed, units.distance));
                        add(page, strings, "AttractionResultsDialogLabel_AverageSpeed", speed(lap->average_speed, units.distance));
                        add(page, strings, "AttractionResultsDialogLabel_RideTime", duration(lap->ride_time));
                        add(page, strings, "AttractionResultsDialogLabel_RideLength", length(lap->ride_length, units.distance));
                        break;
                    case TestSection::Force:
                        add(page, strings, "AttractionResultsDialogLabel_MaxPositiveVerticalG", force(lap->max_positive_vertical));
                        add(page, strings, "AttractionResultsDialogLabel_MaxNegativeVerticalG", force(lap->max_negative_vertical));
                        add(page, strings, "AttractionResultsDialogLabel_MaxLateralG", force(lap->max_lateral));
                        break;
                    case TestSection::Drop:
                        add(page, strings, "AttractionResultsDialogLabel_TotalAirTime", duration(lap->air_time));
                        add(page, strings, "AttractionResultsDialogLabel_NumberOfDrops", ascii(std::to_string(lap->drop_count)));
                        add(page, strings, "AttractionResultsDialogLabel_HighestDropHeight", length(lap->highest_drop, units.distance));
                        add(page, strings, "AttractionResultsDialogLabel_NumberOfInversion", ascii(std::to_string(lap->inversion_count)));
                        break;
                    case TestSection::Basic:
                        break;
                }
                return;
            }
            if (!ride.assessment.valid)
                page.note = text(strings, "AttractionResultsDialogLabel_NotAssessed");
            else
            {
                add(page, strings, "AttractionResultsDialogLabel_Excitement", rating(ride.assessment.excitement, strings));
                add(page, strings, "AttractionResultsDialogLabel_Intensity", rating(ride.assessment.intensity, strings));
                add(page, strings, "AttractionResultsDialogLabel_Nausea", rating(ride.assessment.nausea, strings));
            }
        }

        void vehicles(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::Ride& ride = *f.ride;
            page.heading = kind_name(ride.base.symbol, strings);
            // How many a car or the ride holds is in the ride's kind, not in the save.
            add(page, strings, "AttractionVehicleDialogLabelCapacity", missing());
            if (ride.kind == save::RideKind::Tracked && !ride.trains.empty())
            {
                add(page, strings, "AttractionVehicleDialogLabelNumTrains", count(static_cast<std::int64_t>(ride.trains.size())));
                add(page, strings, "AttractionVehicleDialogLabelNumCars", count(static_cast<std::int64_t>(ride.trains.front().cars.size())));
            }
            paints(page, f, strings);
        }

        InfoControl tick(AttractionControl id, std::u16string label, bool on)
        {
            InfoControl c;
            c.id = control_id(id);
            c.kind = InfoControlKind::Tick;
            c.label = std::move(label);
            c.value = on ? 1 : 0;
            return c;
        }

        /// A waiting time: its tick, and its seconds while ticked.
        void wait_controls(InfoPage& page, AttractionControl on, AttractionControl seconds, std::u16string label, std::int32_t value)
        {
            page.controls.push_back(tick(on, std::move(label), value >= 0));
            InfoControl c;
            c.id = control_id(seconds);
            c.kind = InfoControlKind::Number;
            c.value = std::max(value, 0);
            c.maximum = kMostWait;
            c.step = kWaitStep;
            c.enabled = value >= 0;
            page.controls.push_back(std::move(c));
        }

        void mode(InfoPage& page, const save::Ride& ride, std::size_t section, const StringTable& strings, const InfoUnits& units)
        {
            const auto m = static_cast<std::size_t>(ride.mode);
            if (ride.kind != save::RideKind::Tracked)
            {
                if (m < kModeNames.size())
                    page.heading = text(strings, kModeNames[m]);
                return;
            }
            const save::OperatingOptions& o = ride.options;
            for (const std::string_view key : kModeSections)
                page.sections.push_back(text(strings, key));
            page.section = std::min(section, kModeSections.size() - 1);
            if (page.section == 1)
            {
                // The page has room for these five rows only under the sections.
                InfoControl c;
                c.id = control_id(AttractionControl::WaitFor);
                c.kind = InfoControlKind::Choice;
                c.label = text(strings, "RideOpModeDialog_WaitFor");
                for (const std::string_view key : kLoads)
                    c.items.push_back(text(strings, key));
                c.value = o.wait_for_load >= 0 && std::cmp_less(o.wait_for_load, kLoads.size()) ? o.wait_for_load : -1;
                page.controls.push_back(std::move(c));
                wait_controls(page, AttractionControl::MinWaitOn, AttractionControl::MinWait, text(strings, "RideOpModeDialog_MinWaitTime"), o.min_wait);
                wait_controls(page, AttractionControl::MaxWaitOn, AttractionControl::MaxWait, text(strings, "RideOpModeDialog_MaxWaitTime"), o.max_wait);
                return;
            }
            if (m < kModeNames.size())
                page.heading = text(strings, kModeNames[m]);
            add(page, strings, "RideOpModeDialog_BlockSection", yes_no(o.block_sectioned, strings));
            add(page, strings, "RideOpModeDialog_SyncStations", yes_no(o.sync_trains, strings));
            using enum save::OperatingMode;
            const bool launched = ride.mode == PoweredLaunch || ride.mode == ReverseInclinedLaunch || ride.mode == UpwardsLaunch || ride.mode == MultiplePoweredLaunch;
            if (launched)
                add(page, strings, "RideOpModeDialog_LaunchSpeed", speed(o.launch_speed, units.distance));
            else if (o.lift_speed > 0)
                add(page, strings, "RideOpModeDialog_LiftChainSpeed", speed(o.lift_speed, units.distance));
        }

        void graphs(InfoPage& page, const save::Ride& ride, const StringTable& strings)
        {
            const save::RideGraphs& g = ride.graphs;
            if (g.speed.empty() && g.altitude.empty() && g.vertical_g.empty() && g.lateral_g.empty())
            {
                page.note = text(strings, "AttractionResultsDialogLabel_NoTestResults");
                return;
            }
            page.graph = {
                { text(strings, "Graph_Speed"), g.speed },
                { text(strings, "Graph_Altitude"), g.altitude },
                { text(strings, "Graph_VerticalG"), g.vertical_g },
                { text(strings, "Graph_LateralG"), g.lateral_g },
            };
        }

        void thoughts(InfoPage& page, std::span<const save::ThoughtEntry> all, const StringTable& strings)
        {
            std::vector<const save::ThoughtEntry*> latest;
            for (const save::ThoughtEntry& t : all)
                if (t.thought > 0)
                    latest.push_back(&t);
            std::ranges::stable_sort(latest, std::ranges::greater { }, &save::ThoughtEntry::time);
            for (const save::ThoughtEntry* t : latest)
            {
                if (page.lines.size() == kThoughtLines)
                    break;
                if (std::u16string said = thought_text(strings, t->thought, t->variant, page.title); !said.empty())
                    page.lines.push_back({ std::move(said), { } });
            }
        }

        void maintenance(InfoPage& page, const save::Ride& ride, const StringTable& strings)
        {
            add(page, strings, "RideMaintenanceDialogLabel_Reliability", percent(ride.breakdown.reliability));
            add(page, strings, "RideMaintenanceDialogLabel_DownTime", percent(ride.breakdown.downtime_percent()));
            const float since = ride.base.time_since_inspection;
            add(page, strings, "RideMaintenanceDialogLabel_TimeSinceLastInspection", since < 0 ? std::u16string(u"-") : ascii(std::to_string(std::lround(since / kSecondsPerMinute))));
            add(page, strings, "RideMaintenanceDialogLabel_LastBreakdownType", breakdown(ride.breakdown.last, strings));
            InfoControl c;
            c.id = control_id(AttractionControl::Inspection);
            c.kind = InfoControlKind::Choice;
            c.label = text(strings, "RideMaintenanceDialogLabel_Inspection");
            for (const std::string_view key : kInspectionIntervals)
                c.items.push_back(text(strings, key));
            const std::int32_t interval = ride.breakdown.inspection_interval;
            c.value = interval >= 0 && std::cmp_less(interval, kInspectionIntervals.size()) ? interval : -1;
            page.controls.push_back(std::move(c));
        }

        void items(InfoPage& page, const save::Stall& stall, const StringTable& strings)
        {
            for (std::size_t i = 0; i < stall.items.size(); ++i)
                page.controls.push_back(price(control_id(AttractionControl::ItemPrice, i), item_name(stall.items[i], stall, strings), stall.items[i].price, kMostPrice));
        }

    } // namespace

    std::vector<AttractionTabInfo> attraction_tabs(AttractionKind kind, save::AttractionState state)
    {
        std::vector<AttractionTabInfo> tabs(kCommonTabs.begin(), kCommonTabs.end());
        for (AttractionTabInfo& t : tabs)
        {
            if (t.tab == AttractionTab::State)
                t.icon = state == save::AttractionState::Open ? kStateOpenIcon : state == save::AttractionState::Testing ? kStateTestingIcon : kStateClosedIcon;
        }
        if (kind == AttractionKind::Tracked || kind == AttractionKind::Flat)
            tabs.insert(tabs.end(), kRideTabs.begin(), kRideTabs.end());
        if (kind == AttractionKind::Tracked)
            tabs.insert(tabs.end(), kTrackTabs.begin(), kTrackTabs.end());
        if (kind == AttractionKind::Stall)
            tabs.insert(tabs.end(), kStallTabs.begin(), kStallTabs.end());
        std::ranges::sort(tabs, { }, &AttractionTabInfo::slot);
        return tabs;
    }

    std::vector<StateChoice> state_choices(AttractionKind kind)
    {
        std::vector<StateChoice> out;
        for (const StateChoice& c : kStateChoices)
        {
            if (c.state != save::AttractionState::Testing || kind == AttractionKind::Tracked || kind == AttractionKind::Flat)
                out.push_back(c);
        }
        return out;
    }

    AttractionKind attraction_kind(const save::Ride& ride)
    {
        switch (ride.kind)
        {
            case save::RideKind::Tracked:
                return AttractionKind::Tracked;
            case save::RideKind::Special:
                return AttractionKind::Facility;
            case save::RideKind::Flat:
                break;
        }
        return AttractionKind::Flat;
    }

    std::u16string attraction_status(const save::AttractionCommon& attraction, const StringTable& strings)
    {
        return status(attraction, strings);
    }

    std::u16string attraction_name(const save::AttractionCommon& attraction, const StringTable& strings)
    {
        const std::string_view name = attraction.name;
        if (name.size() > 2 && name.front() == kKeyMark && name.back() == kKeyMark)
        {
            const std::string_view key = name.substr(1, name.size() - 2);
            if (const auto found = strings.get(key); !found.empty())
                return std::u16string(found);
            return ascii(key);
        }
        if (name.starts_with(kNumbered))
        {
            std::u16string kind = kind_name(attraction.symbol, strings);
            if (kind.empty())
                return text(strings, kUnnamed);
            std::string_view number = name.substr(kNumbered.size());
            while (!number.empty() && (number.front() == ' ' || number.front() == '0'))
                number.remove_prefix(1);
            return number.empty() ? kind : kind + u" " + ascii(number);
        }
        if (name.empty())
            return text(strings, kUnnamed);
        return strings::to_utf16(name);
    }

    std::u16string rating_word(float value, const StringTable& strings)
    {
        const auto step = std::ranges::find_if(kRatingSteps, [&](float top) { return value < top; });
        return text(strings, std::format("{}{}", kRatingWord, step - kRatingSteps.begin()));
    }

    InfoPage attraction_page(AttractionTab tab, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units, std::size_t section)
    {
        InfoPage page;
        if (f.common == nullptr)
            return page;
        page.title = attraction_name(*f.common, strings);
        switch (tab)
        {
            case AttractionTab::View:
                page.note = status(*f.common, strings);
                // A ride is painted from its vehicles page, as in the game.
                if (f.ride == nullptr || f.ride->kind == save::RideKind::Special)
                    paints(page, f, strings);
                break;
            case AttractionTab::State:
                page.note = status(*f.common, strings);
                break;
            case AttractionTab::Finances:
                finances(page, f, strings, units);
                break;
            case AttractionTab::Customers:
                customers(page, f, strings);
                break;
            case AttractionTab::Tests:
                if (f.ride)
                    tests(page, *f.ride, section, strings, units);
                break;
            case AttractionTab::Vehicles:
                if (f.ride)
                    vehicles(page, f, strings);
                break;
            case AttractionTab::Mode:
                if (f.ride)
                    mode(page, *f.ride, section, strings, units);
                break;
            case AttractionTab::Graphs:
                if (f.ride)
                    graphs(page, *f.ride, strings);
                break;
            case AttractionTab::Extras:
                if (f.stall && f.stall->extras.empty())
                    page.note = text(strings, "GUITextSymbol_ShopExtras_NoItemsRequireExtras");
                // shortcut: an extra's name and the words of its levels are not read from the save, so each shows
                // as missing with its level's number; name them once the extras' assets are read.
                else if (f.stall)
                {
                    for (const save::StallExtra& e : f.stall->extras)
                        page.lines.push_back({ missing(), count(e.level) });
                }
                break;
            case AttractionTab::Maintenance:
                if (f.ride)
                    maintenance(page, *f.ride, strings);
                break;
            case AttractionTab::Items:
                if (f.stall)
                    items(page, *f.stall, strings);
                break;
            case AttractionTab::Thoughts:
                if (f.ride)
                    thoughts(page, f.ride->thoughts, strings);
                else if (f.stall)
                    thoughts(page, f.stall->thoughts, strings);
                break;
            case AttractionTab::Construction:
                break;
        }
        return page;
    }

} // namespace rct3::ui
