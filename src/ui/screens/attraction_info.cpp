// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/attraction_info.hpp"

#include "strings/ascii.hpp"
#include "strings/unicode.hpp"
#include "ui/screens/stall_menu.hpp"
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

        /// The words of an extra's levels, in the order the save numbers them.
        constexpr std::array<std::string_view, 6> kExtraLevels { "GUITextSymbol_ShopExtraSetting_NotProvided", "GUITextSymbol_ShopExtraSetting_Little", "GUITextSymbol_ShopExtraSetting_Some", "GUITextSymbol_ShopExtraSetting_More", "GUITextSymbol_ShopExtraSetting_Loads", "GUITextSymbol_ShopExtraSetting_GuestChoice" };

        constexpr std::string_view kUnnamed = "TT_AttractionInstance_UnnamedAttraction";
        constexpr char kKeyMark = '#';                 ///< Round a name that is a string's key
        constexpr std::string_view kNumbered = "%ME%"; ///< Before the number of a name made from the kind's
        constexpr std::string_view kNameSuffix = "Name";
        /// Tried between a kind's asset name and kNameSuffix to find the string of its name.
        constexpr std::array<std::string_view, 4> kNameInfixes = { "", "RC", "Stall", "Ride" };
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
        constexpr int kAxisNumbers = 5;
        constexpr float kMaintenancePageWidth = 440;
        constexpr float kModePageWidth = 680;
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
            const std::string_view key = breakdown_key(kind);
            return key.empty() ? std::u16string { } : text(strings, key);
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

        /// `words` with its number mark standing for `n`.
        std::u16string with_number(std::u16string words, std::int64_t n)
        {
            if (const auto at = words.find(u"%NUMBER%"); at != std::u16string::npos)
                words.replace(at, std::u16string_view(u"%NUMBER%").size(), count(n));
            return words;
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

        InfoControl price(int id, std::string name, std::u16string label, std::int64_t value, int most, int step = kPriceStep)
        {
            InfoControl c;
            c.id = id;
            c.name = std::move(name);
            c.kind = InfoControlKind::Number;
            c.label = std::move(label);
            c.value = static_cast<int>(std::clamp<std::int64_t>(value, 0, most));
            c.maximum = most;
            c.step = step;
            c.decimals = 2;
            return c;
        }

        void finances(InfoPage& page, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            // The projections are the game's own reckoning per hour, which the save does not keep.
            // Income, cost and profit, then the total apart under them.
            add(page, strings, "AttractionFinancesDialogLabel_Income", missing());
            add(page, strings, "AttractionFinancesDialogLabel_RunningCost", money_text(units.currency, f.common->running_cost));
            add(page, strings, "AttractionFinancesDialogLabel_Profit", missing());
            page.lines.push_back({ });
            add(page, strings, "AttractionFinancesDialogLabel_TotalProfit", money_text(units.currency, f.common->total_profit));
            if (f.ride == nullptr)
                return;
            const bool ride = f.ride->kind != save::RideKind::Special;
            page.controls.push_back(price(control_id(AttractionControl::Price), "AttractionFinancesDialogLabel_AdmissionPrice", text(strings, "AttractionFinancesDialogLabel_AdmissionPrice"), f.ride->ticket_price, ride ? kMostRidePrice : kMostPrice));
            // The price over the sums, as the player sets it first.
            page.controls_after = 0;
        }

        /// What `kinds` knows of the carried item or extra `symbol`, matched in any case.
        const park::CarriedKind* carried(std::span<const park::CarriedKind> kinds, std::string_view symbol)
        {
            const auto found = std::ranges::find_if(kinds, [&](const park::CarriedKind& k) { return strings::equals_ignoring_case(k.symbol, symbol); });
            return found == kinds.end() ? nullptr : &*found;
        }

        /// An item's name: as the player named it, else after what it is.
        std::u16string item_name(const save::StallItem& item, const save::Stall& stall, const StringTable& strings, std::span<const park::CarriedKind> kinds)
        {
            if (!item.name.starts_with(kNumbered) && !item.name.empty())
                return strings::to_utf16(item.name);
            const park::CarriedKind* k = carried(kinds, item.kind_symbol);
            if (k == nullptr || !strings.contains(k->name_text))
                return kind_name(stall.base.symbol, strings);
            return text(strings, k->name_text) + strings::to_utf16(std::string_view(item.name).substr(std::min(item.name.size(), kNumbered.size())));
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
                    label.replace(at, kSoldName.size(), item_name(item, *f.stall, strings, f.carried));
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
                add(page, strings, "AttractionResultsDialogLabel_Nausea", rating(ride.assessment.nausea, strings));
                // Ours, under the game's three ratings.
                if (const GuestsSuited s = guests_suited(ride.assessment.intensity, guests); s.in_park > 0)
                    add(page, strings, "Rct3pp_GuestsSuited", ascii(std::format("{} / {}", s.suited, s.in_park)));
            }
        }

        void vehicles(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::Ride& ride = *f.ride;
            // The kind of car it runs, else the kind of ride it is.
            if (!ride.trains.empty())
            {
                const std::string_view train = ride.trains.front().symbol;
                page.heading = std::u16string(strings.get_ignoring_case(std::format("{}CarName", train.substr(0, train.find(':')))));
            }
            if (page.heading.empty())
                page.heading = kind_name(ride.base.symbol, strings);
            // A tracked ride's seats in a car, from its first car's; a flat ride's how many it takes each go.
            const auto per_car = std::ranges::count_if(ride.seats, [](const auto& s) { return s.train == 0 && s.car == 0; });
            std::u16string capacity = missing();
            if (ride.kind == save::RideKind::Tracked && per_car > 0)
                capacity = with_number(text(strings, "AttractionVehicleDialogLabelPassengersPerCar"), per_car);
            else if (f.flow)
                capacity = with_number(text(strings, "AttractionVehicleDialogLabelPassengersPerRide"), static_cast<std::int64_t>(f.flow->seats));
            add(page, strings, "AttractionVehicleDialogLabelCapacity", capacity);
            if (ride.kind == save::RideKind::Tracked && !ride.trains.empty())
            {
                // Number boxes, greyed once the ride runs in the park: then its trains stay as they are.
                const auto number = [&](AttractionControl id, std::string_view key, int value, int fewest, int most) {
                    InfoControl c;
                    c.id = control_id(id);
                    c.name = key;
                    c.label = text(strings, key);
                    c.minimum = fewest;
                    c.maximum = std::max(most, fewest);
                    c.value = std::clamp(value, c.minimum, c.maximum);
                    c.enabled = f.trains && c.minimum < c.maximum;
                    page.controls.push_back(std::move(c));
                };
                const int trains = static_cast<int>(ride.trains.size());
                const int cars = static_cast<int>(ride.trains.front().cars.size());
                number(AttractionControl::Cars, "AttractionVehicleDialogLabelNumCars", cars, f.trains ? f.trains->fewest_cars : cars, f.trains ? f.trains->most_cars : cars);
                number(AttractionControl::Trains, "AttractionVehicleDialogLabelNumTrains", trains, f.trains ? 1 : trains, f.trains ? f.trains->most_trains : trains);
                InfoControl kind;
                kind.id = control_id(AttractionControl::CarKind);
                kind.name = "Ride_Coaster";
                kind.kind = InfoControlKind::Button;
                kind.icon = "Ride_Coaster";
                kind.help = text(strings, "AttractionVehicleDialogChangeCarType");
                kind.enabled = f.other_cars;
                page.controls.push_back(std::move(kind));
            }
            paints(page, f, strings);
            if (page.controls.size() > 1 && page.controls.back().kind == InfoControlKind::Paint && page.controls[page.controls.size() - 2].kind == InfoControlKind::Button)
            {
                // The pots go beside the car button, as one row of buttons.
                page.controls.back().beside = true;
                page.controls.back().label.clear();
            }
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

        void graphs(InfoPage& page, const save::Ride& ride, std::uint8_t shown, const StringTable& strings, const InfoUnits& units)
        {
            const save::RideGraphs& g = ride.graphs;
            if (g.speed.empty() && g.altitude.empty() && g.vertical_g.empty() && g.lateral_g.empty())
            {
                page.note = text(strings, "AttractionResultsDialogLabel_NoTestResults");
                return;
            }
            // In the player's units: speeds as they set them, heights in metres or feet, forces in g.
            const bool feet = units.distance == DistanceUnits::Imperial;
            const std::u16string speed_words = std::u16string(speed_unit(units.distance));
            const std::array<std::tuple<std::string_view, const std::vector<float>*, float, std::u16string, AttractionControl>, 4> all { {
                { "Graph_Speed", &g.speed, speed_scale(units.distance), speed_words, AttractionControl::SpeedSeries },
                { "Graph_Altitude", &g.altitude, feet ? kFeetPerMetre : 1.0f, feet ? u"ft" : u"m", AttractionControl::AltitudeSeries },
                { "Graph_VerticalG", &g.vertical_g, 1 / kGravity, u"g", AttractionControl::VerticalSeries },
                { "Graph_LateralG", &g.lateral_g, 1 / kGravity, u"g", AttractionControl::LateralSeries },
            } };
            std::array<InfoControl, 4> ticks;
            for (std::size_t i = 0; i < all.size(); ++i)
            {
                const auto& [key, values, scale, unit, id] = all[i];
                InfoSeries series { .name = text(strings, key), .values = *values, .key = std::string(key), .shown = (shown >> i & 1) != 0 };
                for (float& v : series.values)
                    v *= scale;
                // The numbers up the left follow the first series shown, which the plot stretches top to bottom.
                if (series.shown && page.graph_axis.empty() && !series.values.empty())
                {
                    const auto [low, high] = std::ranges::minmax(series.values);
                    for (int k = 0; k < kAxisNumbers; ++k)
                        page.graph_axis.push_back(ascii(std::format("{:.2f} ", high - (high - low) * static_cast<float>(k) / (kAxisNumbers - 1))) + unit);
                }
                ticks[i] = tick(id, key, series.name, series.shown);
                ticks[i].series = i;
                ticks[i].column = i < 2 ? 0 : 1; // Speed and altitude down the left, the forces down the right
                page.graph.push_back(std::move(series));
            }
            // In each column by name, as the game lists them: altitude over speed, lateral over vertical.
            for (const std::size_t i : { 1, 0, 3, 2 })
                page.controls.push_back(std::move(ticks[i]));
            // With none ticked, the left holds no numbers but the names still go to the ticks.
            if (page.graph_axis.empty())
                page.graph_axis.assign(kAxisNumbers, std::u16string { });
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
            // Reliability on a green bar, down time on a red one.
            add(page, strings, "RideMaintenanceDialogLabel_Reliability", percent(ride.breakdown.reliability));
            page.lines.back().bar = ride.breakdown.reliability / 100;
            add(page, strings, "RideMaintenanceDialogLabel_DownTime", percent(ride.breakdown.downtime_percent()));
            page.lines.back().bar = ride.breakdown.downtime_percent() / 100;
            page.lines.back().bar_colour = hex("#d04030");
            page.width = kMaintenancePageWidth;
            // The interval's list under the bars, how long since and what last broke under it.
            page.controls_after = 2;
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
            InfoControl mechanic;
            mechanic.id = control_id(AttractionControl::Mechanic);
            mechanic.name = "Staff_JobIsMechanicIcon";
            mechanic.kind = InfoControlKind::Button;
            mechanic.icon = "Staff_JobIsMechanicIcon";
            mechanic.help = text(strings, "TT_RideMaintenanceDialog_MechanicButton");
            mechanic.beside = true;
            page.controls.push_back(std::move(mechanic));
        }

        /// A carried item's or an extra's name as its symbol gives it, as `MedBurger` for `MedBurger:cid`.
        // shortcut: not the name its own overlay's text gives, upgrade once carried items' overlays are read.
        std::u16string symbol_name(std::string_view symbol)
        {
            return symbol.empty() ? missing() : strings::to_utf16(symbol.substr(0, symbol.find(':')));
        }

        /// `text` with `mark` in it replaced by `with`.
        std::u16string filled(std::u16string text, std::u16string_view mark, std::u16string_view with)
        {
            if (const auto at = text.find(mark); at != std::u16string::npos)
                text.replace(at, mark.size(), with);
            return text;
        }

        void items(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            const save::Stall& stall = *f.stall;
            const auto name_of = [&](std::span<const park::CarriedKind> kinds, std::string_view symbol) {
                const park::CarriedKind* k = carried(kinds, symbol);
                return k != nullptr && strings.contains(k->name_text) ? text(strings, k->name_text) : symbol_name(symbol);
            };
            const auto icon_of = [&](std::span<const park::CarriedKind> kinds, std::string_view symbol) {
                const park::CarriedKind* k = carried(kinds, symbol);
                return k != nullptr ? k->icon : std::string { };
            };
            StallMenuView m;
            m.sync = stall.sync;
            m.sync_help = text(strings, "Stall_Synchronize");
            m.bin_help = text(strings, "StallItemsWindow_TrashCan");
            m.handle_help = text(strings, "StallItemLine_Drag");
            m.most_price = kMostRidePrice;
            m.price_step = kItemPriceStep;
            m.longest_name = kLongestAttractionName;
            using enum AttractionControl;
            m.ids = { control_id(ItemAdd), control_id(ItemRemove), control_id(ItemMove), control_id(ItemPrice), control_id(ItemExtras), control_id(ItemName), control_id(SyncStalls), control_id(ItemColour) };
            for (std::size_t i = 0; i < f.offers.size() && i < kMostListed; ++i)
            {
                const std::u16string name = name_of(f.carried, f.offers[i]);
                m.goods.push_back({ name, icon_of(f.carried, f.offers[i]), filled(text(strings, "StallItemsWindow_DragOn"), u"%ITEM%", name) });
            }
            // Each extra's picture over its ticks says how much of it goes on, as its own page sets.
            for (const save::StallExtra& e : stall.extras)
            {
                const std::u16string level = e.level >= 0 && std::cmp_less(e.level, kExtraLevels.size()) ? text(strings, kExtraLevels[static_cast<std::size_t>(e.level)]) : missing();
                const std::u16string name = name_of(f.carried_extras, e.kind_symbol);
                m.extras.push_back({ name, icon_of(f.carried_extras, e.kind_symbol), name + u" : " + level });
            }
            for (std::size_t i = 0; i < stall.items.size() && i < kMostListed; ++i)
            {
                const save::StallItem& item = stall.items[i];
                const park::CarriedKind* k = carried(f.carried, item.kind_symbol);
                MenuLine l;
                l.name = item_name(item, stall, strings, f.carried);
                l.icon = k != nullptr ? k->icon : std::string { };
                l.price = static_cast<int>(std::clamp<std::int64_t>(item.price, 0, kMostRidePrice));
                l.ticked = item.extras;
                // shortcut: the profit is shown unknown, as the item's own cost is not read; upgrade once it is.
                l.help = filled(filled(text(strings, "StallItemLine_Profit"), u"%NAME%", l.name), u"%PROFIT%", missing());
                if (k != nullptr)
                {
                    for (const std::string& extra : k->extras)
                    {
                        const auto offered = std::ranges::find_if(stall.extras, [&](const save::StallExtra& e) { return strings::equals_ignoring_case(e.kind_symbol, extra); });
                        l.extras.push_back(offered == stall.extras.end() ? std::nullopt : std::optional(static_cast<std::size_t>(offered - stall.extras.begin())));
                        l.tick_help.push_back(filled(filled(text(strings, "StallItemLine_Check"), u"%EXTRA%", name_of(f.carried_extras, extra)), u"%ITEM%", l.name));
                    }
                    // shortcut: one pot, its first colour, for every item that takes the player's colours; upgrade
                    // once how many each takes is read.
                    if (k->paintable)
                        l.paints = { item.colours[0] };
                }
                m.lines.push_back(std::move(l));
            }
            page.menu = std::move(m);
            page.width = kMenuWidth;
            page.menu_height = kMenuHeight;
        }

    } // namespace

    std::string_view breakdown_key(save::BreakdownKind kind)
    {
        constexpr std::array<std::string_view, 9> kKeys = {
            "RideBreakdownTypeNone", "RideBreakdownTypeEmergencyStop", "RideBreakdownTypeCarRestraintsFailToOpen", "RideBreakdownTypeCarRestraintsFailToClose", "RideBreakdownTypeCarDoorsFailToOpen", "RideBreakdownTypeCarDoorsFailToClose", "RideBreakdownTypeTrainStuckLeavingStation", "RideBreakdownTypeStationBrakesFail", "RideBreakdownTypeControlFailure",
        };
        const auto i = static_cast<std::size_t>(kind);
        return i < kKeys.size() ? kKeys[i] : std::string_view { };
    }

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

    std::vector<StateChoice> state_choices(AttractionKind kind, bool tested)
    {
        std::vector<StateChoice> out;
        for (const StateChoice& c : kStateChoices)
        {
            if (c.state != save::AttractionState::Testing || (tested && (kind == AttractionKind::Tracked || kind == AttractionKind::Flat)))
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

    std::vector<InfoLine> design_lines(const save::DesignSummary& design, const StringTable& strings, const InfoUnits& units)
    {
        InfoPage page;
        add(page, strings, "AttractionResultsDialogLabel_SpaceRequired", ascii(std::format("{} x {}", design.width, design.length)));
        add(page, strings, "AttractionResultsDialogLabel_Cost", money_text(units.currency, design.cost));
        if (design.assessment.valid)
        {
            add(page, strings, "AttractionResultsDialogLabel_Excitement", rating(design.assessment.excitement, strings));
            add(page, strings, "AttractionResultsDialogLabel_Intensity", rating(design.assessment.intensity, strings));
            add(page, strings, "AttractionResultsDialogLabel_Nausea", rating(design.assessment.nausea, strings));
        }
        if (const auto& lap = design.results)
        {
            add(page, strings, "AttractionResultsDialogLabel_MaxSpeed", speed(lap->max_speed, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_AverageSpeed", speed(lap->average_speed, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_RideTime", duration(lap->ride_time));
            add(page, strings, "AttractionResultsDialogLabel_RideLength", length(lap->ride_length, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_MaxPositiveVerticalG", force(lap->max_positive_vertical));
            add(page, strings, "AttractionResultsDialogLabel_MaxNegativeVerticalG", force(lap->max_negative_vertical));
            add(page, strings, "AttractionResultsDialogLabel_MaxLateralG", force(lap->max_lateral));
            add(page, strings, "AttractionResultsDialogLabel_TotalAirTime", duration(lap->air_time));
            add(page, strings, "AttractionResultsDialogLabel_NumberOfDrops", ascii(std::to_string(lap->drop_count)));
            add(page, strings, "AttractionResultsDialogLabel_HighestDropHeight", length(lap->highest_drop, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_NumberOfInversion", ascii(std::to_string(lap->inversion_count)));
        }
        return std::move(page.lines);
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
                // Running, how many are on it after its state.
                if (f.flow && f.refusal.empty())
                {
                    std::u16string on = text(strings, f.flow->riding == 1 ? "PeopleOnRide1" : "PeopleOnRide0");
                    if (const auto mark = on.find(u'#'); mark != std::u16string::npos)
                        on.replace(mark, 1, count(static_cast<std::int64_t>(f.flow->riding)));
                    words.label += u", " + on;
                }
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
                // shortcut: pulling a whole attraction down, keeping a design with its scenery, picking a fence and
                // riding along are not built yet, so their buttons are greyed; enable each as it lands.
                button(AttractionControl::Demolish, "Attraction_General_Delete", "TT_AttractionDialogIconDestroyButton", false);
                if (f.ride && f.ride->kind == save::RideKind::Tracked)
                {
                    button(AttractionControl::SceneryPick, "ParkInfoIcon_PlaceOwned", "SavedSelection_Coaster_Button_TT", false);
                    button(AttractionControl::SceneryClear, "SavedSelectionGrabber_ClearIcon", "SavedSelection_Coaster_Clear_TT", false);
                    button(AttractionControl::SaveDesign, "File_SaveGame", "TT_AttractionDialogIconSaveButton", false);
                }
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
                {
                    // A ride's cars can be ridden along in once it has some, and a flat ride's seats looked out from.
                    const bool cars = (attraction_kind(*f.ride) == AttractionKind::Tracked && !f.ride->trains.empty()) || f.seats;
                    button(AttractionControl::RideCam, cars ? "RideCamGUI_Enabled" : "RideCamGUI_Disabled", cars ? "RideCamButton_Enabled" : "RideCamButton_Disabled", cars);
                }
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
                    graphs(page, *f.ride, f.graph_series, strings, units);
                break;
            case AttractionTab::Extras:
                if (f.stall && f.stall->extras.empty())
                    page.note = text(strings, "GUITextSymbol_ShopExtras_NoItemsRequireExtras");
                else if (f.stall)
                {
                    for (std::size_t i = 0; i < f.stall->extras.size() && i < kMostListed; ++i)
                    {
                        InfoControl c;
                        c.id = control_id(AttractionControl::ExtraLevel, i);
                        c.name = std::format("ExtraLevel{}", i);
                        c.kind = InfoControlKind::Choice;
                        c.label = symbol_name(f.stall->extras[i].kind_symbol);
                        for (const std::string_view key : kExtraLevels)
                            c.items.push_back(text(strings, key));
                        c.value = std::cmp_less(f.stall->extras[i].level, kExtraLevels.size()) ? f.stall->extras[i].level : -1;
                        page.controls.push_back(std::move(c));
                    }
                }
                break;
            case AttractionTab::Maintenance:
                if (f.ride)
                    maintenance(page, *f.ride, strings);
                break;
            case AttractionTab::Items:
                if (f.stall)
                    items(page, f, strings);
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
