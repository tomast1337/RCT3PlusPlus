// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management_pages.hpp"

#include "strings/unicode.hpp"
#include "ui/save_text.hpp"
#include "ui/screens/person_info.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <span>
#include <string>
#include <unordered_map>

namespace rct3::ui
{

    namespace
    {
        constexpr std::u16string_view kDash = u"-";

        struct TabSpec
        {
            std::string_view icon;
            std::string_view name; ///< String key
        };

        constexpr std::array<TabSpec, 4> kParkTabs = { {
            { "ParkInfoDialogIcon_View", "PID_TT_ViewTabPopUp" },
            { "ParkInfoDialogIcon_Graphs", "PID_TT_GraphsTabPopUp" },
            { "ParkInfoDialogIcon_Campaigns", "PID_TT_CampaignsTabPopUp" },
            { "ParkInfoDialogIcon_Awards", "PID_TT_AwardTabPopUp" },
        } };
        constexpr std::array<TabSpec, 2> kFinanceTabs = { {
            { "FinancialSummaryTabIcon", "FinancialSummary_MonthlySummaryHelp" },
            { "CashGraphTabIcon", "FinancialSummary_GraphsHelp" },
        } };
        /// The first lists everyone; each of the others one job.
        constexpr std::array<TabSpec, 8> kStaffTabs = { {
            { "Staff_JobsTabIcon", "Staff_Title" },
            { "Staff_JobIsMaintenanceIcon", "HandymanName" },
            { "Staff_JobIsMechanicIcon", "MechanicName" },
            { "Staff_JobIsSecurityIcon", "SecurityGuardName" },
            { "Staff_JobIsEntertainerIcon", "EntertainerName" },
            { "Staff_JobIsVendorIcon", "VendorName" },
            { "Staff_JobIsParkInspectorIcon", "ParkInspectorName" },
            { "Staff_JobIsAnimalKeeperIcon", "AnimalKeeperName" },
        } };
        constexpr std::array<save::StaffKind, 7> kStaffTabKinds = {
            save::StaffKind::Handyman, save::StaffKind::Mechanic, save::StaffKind::SecurityGuard, save::StaffKind::Entertainer, save::StaffKind::Vendor, save::StaffKind::ParkInspector, save::StaffKind::AnimalKeeper,
        };
        /// The name of each job, in the order of save::StaffKind.
        constexpr std::array<std::string_view, 8> kJobNames = {
            "HandymanName", "MechanicName", "SecurityGuardName", "EntertainerName", "VendorName", "AnimalKeeperName", "LifeguardName", "ParkInspectorName",
        };
        constexpr std::array<TabSpec, 4> kGuestTabs = { {
            { "Guest_Member", "GuestList_IndividualGuests" },
            { "Guest_Group", "GuestList_Summarized" },
            { "Guest_Stats", "GuestList_Stats" },
            { "AvatarEditor_TitleIcon", "AvatarEditor_Title" }, // Ours: the player's families
        } };
        constexpr std::array<TabSpec, 4> kAttractionTabs = { {
            { "AttractionListDialogIconRides", "TT_AttractionListDialogIconRides" },
            { "AttractionListDialogIconShopsStalls", "TT_AttractionListDialogIconShopsStalls" },
            { "AttractionListDialogIconToiletsKiosks", "TT_AttractionListDialogIconToiletsKiosks" },
            { "AttractionListDialogIconAllAttractions", "TT_AttractionListDialogIconAllAttractions" },
        } };
        constexpr std::array<TabSpec, 3> kObjectiveTabs = { {
            { "Difficulty_Hat_Apprentice", "Difficulty_Apprentice" },
            { "Difficulty_Hat_Entrepreneur", "Difficulty_Entrepreneur" },
            { "Difficulty_Hat_Tycoon", "Difficulty_Tycoon" },
        } };
        constexpr std::array<TabSpec, 1> kResearchTabs = { { { "Man_Research", "Research_Label_Priorities" } } };
        constexpr std::array<TabSpec, 1> kChallengeTabs = { { { "Icon_Challenges_Large", "ChallengeWindow_Title" } } };

        enum class AttractionListTab : std::uint8_t
        {
            Rides,
            Stalls,
            Facilities,
            All,
        };

        /// The research areas the priorities belong to, in the priorities' order.
        constexpr std::array<std::string_view, 12> kResearchAreas = {
            "Obj_Rides_RollerCoastersHelp", "Obj_Rides_JuniorCoastersHelp", "Obj_Rides_ThrillRidesHelp", "Obj_Rides_WaterRidesHelp", "Obj_Rides_GentleRidesHelp", "Obj_Rides_TransportRidesHelp", "Obj_Rides_OtherRidesHelp", "Obj_ShopsHelp", "Obj_SceneryHelp", "Obj_PathsHelp", "ResearchCategory_ShowElements", "Obj_Rides_PoolRidesHelp",
        };

        constexpr std::array<std::string_view, 11> kGroupTypes = {
            "GuestGroupType_LoneTeenager", "GuestGroupType_LoneAdult", "GuestGroupType_TeenageCouple", "GuestGroupType_AdultCouple", "GuestGroupType_TeenageGang", "GuestGroupType_AdultParty", "GuestGroupType_Family", "GuestGroupType_DayTrip", "GuestGroupType_LoneVIP", "GuestGroupType_VIPEntourage", "GuestGroupType_AvatarFamily",
        };
        constexpr std::int32_t kFamilyGroup = 10; ///< The type of the groups the player makes

        // Ours, from our translations.
        constexpr std::string_view kNotRead = "Rct3pp_ManagementNotRead";
        constexpr std::string_view kFamiliesNote = "Rct3pp_ManagementFamiliesNote";
        constexpr std::string_view kLeader = "Rct3pp_ManagementLeader";
        constexpr std::string_view kMembers = "Rct3pp_ManagementMembers";
        constexpr std::string_view kCount = "Rct3pp_ManagementCount";

        constexpr std::size_t kRowKindShift = 48;

        std::u16string text(const StringTable& strings, std::string_view key)
        {
            return std::u16string(strings.get(key));
        }

        std::u16string ascii(std::string_view s)
        {
            return { s.begin(), s.end() };
        }

        std::u16string whole(std::int64_t n)
        {
            return ascii(std::format("{}", n));
        }

        /// `value` of 0 to 1 as a percentage.
        std::u16string share(float value)
        {
            return ascii(std::format("{} %", std::lround(value * 100)));
        }

        std::u16string money(const InfoUnits& units, std::int64_t amount)
        {
            return money_text(units.currency, amount);
        }

        std::u16string optional_money(const InfoUnits& units, std::optional<float> amount)
        {
            return amount ? money(units, std::llround(*amount)) : std::u16string(kDash);
        }

        std::vector<float> oldest_first(const save::Series& series)
        {
            // Series keep their newest sample first.
            return { series.samples.rbegin(), series.samples.rend() };
        }

        template<std::size_t N> std::vector<ManagementTab> tabs(const std::array<TabSpec, N>& specs, const StringTable& strings)
        {
            std::vector<ManagementTab> out;
            for (const TabSpec& t : specs)
                out.push_back({ std::string(t.icon), text(strings, t.name) });
            return out;
        }

        ManagementButton stub(std::string_view icon, std::u16string label, std::u16string help)
        {
            return { std::string(icon), std::move(label), std::move(help), false };
        }

        PersonFacts guest_facts(const ManagementFacts& f, const save::Guest& g)
        {
            return { .guest = &g, .people = f.people, .rides = f.rides };
        }

        PersonFacts staff_facts(const ManagementFacts& f, const save::Staff& s)
        {
            return { .staff = &s, .people = f.people, .rides = f.rides };
        }

        void add_series(ManagementPage& page, const StringTable& strings, std::string_view key, const save::Series& series)
        {
            if (!series.empty())
                page.graph.push_back({ text(strings, key), oldest_first(series), std::string(key) });
        }

        // ----- park operations -----

        void park_view(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const save::ParkModel* m = f.park != nullptr && f.park->has_park ? f.park : nullptr;
            page.lines.push_back({ text(strings, "Park_Name"), m != nullptr ? park_name(m->park, strings) : std::u16string(kDash), false, "Park_Name" });
            std::u16string status(kDash);
            if (m != nullptr)
                status = text(strings, m->park.flags.has(save::ParkFlag::Open) ? "AttractionStatusOpen" : "AttractionStatusClosed");
            page.lines.push_back({ text(strings, "AttractionListDialogParameterStatus"), status, false, "AttractionListDialogParameterStatus" });
            const bool finance = f.park != nullptr && f.park->has_finance;
            page.lines.push_back({ text(strings, "FinancialSummary_EntranceFee"), finance ? money(units, f.park->finance.entrance_fee) : std::u16string(kDash), false, "FinancialSummary_EntranceFee" });
            page.lines.push_back({ text(strings, "Park_OpeningTime"), m != nullptr ? clock_text(m->park.opening_minutes, false) : std::u16string(kDash), false, "Park_OpeningTime" });
            page.lines.push_back({ text(strings, "Park_ClosingTime"), m != nullptr ? clock_text(m->park.closing_minutes, false) : std::u16string(kDash), false, "Park_ClosingTime" });
            page.lines.push_back({ text(strings, "ObjectiveCondition_ParkGuests"), f.state != nullptr ? whole(f.state->guests) : std::u16string(kDash), false, "ObjectiveCondition_ParkGuests" });
            std::u16string rating(kDash);
            if (f.state != nullptr && f.state->rating)
                rating = whole(std::lround(*f.state->rating * save::kMaxParkRating));
            page.lines.push_back({ text(strings, "Graph_ParkRating"), rating, false, "Graph_ParkRating" });
            if (finance && f.park->finance.entrance_fee_fixed())
                page.note = text(strings, "FinancialSummary_EntranceFee_CannotBeChanged_TT");

            const bool open = m != nullptr && m->park.flags.has(save::ParkFlag::Open);
            page.buttons.push_back(stub(open ? "Attraction_General_Open" : "Attraction_General_Closed", { }, text(strings, "Park_OpenCloseHelp")));
            page.buttons.push_back(stub("ParkInfoIcon_PointCameraAtEntrance", { }, text(strings, "Park_CentreViewHelp")));
            page.buttons.push_back(stub("ParkInfoIcon_BuyLand", { }, text(strings, "Park_BuyLandHelp")));
            page.buttons.push_back(stub("ParkInfoIcon_BuyConstructionRites", { }, text(strings, "Park_BuyConstructionRitesHelp")));
        }

        void park_graphs(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            if (f.park != nullptr && f.park->has_park)
            {
                add_series(page, strings, "Graph_ParkRating", f.park->park.park_rating);
                add_series(page, strings, "Graph_Guests", f.park->park.guests_in_park);
                add_series(page, strings, "Graph_GuestIntake", f.park->park.guest_intake);
            }
            if (f.park != nullptr && f.park->has_finance)
                add_series(page, strings, "Graph_ParkValue", f.park->finance.park_value);
            if (page.graph.empty())
                page.note = text(strings, kNotRead);
        }

        void park_campaigns(ManagementPage& page, const StringTable& strings)
        {
            page.lines.push_back({ text(strings, "Campaign_Active"), std::u16string(kDash), false, "Campaign_Active" });
            page.lines.push_back({ text(strings, "Campaign_Availiable"), std::u16string(kDash), false, "Campaign_Availiable" });
            page.note = text(strings, kNotRead);
            page.buttons.push_back(stub("Campaign_ParkVouchers", { }, text(strings, "Campaign_Park_Vouchers")));
            page.buttons.push_back(stub("Campaign_Advertising", { }, text(strings, "Campaign_Park_Advertise")));
            page.buttons.push_back(stub("Campaign_AttractionVouchers", { }, text(strings, "Campaign_Attraction_Vouchers")));
        }

        void park_awards(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            if (f.park == nullptr || !f.park->has_awards)
            {
                page.lines.push_back({ text(strings, "PID_TT_AwardTabPopUp"), std::u16string(kDash), false, "PID_TT_AwardTabPopUp" });
                return;
            }
            std::size_t held = 0;
            for (const auto& category : f.park->awards.earned)
                held += category.size();
            if (held == 0)
            {
                page.note = text(strings, "Award_No_Awards");
                return;
            }
            // Which award each held number is is not known yet: only how many there are.
            page.lines.push_back({ text(strings, "PID_TT_AwardTabPopUp"), whole(static_cast<std::int64_t>(held)), false, "PID_TT_AwardTabPopUp" });
        }

        // ----- finances -----

        struct LedgerRow
        {
            std::string_view key;
            std::optional<std::int32_t> save::LedgerPeriod::* optional = nullptr;
            std::int32_t save::LedgerPeriod::* value = nullptr;
        };

        constexpr std::array<LedgerRow, 22> kLedger = { {
            { "Money_RideConstruction", nullptr, &save::LedgerPeriod::ride_construction },
            { "Money_RideRunningCosts", nullptr, &save::LedgerPeriod::ride_running_costs },
            { "Money_LandPurchase", nullptr, &save::LedgerPeriod::land_purchase },
            { "Money_Landscaping", nullptr, &save::LedgerPeriod::landscaping },
            { "Money_ParkEntranceTickets", nullptr, &save::LedgerPeriod::park_entrance_tickets },
            { "Money_RideTickets", nullptr, &save::LedgerPeriod::ride_tickets },
            { "Money_PoolTickets", &save::LedgerPeriod::pool_tickets, nullptr },
            { "Money_ViewingGalleryTickets", &save::LedgerPeriod::viewing_gallery_tickets, nullptr },
            { "Money_ShopSales", nullptr, &save::LedgerPeriod::shop_sales },
            { "Money_ShopStock", nullptr, &save::LedgerPeriod::shop_stock },
            { "Money_FoodDrinkSales", nullptr, &save::LedgerPeriod::food_drink_sales },
            { "Money_FoodDrinkStock", nullptr, &save::LedgerPeriod::food_drink_stock },
            { "Money_StaffWages", nullptr, &save::LedgerPeriod::staff_wages },
            { "Money_StaffTraining", nullptr, &save::LedgerPeriod::staff_training },
            { "Money_Marketing", nullptr, &save::LedgerPeriod::marketing },
            { "Money_Research", nullptr, &save::LedgerPeriod::research },
            { "Money_LoanInterest", nullptr, &save::LedgerPeriod::loan_interest },
            { "Money_Prizes", nullptr, &save::LedgerPeriod::prize_money },
            { "Money_AnimalCosts", &save::LedgerPeriod::other_animal_spending, nullptr },
            { "Money_Billboards", &save::LedgerPeriod::billboards, nullptr },
            { "Money_Other", nullptr, &save::LedgerPeriod::other },
            { "Money_Total", nullptr, nullptr },
        } };

        void finance_summary(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const save::Finance* m = f.park != nullptr && f.park->has_finance ? &f.park->finance : nullptr;
            const auto line = [&](std::string_view key, std::u16string value) { page.lines.push_back({ text(strings, key), std::move(value), true, std::string(key) }); };
            std::u16string cash(kDash);
            if (f.state != nullptr)
                cash = f.state->cash ? money(units, *f.state->cash) : text(strings, "FinancialSummary_MoneyOperates");
            else if (m != nullptr)
                cash = m->unlimited() ? text(strings, "FinancialSummary_MoneyOperates") : money(units, m->cash);
            line("FinancialSummary_Cash", cash);
            line("FinancialSummary_Loan", f.state != nullptr ? money(units, f.state->loan) : m != nullptr ? money(units, m->loan) : std::u16string(kDash));
            line("FinancialSummary_MaxLoan", m != nullptr ? money(units, m->max_loan) : std::u16string(kDash));
            std::u16string interest(kDash);
            if (m != nullptr)
                interest = ascii(std::format("{:.1f} % ", m->interest_rate_percent)) + text(strings, "FinancialSummary_PercentPerYear");
            line("Money_LoanInterest", interest);
            line("FinancialSummary_EntranceFee", m != nullptr ? money(units, m->entrance_fee) : std::u16string(kDash));
            line("FinancialSummary_ParkValue", m != nullptr ? optional_money(units, m->park_value.latest()) : std::u16string(kDash));
            line("FinancialSummary_CompanyValue", m != nullptr ? optional_money(units, m->company_value.latest()) : std::u16string(kDash));
            if (m == nullptr || m->ledger_periods_loaded == 0)
            {
                page.note = text(strings, kNotRead);
                return;
            }

            // One column a month, this month first, going back.
            const std::size_t periods = std::min(m->ledger_periods_loaded, save::kLedgerPeriods);
            page.columns.push_back({ std::u16string { }, 0, Align::Start, false });
            const std::int32_t month = f.state != nullptr ? f.state->date.month : f.park->clock.date().month;
            for (std::size_t p = 0; p < periods; ++p)
            {
                const std::int32_t back = ((month - static_cast<std::int32_t>(p)) % 8 + 8) % 8;
                page.columns.push_back({ text(strings, std::format("Month{}", back)), 76, Align::End, false });
            }
            for (std::size_t r = 0; r < kLedger.size(); ++r)
            {
                const LedgerRow& row = kLedger[r];
                TableRow out { { text(strings, row.key) }, { std::nullopt }, r };
                bool any = row.optional == nullptr;
                for (std::size_t p = 0; p < periods; ++p)
                {
                    const save::LedgerPeriod& period = m->ledger.at(p);
                    std::optional<std::int64_t> value;
                    if (row.value != nullptr)
                        value = period.*row.value;
                    else if (row.optional != nullptr)
                        value = period.*row.optional;
                    else
                        value = period.raw_sum();
                    any = any || value.has_value();
                    // The park keeps money coming in as negative and money going out as positive; shown the
                    // other way round, as a statement reads.
                    out.cells.push_back(value ? money(units, -*value) : std::u16string(kDash));
                    out.keys.emplace_back(std::nullopt);
                }
                if (any)
                    page.rows.push_back(std::move(out));
            }
        }

        void finance_graphs(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            if (f.park == nullptr || !f.park->has_finance)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const save::Finance& m = f.park->finance;
            add_series(page, strings, "Graph_CashLessLoan", m.cash_less_loan);
            add_series(page, strings, "Graph_ParkValue", m.park_value);
            add_series(page, strings, "Graph_WeeklyProfit", m.weekly_profit);
            add_series(page, strings, "Graph_CompanyValue", m.company_value);
            add_series(page, strings, "Graph_Income", m.income);
            add_series(page, strings, "Graph_MoneySpent", m.expenditure);
            if (page.graph.empty())
                page.note = text(strings, kNotRead);
        }

        // ----- staff -----

        void staff_list(ManagementPage& page, std::size_t tab, const ManagementFacts& f, const StringTable& strings)
        {
            page.columns = {
                { text(strings, "Staff_ColumnHeader_Name"), 0, Align::Start, true },
                { text(strings, "Staff_ColumnHeader_Jobs"), 90, Align::Start, true },
                { text(strings, "Staff_ColumnHeader_Activity"), 0, Align::Start, true },
                { text(strings, "Staff_ColumnHeader_Wage"), 50, Align::End, false },
            };
            page.sort = 1;
            if (f.people != nullptr)
            {
                for (std::size_t i = 0; i < f.people->staff.size(); ++i)
                {
                    const save::Staff& s = f.people->staff[i];
                    if (tab > 0 && s.kind != kStaffTabKinds.at(tab - 1))
                        continue;
                    const PersonFacts facts = staff_facts(f, s);
                    const auto kind = static_cast<std::size_t>(s.kind);
                    const std::u16string job = kind < kJobNames.size() ? text(strings, kJobNames.at(kind)) : std::u16string(kDash);
                    page.rows.push_back({ { person_name(facts, strings), job, person_doing(facts, strings), std::u16string(kDash) }, { std::nullopt, static_cast<double>(s.kind), std::nullopt, std::nullopt }, management_row_id({ ManagementRow::Kind::Staff, i }) });
                }
            }
            const std::array<std::pair<std::string_view, std::string_view>, 5> hire = { {
                { "Staff_HireMaintenanceIcon", "Staff_Hire_Maintenance" },
                { "Staff_HireMechanicIcon", "Staff_Hire_Mechanic" },
                { "Staff_HireSecurityIcon", "Staff_Hire_Security" },
                { "Staff_HireEntertainerIcon", "Staff_Hire_Entertainer" },
                { "Staff_HireAnimalKeeperIcon", "Staff_Hire_AnimalKeeper" },
            } };
            for (const auto& [icon, help] : hire)
                page.buttons.push_back(stub(icon, { }, text(strings, help)));
        }

        // ----- guests -----

        std::vector<std::size_t> guests_in_park(const ManagementFacts& f)
        {
            std::vector<std::size_t> out;
            if (f.people == nullptr)
                return out;
            for (std::size_t i = 0; i < f.people->guests.size(); ++i)
            {
                if (f.people->guests[i].in_park)
                    out.push_back(i);
            }
            return out;
        }

        void guest_list(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            page.columns = {
                { text(strings, "AttractionListDialogName"), 0, Align::Start, true },
                { text(strings, "AttractionListDialogParameterStatus"), 0, Align::Start, true },
                { text(strings, "StaffWindow_SortBy_Happiness"), 70, Align::End, true },
                { text(strings, "GuestDetails_CashHelp"), 70, Align::End, true },
            };
            page.sort = 0;
            for (const std::size_t i : guests_in_park(f))
            {
                const save::Guest& g = f.people->guests[i];
                const PersonFacts facts = guest_facts(f, g);
                page.rows.push_back({ { person_name(facts, strings), person_doing(facts, strings), share(g.mood_now.happiness), money(units, g.cash.current) }, { std::nullopt, std::nullopt, g.mood_now.happiness, static_cast<double>(g.cash.current) }, management_row_id({ ManagementRow::Kind::Guest, i }) });
            }
        }

        void guest_summary(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            page.columns = {
                { text(strings, "AttractionListDialogParameterStatus"), 0, Align::Start, true },
                { text(strings, kCount), 70, Align::End, true },
            };
            page.sort = 1;
            page.sort_descending = true;
            std::map<std::u16string, std::size_t> counts;
            for (const std::size_t i : guests_in_park(f))
                ++counts[person_doing(guest_facts(f, f.people->guests[i]), strings)];
            std::size_t n = 0;
            for (const auto& [doing, count] : counts)
            {
                const auto c = static_cast<double>(count);
                page.rows.push_back({ { doing.empty() ? std::u16string(kDash) : doing, whole(static_cast<std::int64_t>(count)) }, { std::nullopt, c }, management_row_id({ ManagementRow::Kind::None, n++ }) });
            }
        }

        void guest_stats(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            const std::vector<std::size_t> in_park = guests_in_park(f);
            const auto line = [&](std::string_view key, std::u16string value) { page.lines.push_back({ text(strings, key), std::move(value), false, std::string(key) }); };
            if (f.people == nullptr)
            {
                line("GuestStats_NumGuests", std::u16string(kDash));
                line("GuestStats_NumGroups", std::u16string(kDash));
                return;
            }
            line("GuestStats_NumGuests", whole(static_cast<std::int64_t>(in_park.size())));
            line("GuestStats_NumGroups", whole(static_cast<std::int64_t>(f.people->groups.size())));
            const auto average = [&](std::string_view key, auto value) {
                float sum = 0;
                std::size_t n = 0;
                for (const std::size_t i : in_park)
                {
                    if (const std::optional<float> v = value(f.people->guests[i]))
                    {
                        sum += *v;
                        ++n;
                    }
                }
                line(key, n > 0 ? share(sum / static_cast<float>(n)) : std::u16string(kDash));
            };
            average("GuestStats_AverageHappiness", [](const save::Guest& g) { return std::optional(g.mood_now.happiness); });
            average("GuestStats_AverageEnergy", [](const save::Guest& g) { return std::optional(g.mood_now.energy); });
            average("GuestStats_AverageHunger", [](const save::Guest& g) { return std::optional(g.needs.food); });
            average("GuestStats_AverageThirst", [](const save::Guest& g) { return std::optional(g.needs.drink); });
            average("GuestStats_AverageToilet", [](const save::Guest& g) { return std::optional(g.needs.toilet); });
            average("GuestStats_AverageNausea", [](const save::Guest& g) { return std::optional(g.mood_now.nausea); });
            average("GuestStats_AverageSunburn", [](const save::Guest& g) { return g.mood_now.sunburn; });
            page.lines.push_back({ text(strings, "GuestStats_GroupsOfType"), { }, false, "GuestStats_GroupsOfType" });
            std::array<std::int64_t, kGroupTypes.size()> types { };
            for (const save::GuestGroup& g : f.people->groups)
            {
                if (g.group_type >= 0 && static_cast<std::size_t>(g.group_type) < types.size())
                    ++types.at(static_cast<std::size_t>(g.group_type));
            }
            for (std::size_t t = 0; t < types.size(); ++t)
                page.lines.push_back({ text(strings, kGroupTypes.at(t)), whole(types.at(t)), true, std::string(kGroupTypes.at(t)) });
        }

        void guest_families(ManagementPage& page, const ManagementFacts& f, const StringTable& strings)
        {
            page.columns = {
                { text(strings, kLeader), 0, Align::Start, true },
                { text(strings, kMembers), 0, Align::Start, false },
                { text(strings, kCount), 50, Align::End, true },
            };
            page.note = text(strings, kFamiliesNote);
            page.buttons = {
                stub("AvatarEditor_AvatarCreateIcon", { }, text(strings, "AvatarEditor_TT_AvatarCreate")), stub("AvatarEditor_AvatarSaveIcon", { }, text(strings, "AvatarEditor_TT_AvatarSave")), stub("AvatarEditor_AvatarDeleteIcon", { }, text(strings, "AvatarEditor_TT_AvatarDelete")),
                stub("AvatarEditor_MemberCreateIcon", { }, text(strings, "AvatarEditor_TT_MemberCreate")), stub("AvatarEditor_MemberEditIcon", { }, text(strings, "AvatarEditor_TT_MemberEdit")), stub("AvatarEditor_MemberDeleteIcon", { }, text(strings, "AvatarEditor_TT_MemberDelete")),
            };
            if (f.people == nullptr)
                return;
            std::unordered_map<save::PeopleObjectId, std::size_t> by_id;
            for (std::size_t i = 0; i < f.people->guests.size(); ++i)
                by_id.emplace(f.people->guests[i].person.id, i);
            const auto name_of = [&](save::PeopleRef ref) -> std::optional<std::size_t> {
                const auto it = by_id.find(ref.id);
                return ref && it != by_id.end() ? std::optional(it->second) : std::nullopt;
            };
            for (const save::GuestGroup& g : f.people->groups)
            {
                if (g.group_type != kFamilyGroup && !g.avatar)
                    continue;
                const std::optional<std::size_t> leader = name_of(g.leader);
                std::u16string members;
                std::size_t count = 0;
                for (const save::PeopleRef m : g.members)
                {
                    const std::optional<std::size_t> member = name_of(m);
                    if (!member)
                        continue;
                    if (!members.empty())
                        members += u", ";
                    members += person_name(guest_facts(f, f.people->guests[*member]), strings);
                    ++count;
                }
                const std::u16string leader_name = leader ? person_name(guest_facts(f, f.people->guests[*leader]), strings) : std::u16string(kDash);
                page.rows.push_back({ { leader_name, members, whole(static_cast<std::int64_t>(count)) }, { std::nullopt, std::nullopt, static_cast<double>(count) }, leader ? management_row_id({ ManagementRow::Kind::Guest, *leader }) : management_row_id({ ManagementRow::Kind::None, page.rows.size() }) });
            }
        }

        // ----- attractions -----

        std::u16string whole_percent(float value)
        {
            return ascii(std::format("{} %", std::lround(value)));
        }

        void attraction_list(ManagementPage& page, AttractionListTab tab, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const bool rides = tab == AttractionListTab::Rides;
            page.columns = {
                { text(strings, "AttractionListDialogName"), 0, Align::Start, true },
                { text(strings, "AttractionListDialogParameterStatus"), 76, Align::Start, true },
                { text(strings, "AttractionListDialogParameterPopularity"), 76, Align::End, true },
                { text(strings, "AttractionListDialogParameterSatisfaction"), 84, Align::End, true },
            };
            if (rides)
            {
                page.columns.push_back({ text(strings, "AttractionListDialogParameterQueueLength"), 100, Align::End, true });
                page.columns.push_back({ text(strings, "AttractionListDialogParameterQueueTime"), 82, Align::End, true });
                page.columns.push_back({ text(strings, "AttractionListDialogParameterReliability"), 78, Align::End, true });
                page.columns.push_back({ text(strings, "AttractionListDialogParameterDownTime"), 76, Align::End, true });
            }
            else
            {
                page.columns.push_back({ text(strings, "AttractionCustomersDialogLabel_TotalCustomers"), 112, Align::End, true });
                page.columns.push_back({ text(strings, "AttractionFinancesDialogLabel_TotalProfit"), 92, Align::End, true });
            }
            page.sort = 0;
            page.buttons = {
                { { }, text(strings, "AttractionListDialogOpenAll"), text(strings, "TT_AttractionListDialogOpenAll"), false },
                { { }, text(strings, "AttractionListDialogCloseAll"), text(strings, "TT_AttractionListDialogCloseAll"), false },
            };
            if (f.rides == nullptr)
                return;
            const auto common_row = [&](const save::AttractionCommon& a, ManagementRow ref) {
                TableRow row { { attraction_name(a, strings), attraction_status(a, strings), whole_percent(a.popularity), whole_percent(a.satisfaction) }, { std::nullopt, std::nullopt, a.popularity, a.satisfaction }, management_row_id(ref) };
                return row;
            };
            const auto others = [&](TableRow& row, const save::AttractionCommon& a) {
                row.cells.push_back(whole(a.total_guests));
                row.keys.emplace_back(static_cast<double>(a.total_guests));
                row.cells.push_back(money(units, a.total_profit));
                row.keys.emplace_back(static_cast<double>(a.total_profit));
            };
            for (std::size_t i = 0; i < f.rides->rides.size(); ++i)
            {
                const save::Ride& r = f.rides->rides[i];
                const bool facility = r.kind == save::RideKind::Special;
                if ((tab == AttractionListTab::Rides && facility) || (tab == AttractionListTab::Facilities && !facility) || tab == AttractionListTab::Stalls)
                    continue;
                TableRow row = common_row(r.base, { ManagementRow::Kind::Ride, i });
                if (rides)
                {
                    std::int64_t queue = 0;
                    float wait = 0;
                    for (const save::Station& s : r.stations)
                    {
                        queue += s.queue_size;
                        wait += s.queue_time;
                    }
                    const float minutes = r.stations.empty() ? 0 : wait / static_cast<float>(r.stations.size()) / 60;
                    row.cells.push_back(whole(queue));
                    row.keys.emplace_back(static_cast<double>(queue));
                    row.cells.push_back(ascii(std::format("{} min", std::lround(minutes))));
                    row.keys.emplace_back(minutes);
                    row.cells.push_back(whole_percent(std::floor(r.breakdown.reliability)));
                    row.keys.emplace_back(r.breakdown.reliability);
                    row.cells.push_back(whole_percent(std::floor(r.breakdown.downtime_percent())));
                    row.keys.emplace_back(r.breakdown.downtime_percent());
                }
                else
                    others(row, r.base);
                page.rows.push_back(std::move(row));
            }
            if (tab != AttractionListTab::Stalls && tab != AttractionListTab::All)
                return;
            for (std::size_t i = 0; i < f.rides->stalls.size(); ++i)
            {
                TableRow row = common_row(f.rides->stalls[i].base, { ManagementRow::Kind::Stall, i });
                others(row, f.rides->stalls[i].base);
                page.rows.push_back(std::move(row));
            }
        }

        // ----- objectives -----

        std::string_view condition_key(save::ConditionKind kind)
        {
            using enum save::ConditionKind;
            switch (kind)
            {
                case ParkValue:
                    return "ObjectiveCondition_ParkValue";
                case ParkGuests:
                    return "ObjectiveCondition_ParkGuests";
                case ParkRating:
                    return "ObjectiveCondition_ParkRating";
                case RideIncome:
                    return "ObjectiveCondition_RideIncome";
                case ShopProfit:
                    return "ObjectiveCondition_ShopProfit";
                case RepayLoan:
                    return "ObjectiveCondition_RepayLoan";
                case Coaster:
                    return "ObjectiveCondition_RollerCoasters";
                case ItemsBought:
                    return "ObjectiveCondition_ItemsBought";
                case Vip:
                    return "ObjectiveCondition_VIP";
                case Other:
                    break;
            }
            return { };
        }

        std::u16string condition_target(const save::Condition& c, const InfoUnits& units)
        {
            if (!c.value || c.kind == save::ConditionKind::RepayLoan)
                return std::u16string(kDash);
            switch (c.kind)
            {
                case save::ConditionKind::ParkValue:
                case save::ConditionKind::RideIncome:
                case save::ConditionKind::ShopProfit:
                    return money(units, *c.value);
                case save::ConditionKind::ParkGuests:
                case save::ConditionKind::ParkRating:
                case save::ConditionKind::ItemsBought:
                    return whole(*c.value);
                default:
                    return std::u16string(kDash);
            }
        }

        std::u16string held_for(const save::Condition& c, const StringTable& strings)
        {
            switch (c.months_to_hold)
            {
                case 0:
                    return text(strings, "ObjectiveCondition_AtAnyTime");
                case 1:
                    return text(strings, "ObjectiveCondition_For1Month");
                case 2:
                    return text(strings, "ObjectiveCondition_For2Months");
                case 3:
                    return text(strings, "ObjectiveCondition_For3Months");
                default:
                    return text(strings, "ObjectiveCondition_ForManyMonths0") + ascii(std::format(" {} ", c.months_to_hold)) + text(strings, "ObjectiveCondition_ForManyMonths1");
            }
        }

        void objectives(ManagementPage& page, std::size_t difficulty, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            if (f.park == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const save::ObjectiveSet& set = f.park->objectives;
            const auto level = static_cast<std::int32_t>(difficulty);
            const auto objective = std::ranges::find_if(set.objectives, [&](const save::Objective& o) { return o.difficulty == level && o.objective_id == 0; });
            std::u16string state(kDash);
            if (objective != set.objectives.end() && objective->success)
                state = text(strings, "Objectives_Completed");
            page.lines.push_back({ text(strings, "AttractionListDialogParameterStatus"), state, false, "AttractionListDialogParameterStatus" });
            // Which prize each saved number stands for is not known yet.
            page.lines.push_back({ text(strings, "Objectives_Prize"), std::u16string(kDash), false, "Objectives_Prize" });
            const std::vector<const save::Condition*> conditions = set.conditions_for(level, 0);
            if (conditions.empty())
                page.note = text(strings, kNotRead);
            for (const save::Condition* c : conditions)
            {
                // The condition and its target, then how long it must hold and whether it has been met.
                const std::string_view key = condition_key(c->kind);
                page.lines.push_back({ key.empty() ? std::u16string(kDash) : text(strings, key), condition_target(*c, units), false, std::string(key) });
                page.lines.push_back({ held_for(*c, strings), c->completed ? text(strings, "Objectives_Completed") : std::u16string(kDash) });
            }
            if (const std::u16string goal = text(strings, f.park->park.objective_text); !goal.empty() && page.note.empty())
                page.note = goal;
        }

        // ----- research -----

        void research(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const save::ResearchState* r = f.park != nullptr && f.park->research.has_simple_research ? &f.park->research : nullptr;
            page.lines.push_back({ text(strings, "Research_Label_Budget"), r != nullptr ? money(units, r->monthly_budget) : std::u16string(kDash), false, "Research_Label_Budget" });
            page.columns = {
                { text(strings, "Research_Label_TechTree"), 0, Align::Start, false },
                { text(strings, "Research_Label_Priorities"), 120, Align::End, false },
            };
            if (r == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            for (std::size_t i = 0; i < r->priorities.size() && i < kResearchAreas.size(); ++i)
                page.rows.push_back({ { text(strings, kResearchAreas.at(i)), share(r->priorities[i]) }, { std::nullopt, r->priorities[i] }, management_row_id({ ManagementRow::Kind::None, i }) });
        }

        // ----- challenges -----

        void challenges(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            if (f.park == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const std::vector<save::Challenge>& list = f.park->objectives.challenges;
            if (list.empty())
            {
                page.note = text(strings, "ChallengeWindow_NoChallenge");
                return;
            }
            page.columns = {
                { text(strings, "ChallengeWindow_CurrentChallenge"), 0, Align::Start, true },
                { text(strings, "ChallengeWindow_TimeLimit"), 90, Align::End, false },
                { text(strings, "ChallengeWindow_TimeRemaining"), 100, Align::End, false },
                { text(strings, "Objectives_Prize"), 90, Align::End, true },
            };
            for (std::size_t i = 0; i < list.size(); ++i)
            {
                const save::Challenge& c = list[i];
                std::u16string name = text(strings, c.name_key);
                if (name.empty())
                    name = strings::to_utf16(c.name_key);
                // The time fields' unit is not known yet.
                page.rows.push_back({ { name, std::u16string(kDash), std::u16string(kDash), money(units, c.prize_cash) }, { std::nullopt, std::nullopt, std::nullopt, static_cast<double>(c.prize_cash) }, management_row_id({ ManagementRow::Kind::None, i }) });
            }
        }
    } // namespace

    std::u16string park_name(const save::Park& park, const StringTable& strings)
    {
        if (const auto found = strings.get(park.name); !park.name.empty() && !found.empty())
            return std::u16string(found);
        // A sandbox's scenario is named by its own text.
        const std::string_view key = park.scenario_name == "#SANDBOX#" ? std::string_view("SandboxScenario_Name") : std::string_view(park.scenario_name);
        const auto found = strings.get(key);
        const std::u16string scenario = !key.empty() && !found.empty() ? std::u16string(found) : strings::to_utf16(park.scenario_name);
        if (park.name.empty())
            return scenario.empty() ? std::u16string(kDash) : scenario;
        std::u16string name = strings::to_utf16(park.name);
        constexpr std::u16string_view kMe = u"#ME#";
        for (auto at = name.find(kMe); at != std::u16string::npos; at = name.find(kMe, at + scenario.size()))
            name.replace(at, kMe.size(), scenario);
        return name;
    }

    std::optional<ManagementTopic> management_topic(ToolItem item)
    {
        switch (item)
        {
            case ToolItem::ParkOperations:
                return ManagementTopic::ParkOperations;
            case ToolItem::Finances:
                return ManagementTopic::Finances;
            case ToolItem::Staff:
                return ManagementTopic::Staff;
            case ToolItem::Guests:
                return ManagementTopic::Guests;
            case ToolItem::Attractions:
                return ManagementTopic::Attractions;
            case ToolItem::Objectives:
                return ManagementTopic::Objectives;
            case ToolItem::Research:
                return ManagementTopic::Research;
            case ToolItem::Challenges:
                return ManagementTopic::Challenges;
            default:
                return std::nullopt;
        }
    }

    ManagementLayout management_layout(ManagementTopic topic)
    {
        switch (topic)
        {
            case ManagementTopic::ParkOperations:
                return { "Man_Park", "PID_Title", 481, 300 };
            case ManagementTopic::Finances:
                return { "Man_Finances", "FinancialManagement_Title", 600, 560 };
            case ManagementTopic::Staff:
                return { "Man_Staff", "Staff_Title", 520, 420 };
            case ManagementTopic::Guests:
                return { "Man_Guests", "Man_GuestsHelp", 480, 500 };
            case ManagementTopic::Attractions:
                return { "Man_Rides", "AttractionListDialog_Title", 800, 420 };
            case ManagementTopic::Objectives:
                return { "Man_Objectives", "Objectives_DialogTitle", 480, 420 };
            case ManagementTopic::Research:
                return { "Man_Research", "Research_Title", 420, 380 };
            case ManagementTopic::Challenges:
            case ManagementTopic::Count_:
                break;
        }
        return { "Man_Challenges", "ChallengeWindow_Title", 460, 320 };
    }

    std::vector<ManagementTab> management_tabs(ManagementTopic topic, const StringTable& strings)
    {
        switch (topic)
        {
            case ManagementTopic::ParkOperations:
                return tabs(kParkTabs, strings);
            case ManagementTopic::Finances:
                return tabs(kFinanceTabs, strings);
            case ManagementTopic::Staff:
                return tabs(kStaffTabs, strings);
            case ManagementTopic::Guests:
                return tabs(kGuestTabs, strings);
            case ManagementTopic::Attractions:
                return tabs(kAttractionTabs, strings);
            case ManagementTopic::Objectives:
                return tabs(kObjectiveTabs, strings);
            case ManagementTopic::Research:
                return tabs(kResearchTabs, strings);
            case ManagementTopic::Challenges:
            case ManagementTopic::Count_:
                break;
        }
        return tabs(kChallengeTabs, strings);
    }

    ManagementPage management_page(ManagementTopic topic, std::size_t tab, const ManagementFacts& facts, const StringTable& strings, const InfoUnits& units)
    {
        ManagementPage page;
        switch (topic)
        {
            case ManagementTopic::ParkOperations:
                if (tab == 0)
                    park_view(page, facts, strings, units);
                else if (tab == 1)
                    park_graphs(page, facts, strings);
                else if (tab == 2)
                    park_campaigns(page, strings);
                else
                    park_awards(page, facts, strings);
                break;
            case ManagementTopic::Finances:
                if (tab == 0)
                    finance_summary(page, facts, strings, units);
                else
                    finance_graphs(page, facts, strings);
                break;
            case ManagementTopic::Staff:
                staff_list(page, std::min(tab, kStaffTabs.size() - 1), facts, strings);
                break;
            case ManagementTopic::Guests:
                if (tab == 0)
                    guest_list(page, facts, strings, units);
                else if (tab == 1)
                    guest_summary(page, facts, strings);
                else if (tab == 2)
                    guest_stats(page, facts, strings);
                else
                    guest_families(page, facts, strings);
                break;
            case ManagementTopic::Attractions:
                attraction_list(page, static_cast<AttractionListTab>(std::min<std::size_t>(tab, 3)), facts, strings, units);
                break;
            case ManagementTopic::Objectives:
                objectives(page, std::min<std::size_t>(tab, 2), facts, strings, units);
                break;
            case ManagementTopic::Research:
                research(page, facts, strings, units);
                break;
            case ManagementTopic::Challenges:
            case ManagementTopic::Count_:
                challenges(page, facts, strings, units);
                break;
        }
        return page;
    }

    std::size_t management_row_id(ManagementRow row)
    {
        return (static_cast<std::size_t>(row.kind) << kRowKindShift) | (row.index & ((std::size_t { 1 } << kRowKindShift) - 1));
    }

    ManagementRow management_row(std::size_t id)
    {
        const auto kind = static_cast<std::uint8_t>(id >> kRowKindShift);
        if (kind > static_cast<std::uint8_t>(ManagementRow::Kind::None))
            return { };
        return { static_cast<ManagementRow::Kind>(kind), id & ((std::size_t { 1 } << kRowKindShift) - 1) };
    }

} // namespace rct3::ui
