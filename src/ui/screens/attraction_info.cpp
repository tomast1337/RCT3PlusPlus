// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/attraction_info.hpp"

#include "strings/ascii.hpp"
#include "strings/unicode.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>

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
            { AttractionTab::Construction, 7, "Attraction_Construction", "AttractionDetails_ConstructionHelp" },
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

        void finances(InfoPage& page, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            add(page, strings, "AttractionFinancesDialogLabel_TotalProfit", money_text(units.currency, f.common->total_profit));
            add(page, strings, f.ride ? "AttractionFinancesDialogLabel_ProjectedRunningCost" : "AttractionFinancesDialogLabel_RunningCost", money_text(units.currency, f.common->running_cost));
            if (f.ride && f.ride->ticket_price > 0)
                add(page, strings, "AttractionFinancesDialogLabel_AdmissionPrice", money_text(units.currency, f.ride->ticket_price));
        }

        void customers(InfoPage& page, const AttractionFacts& f, const StringTable& strings)
        {
            add(page, strings, "AttractionCustomersDialogLabel_TotalCustomers", ascii(std::to_string(f.common->total_guests)));
            if (f.ride && !f.ride->stations.empty())
            {
                float longest = 0;
                for (const save::Station& s : f.ride->stations)
                    longest = std::max(longest, s.queue_time);
                add(page, strings, "AttractionCustomersDialogLabel_QueueTime", duration(longest));
            }
        }

        void tests(InfoPage& page, const save::Ride& ride, const StringTable& strings, const InfoUnits& units)
        {
            if (!ride.assessment.valid)
                page.note = text(strings, "AttractionResultsDialogLabel_NotAssessed");
            else
            {
                add(page, strings, "AttractionResultsDialogLabel_Excitement", rating(ride.assessment.excitement, strings));
                add(page, strings, "AttractionResultsDialogLabel_Intensity", rating(ride.assessment.intensity, strings));
                add(page, strings, "AttractionResultsDialogLabel_Nausea", rating(ride.assessment.nausea, strings));
            }
            const auto& lap = ride.stats.last_lap;
            if (!lap || !lap->valid)
            {
                if (page.note.empty())
                    page.note = text(strings, "AttractionResultsDialogLabel_NoTestResults");
                return;
            }
            add(page, strings, "AttractionResultsDialogLabel_MaxSpeed", speed(lap->max_speed, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_RideLength", length(lap->ride_length, units.distance));
            add(page, strings, "AttractionResultsDialogLabel_RideTime", duration(lap->ride_time));
        }

        void maintenance(InfoPage& page, const save::Ride& ride, const StringTable& strings)
        {
            add(page, strings, "RideMaintenanceDialogLabel_Reliability", percent(ride.breakdown.reliability));
            add(page, strings, "RideMaintenanceDialogLabel_DownTime", percent(ride.breakdown.downtime_percent()));
            const float since = ride.base.time_since_inspection;
            add(page, strings, "RideMaintenanceDialogLabel_TimeSinceLastInspection", since < 0 ? std::u16string(u"-") : ascii(std::to_string(std::lround(since / kSecondsPerMinute))));
            add(page, strings, "RideMaintenanceDialogLabel_LastBreakdownType", breakdown(ride.breakdown.last, strings));
        }

        void items(InfoPage& page, const save::Stall& stall, const StringTable& strings, const InfoUnits& units)
        {
            for (const save::StallItem& item : stall.items)
            {
                std::u16string label = item.name.starts_with(kNumbered) || item.name.empty() ? kind_name(stall.base.symbol, strings) : strings::to_utf16(item.name);
                page.lines.push_back({ std::move(label), money_text(units.currency, item.price) });
            }
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

    InfoPage attraction_page(AttractionTab tab, const AttractionFacts& f, const StringTable& strings, const InfoUnits& units)
    {
        InfoPage page;
        if (f.common == nullptr)
            return page;
        page.title = attraction_name(*f.common, strings);
        switch (tab)
        {
            case AttractionTab::View:
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
                    tests(page, *f.ride, strings, units);
                break;
            case AttractionTab::Maintenance:
                if (f.ride)
                    maintenance(page, *f.ride, strings);
                break;
            case AttractionTab::Items:
                if (f.stall)
                    items(page, *f.stall, strings, units);
                break;
            case AttractionTab::Thoughts:
            case AttractionTab::Vehicles:
            case AttractionTab::Mode:
            case AttractionTab::Construction:
            case AttractionTab::Graphs:
            case AttractionTab::Extras:
                break;
        }
        return page;
    }

} // namespace rct3::ui
