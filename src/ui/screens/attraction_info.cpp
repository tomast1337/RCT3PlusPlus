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
#include <tuple>
#include <utility>

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
            { AttractionTab::Maintenance, 10, "Attraction_Maintenance", "AttractionDetails_RideMaintenanceHelp" },
        } };
        constexpr std::array<AttractionTabInfo, 2> kTrackTabs { {
            { AttractionTab::Construction, 7, "Attraction_Construction", "AttractionDetails_ConstructionHelp", true },
            { AttractionTab::Graphs, 9, "Attraction_Graphs", "AttractionDetails_GraphsHelp" },
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
        /// Our own ranges for the speeds the operating page sets, in metres a second.
        constexpr std::pair<float, float> kLaunchSpeeds { 4, 50 };
        constexpr std::pair<float, float> kLiftSpeeds { 1.5f, 5.5f };
        constexpr int kMostTrackCircuits = 20;
        constexpr float kModePageWidth = 600;
        constexpr float kSpeedWidth = 100;
        constexpr std::string_view kSoldName = "%NAME%";

        constexpr float kGravity = 9.81f;
        constexpr float kKmhPerMs = 3.6f;
        constexpr float kMphPerMs = 2.2369363f;
        constexpr float kFeetPerMetre = 3.28084f;
        constexpr int kSecondsPerMinute = 60;
        constexpr float kSecondsPerHour = 3600;
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
            page.lines.push_back({ text(strings, label), std::move(value), false, std::string(label) });
        }

        /// A line sharing its row with the next such line.
        void add_half(InfoPage& page, const StringTable& strings, std::string_view label, std::u16string value)
        {
            page.lines.push_back({ text(strings, label), std::move(value), true, std::string(label) });
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
            c.name = "AttractionVehicleDialogHelpPaintAttraction";
            c.kind = InfoControlKind::Paint;
            c.label = text(strings, "AttractionVehicleDialogHelpPaintAttraction");
            c.paints = f.colours;
            page.controls.push_back(std::move(c));
        }

        InfoControl price(int id, std::string name, std::u16string label, std::int64_t value, int most)
        {
            InfoControl c;
            c.id = id;
            c.name = std::move(name);
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
            page.controls.push_back(price(control_id(AttractionControl::Price), "AttractionFinancesDialogLabel_AdmissionPrice", text(strings, "AttractionFinancesDialogLabel_AdmissionPrice"), f.ride->ticket_price, ride ? kMostRidePrice : kMostPrice));
        }

        std::u16string item_name(const save::StallItem& item, const save::Stall& stall, const StringTable& strings)
        {
            return item.name.starts_with(kNumbered) || item.name.empty() ? kind_name(stall.base.symbol, strings) : strings::to_utf16(item.name);
        }

        void customers(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::AttractionCommon& a = *f.common;
            // Customers an hour and the age are reckoned by the game from its clock; neither is saved. A running
            // park measures the first.
            const bool measured = f.flow && f.flow->per_hour > 0;
            add_half(page, strings, "AttractionCustomersDialogLabel_Customers", measured ? count(std::lround(f.flow->per_hour)) : missing());
            add_half(page, strings, "AttractionCustomersDialogLabel_Popularity", percent(a.popularity * kPercent));
            add_half(page, strings, "AttractionCustomersDialogLabel_Satisfaction", percent(a.satisfaction * kPercent));
            add_half(page, strings, "AttractionCustomersDialogLabel_TotalCustomers", count(a.total_guests));
            add_half(page, strings, "AttractionCustomersDialogLabel_AgeYears", missing());
            if (f.ride && !f.ride->stations.empty())
            {
                float longest = 0;
                for (const save::Station& s : f.ride->stations)
                    longest = std::max(longest, s.queue_time);
                // Running, the wait the queue now has at the rate the ride takes riders; unknown until measured.
                const bool unknown = f.flow && !measured && f.flow->queued > 0;
                if (f.flow)
                    longest = measured ? static_cast<float>(f.flow->queued) * kSecondsPerHour / f.flow->per_hour : 0;
                add_half(page, strings, "AttractionCustomersDialogLabel_QueueTime", unknown ? missing() : duration(longest));
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

        void tests(InfoPage& page, const save::Ride& ride, std::span<const save::Guest> guests, std::size_t section, const StringTable& strings, const InfoUnits& units)
        {
            if (measured(ride))
            {
                for (const std::string_view key : kTestSections)
                    page.sections.push_back(text(strings, key));
                page.section = std::min(section, kTestSections.size() - 1);
                page.section_list = true;
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
                if (const GuestsSuited s = guests_suited(ride.assessment.intensity, guests); s.in_park > 0)
                    add(page, strings, "Rct3pp_GuestsSuited", ascii(std::format("{} / {}", s.suited, s.in_park)));
                add(page, strings, "AttractionResultsDialogLabel_Nausea", rating(ride.assessment.nausea, strings));
            }
        }

        void vehicles(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::Ride& ride = *f.ride;
            page.heading = kind_name(ride.base.symbol, strings);
            // How many a car or the ride holds is in the ride's kind, not in the save; a running park knows it.
            add(page, strings, "AttractionVehicleDialogLabelCapacity", f.flow ? count(f.flow->seats) : missing());
            if (f.trains)
            {
                // While it is built, its trains are chosen here.
                const auto number = [&](AttractionControl id, std::string_view key, int value, int fewest, int most) {
                    InfoControl c;
                    c.id = control_id(id);
                    c.name = key;
                    c.label = text(strings, key);
                    c.minimum = fewest;
                    c.maximum = std::max(most, fewest);
                    c.value = std::clamp(value, c.minimum, c.maximum);
                    c.enabled = c.minimum < c.maximum;
                    page.controls.push_back(std::move(c));
                };
                number(AttractionControl::Trains, "AttractionVehicleDialogLabelNumTrains", ride.train_count, 1, f.trains->most_trains);
                number(AttractionControl::Cars, "AttractionVehicleDialogLabelNumCars", ride.cars_per_train, f.trains->fewest_cars, f.trains->most_cars);
            }
            else if (ride.kind == save::RideKind::Tracked && !ride.trains.empty())
            {
                add(page, strings, "AttractionVehicleDialogLabelNumTrains", count(static_cast<std::int64_t>(ride.trains.size())));
                add(page, strings, "AttractionVehicleDialogLabelNumCars", count(static_cast<std::int64_t>(ride.trains.front().cars.size())));
            }
            paints(page, f, strings);
        }

        InfoControl tick(AttractionControl id, std::string_view name, std::u16string label, bool on)
        {
            InfoControl c;
            c.id = control_id(id);
            c.name = name;
            c.kind = InfoControlKind::Tick;
            c.label = std::move(label);
            c.value = on ? 1 : 0;
            return c;
        }

        /// A waiting time: its tick, and its seconds beside it while ticked.
        void wait_controls(InfoPage& page, AttractionControl on, AttractionControl seconds, std::string_view key, const StringTable& strings, std::int32_t value)
        {
            InfoControl t = tick(on, key, text(strings, key), value >= 0);
            t.column = 1;
            page.controls.push_back(std::move(t));
            InfoControl c;
            c.id = control_id(seconds);
            c.name = std::format("{}_seconds", key);
            c.kind = InfoControlKind::Number;
            c.value = std::max(value, 0);
            c.maximum = kMostWait;
            c.step = kWaitStep;
            c.enabled = value >= 0;
            c.column = 1;
            c.beside = true;
            c.width = kSpeedWidth;
            const std::u16string seconds_words = text(strings, "ManySeconds");
            c.text = seconds_words.substr(std::min(seconds_words.find(u'#') + 1, seconds_words.size()));
            page.controls.push_back(std::move(c));
        }

        /// A show ride's show, one section for each place in it and one more to add a trick at the end.
        void show(InfoPage& page, const save::RideShow& show, std::size_t section, const StringTable& strings)
        {
            float seconds = 0;
            for (const std::uint32_t t : show.order)
                seconds += show.tricks.at(t).seconds;
            for (std::size_t i = 0; i < show.order.size(); ++i)
                page.sections.push_back(ascii(std::to_string(i + 1)));
            page.sections.push_back(u"+");
            page.section = std::min(section, show.order.size());
            add(page, strings, "FireworkMaster_Timeline_TT", duration(seconds));
            const bool adding = page.section == show.order.size();
            const std::vector<std::uint32_t> offers = show_offers(show);
            InfoControl c;
            c.id = control_id(AttractionControl::ShowTrick);
            c.name = "FireworkMaster_ClickToAdd_TT";
            c.kind = InfoControlKind::Choice;
            c.label = adding ? text(strings, "FireworkMaster_ClickToAdd_TT") : duration(show.tricks.at(show.order[page.section]).seconds);
            for (const std::uint32_t t : offers)
                c.items.push_back(text(strings, show.tricks[t].name));
            const auto at = adding ? offers.end() : std::ranges::find(offers, show.order[page.section]);
            c.value = at == offers.end() ? -1 : static_cast<int>(at - offers.begin());
            page.controls.push_back(std::move(c));
            if (adding)
                return;
            InfoControl del;
            del.id = control_id(AttractionControl::ShowDelete);
            del.name = "PathEditor_Delete";
            del.kind = InfoControlKind::Button;
            del.text = text(strings, "PathEditor_Delete");
            del.enabled = show.order.size() > 1; // A show keeps one trick at least
            page.controls.push_back(std::move(del));
        }

        void mode(InfoPage& page, const save::Ride& ride, bool low_friction, const StringTable& strings, const InfoUnits& units)
        {
            const auto m = static_cast<std::size_t>(ride.mode);
            if (ride.kind != save::RideKind::Tracked)
            {
                if (m < kModeNames.size())
                    page.heading = text(strings, kModeNames[m]);
                // A flat ride is set by how many times round it runs each go; the waits are for trains.
                if (ride.kind == save::RideKind::Flat)
                {
                    InfoControl c;
                    c.id = control_id(AttractionControl::Circuits);
                    c.name = "RideOpModeDialog_NumCircuits";
                    c.label = text(strings, "RideOpModeDialog_NumCircuits");
                    c.value = std::clamp(ride.loop_count, 1, kMostCircuits);
                    c.minimum = 1;
                    c.maximum = kMostCircuits;
                    page.controls.push_back(std::move(c));
                }
                return;
            }
            // Two columns, as the game lays them out: how it runs on the left, when its trains leave on the right.
            const save::OperatingOptions& o = ride.options;
            page.width = kModePageWidth;
            InfoControl list;
            list.id = control_id(AttractionControl::Mode);
            list.name = "RideOpModeDialog_Mode";
            list.kind = InfoControlKind::Choice;
            // shortcut: the modes a ride's track allows are not read, so the list holds its own mode only and is
            // greyed; offer the others once the track kinds' modes are read.
            if (m < kModeNames.size())
                list.items.push_back(text(strings, kModeNames[m]));
            list.enabled = false;
            page.controls.push_back(std::move(list));
            const auto left_tick = [&](AttractionControl id, std::string_view key, bool on, std::string_view help = { }) {
                InfoControl c = tick(id, key, text(strings, key), on);
                c.help = help.empty() ? std::u16string { } : text(strings, help);
                page.controls.push_back(std::move(c));
            };
            left_tick(AttractionControl::BlockSection, "RideOpModeDialog_BlockSection", o.block_sectioned);
            using enum save::OperatingMode;
            const bool launched = ride.mode == PoweredLaunch || ride.mode == ReverseInclinedLaunch || ride.mode == UpwardsLaunch || ride.mode == MultiplePoweredLaunch;
            if (launched || o.lift_speed > 0)
            {
                // In hundredths of the player's unit, over a range of our own.
                const float scale = speed_scale(units.distance) * 100;
                const auto [low, high, step] = launched ? std::tuple { kLaunchSpeeds.first, kLaunchSpeeds.second, 1.0f } : std::tuple { kLiftSpeeds.first, kLiftSpeeds.second, 0.1f };
                const std::string_view key = launched ? "RideOpModeDialog_LaunchSpeed" : "RideOpModeDialog_LiftChainSpeed";
                InfoControl c;
                c.id = control_id(launched ? AttractionControl::LaunchSpeed : AttractionControl::LiftSpeed);
                c.name = key;
                c.label = text(strings, key);
                c.minimum = static_cast<int>(std::lround(low * scale));
                c.maximum = static_cast<int>(std::lround(high * scale));
                c.value = static_cast<int>(std::lround((launched ? o.launch_speed : o.lift_speed) * scale));
                c.step = static_cast<int>(std::lround(step * 100));
                c.decimals = 2;
                c.text = std::u16string(u" ") + std::u16string(speed_unit(units.distance));
                c.width = kSpeedWidth;
                page.controls.push_back(std::move(c));
            }
            InfoControl laps;
            laps.id = control_id(AttractionControl::Circuits);
            laps.name = "RideOpModeDialog_NumCircuits";
            laps.label = text(strings, laps.name);
            laps.value = std::clamp(ride.loop_count, 1, kMostTrackCircuits);
            laps.minimum = 1;
            laps.maximum = kMostTrackCircuits;
            page.controls.push_back(std::move(laps));
            left_tick(AttractionControl::SyncStations, "RideOpModeDialog_SyncStations", o.sync_trains, "RideOpModeDialog_SyncStations_Tooltip");

            InfoControl wait;
            wait.id = control_id(AttractionControl::WaitFor);
            wait.name = "RideOpModeDialog_WaitFor";
            wait.kind = InfoControlKind::Choice;
            wait.label = text(strings, "RideOpModeDialog_WaitFor");
            for (const std::string_view key : kLoads)
                wait.items.push_back(text(strings, key));
            wait.value = o.wait_for_load >= 0 && std::cmp_less(o.wait_for_load, kLoads.size()) ? o.wait_for_load : -1;
            wait.column = 1;
            page.controls.push_back(std::move(wait));
            wait_controls(page, AttractionControl::MinWaitOn, AttractionControl::MinWait, "RideOpModeDialog_MinWaitTime", strings, o.min_wait);
            wait_controls(page, AttractionControl::MaxWaitOn, AttractionControl::MaxWait, "RideOpModeDialog_MaxWaitTime", strings, o.max_wait);
            for (const auto& [id, key, on, help] : { std::tuple { AttractionControl::LeaveOnArrival, "RideOpModeDialog_LeaveIfAnotherArrives", o.leave_if_other_train_enters_station, "RideOpModeDialog_LeaveIfAnotherArrives_Tooltip" }, std::tuple { AttractionControl::LowFriction, "RideOpModeDialog_LowFriction", low_friction, "RideOpModeDialog_LowFriction_TT" } })
            {
                InfoControl c = tick(id, key, text(strings, key), on);
                c.help = text(strings, help);
                c.column = 1;
                page.controls.push_back(std::move(c));
            }
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
            c.name = "RideMaintenanceDialogLabel_Inspection";
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
                page.controls.push_back(price(control_id(AttractionControl::ItemPrice, i), std::format("ItemPrice{}", i), item_name(stall.items[i], stall, strings), stall.items[i].price, kMostPrice));
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

    float speed_scale(DistanceUnits units)
    {
        return units == DistanceUnits::Imperial ? kMphPerMs : units == DistanceUnits::SI ? 1.0f : kKmhPerMs;
    }

    std::u16string_view speed_unit(DistanceUnits units)
    {
        return units == DistanceUnits::Imperial ? u"mph" : units == DistanceUnits::SI ? u"m/s" : u"km/h";
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

    GuestsSuited guests_suited(float intensity, std::span<const save::Guest> guests)
    {
        GuestsSuited s;
        for (const save::Guest& g : guests)
        {
            if (!g.in_park)
                continue;
            ++s.in_park;
            s.suited += g.ride_preference.min <= intensity && intensity <= g.ride_preference.max;
        }
        return s;
    }

    std::vector<std::uint32_t> show_offers(const save::RideShow& show)
    {
        std::vector<std::uint32_t> offers;
        for (std::uint32_t t = 1; t < show.tricks.size(); ++t)
            if (!show.tricks[t].hidden && show.tricks[t].available && show.tricks[t].invented)
                offers.push_back(t);
        return offers;
    }

    InfoPage attraction_page(AttractionTab tab, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units, std::size_t section)
    {
        InfoPage page;
        if (f.common == nullptr)
            return page;
        page.title = attraction_name(*f.common, strings);
        const std::u16string note = f.refusal.empty() ? status(*f.common, strings) : text(strings, f.refusal);
        switch (tab)
        {
            case AttractionTab::View:
            {
                // Its name to type over, its tune beside it, its state under them and a row of picture buttons.
                InfoControl name;
                name.id = control_id(AttractionControl::Name);
                name.name = "TT_AttractionDialogIconLabelButton";
                name.kind = InfoControlKind::Text;
                name.text = page.title;
                name.maximum = kLongestAttractionName;
                name.width = kNameWidth;
                name.help = text(strings, name.name);
                page.controls.push_back(std::move(name));
                if (f.ride)
                {
                    InfoControl music;
                    music.id = control_id(AttractionControl::Music);
                    music.name = "AttractionViewDialogMusicToolTip";
                    music.kind = InfoControlKind::Choice;
                    music.items.push_back(text(strings, "AttractionViewDialogNoMusic"));
                    music.items.insert(music.items.end(), f.music.begin(), f.music.end());
                    music.value = static_cast<int>(f.tune < music.items.size() ? f.tune : 0);
                    music.width = kNameWidth;
                    music.beside = true;
                    music.help = text(strings, music.name);
                    page.controls.push_back(std::move(music));
                }
                InfoControl words;
                words.name = "status";
                words.kind = InfoControlKind::Words;
                words.label = note;
                page.controls.push_back(std::move(words));
                bool first = true;
                const auto button = [&](AttractionControl id, std::string_view icon, std::string_view help, bool enabled = true) {
                    InfoControl c;
                    c.id = control_id(id);
                    c.name = icon;
                    c.kind = InfoControlKind::Button;
                    c.icon = icon;
                    c.help = text(strings, help);
                    c.enabled = enabled;
                    c.beside = !std::exchange(first, false);
                    page.controls.push_back(std::move(c));
                };
                // shortcut: pulling a whole attraction down, keeping a design, picking a fence and riding along
                // are not built yet, so their buttons are greyed; enable each as it lands.
                button(AttractionControl::Demolish, "Attraction_General_Delete", "TT_AttractionDialogIconDestroyButton", false);
                if (f.ride && f.ride->kind == save::RideKind::Tracked)
                    button(AttractionControl::SaveDesign, "File_SaveGame", "TT_AttractionDialogIconSaveButton", false);
                if (f.station)
                {
                    button(AttractionControl::Fence, "Attraction_General_PickFence", *f.station ? "TT_AttractionDialogIconSetFenceButton" : "CantChooseFenceAsRideIsNotPlaced", false);
                    for (const bool exit : { false, true })
                    {
                        const std::string_view help = exit ? "TT_AttractionDialogIconSetExitButton" : "TT_AttractionDialogIconSetEntranceButton";
                        button(exit ? AttractionControl::Exit : AttractionControl::Entrance, exit ? "Attraction_General_Exit" : "Attraction_General_Entrance", *f.station ? help : exit ? "CantPlaceExitAsRideHasNoStation" : "CantPlaceEntranceAsRideHasNoStation", *f.station);
                    }
                }
                if (f.queue_signs)
                    button(AttractionControl::QueueSigns, "Attraction_General_ToggleQueueSigns", "TT_AttractionDialogIconToggleQueueSigns");
                button(AttractionControl::Locate, "CenterInViewIcon", "TT_AttractionDialogIconFocusCameraButton");
                if (f.ride)
                    button(AttractionControl::RideCam, "RideCamGUI_Disabled", "KeySetting_ToggleRideCam", false);
                // A ride is painted from its vehicles page, as in the game.
                if (f.ride == nullptr || f.ride->kind == save::RideKind::Special)
                    paints(page, f, strings);
                break;
            }
            case AttractionTab::State:
                page.note = note;
                break;
            case AttractionTab::Finances:
                finances(page, f, strings, units);
                break;
            case AttractionTab::Customers:
                customers(page, f, strings);
                break;
            case AttractionTab::Tests:
                if (f.ride)
                    tests(page, *f.ride, f.guests, section, strings, units);
                break;
            case AttractionTab::Vehicles:
                if (f.ride)
                    vehicles(page, f, strings);
                break;
            case AttractionTab::Mode:
                if (f.show)
                    show(page, *f.show, section, strings);
                else if (f.ride)
                    mode(page, *f.ride, f.low_friction, strings, units);
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
