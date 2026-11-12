// SPDX-License-Identifier: GPL-3.0-or-later
#include "ui/screens/management_pages.hpp"

#include "simulation/awards.hpp"
#include "simulation/objectives.hpp"
#include "simulation/park/finance.hpp"
#include "strings/unicode.hpp"
#include "ui/save_text.hpp"
#include "ui/screens/person_info.hpp"
#include "ui/thought_text.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <format>
#include <map>
#include <span>
#include <string>
#include <unordered_map>
#include <utility>

namespace rct3::ui::screens
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
        constexpr std::array<std::string_view, simulation::kResearchAreas> kResearchAreas = {
            "Obj_Rides_RollerCoastersHelp", "Obj_Rides_JuniorCoastersHelp", "Obj_Rides_ThrillRidesHelp", "Obj_Rides_WaterRidesHelp", "Obj_Rides_GentleRidesHelp", "Obj_Rides_TransportRidesHelp", "Obj_Rides_OtherRidesHelp", "Obj_ShopsHelp", "Obj_SceneryHelp", "Obj_PathsHelp", "ResearchCategory_ShowElements", "Obj_Rides_PoolRidesHelp", "Rct3pp_Improvement_Area",
        };

        constexpr std::array<std::string_view, 11> kGroupTypes = {
            "GuestGroupType_LoneTeenager", "GuestGroupType_LoneAdult", "GuestGroupType_TeenageCouple", "GuestGroupType_AdultCouple", "GuestGroupType_TeenageGang", "GuestGroupType_AdultParty", "GuestGroupType_Family", "GuestGroupType_DayTrip", "GuestGroupType_LoneVIP", "GuestGroupType_VIPEntourage", "GuestGroupType_AvatarFamily",
        };
        constexpr std::int32_t kFamilyGroup = 10; ///< The type of the groups the player makes

        // Ours, from our translations.
        constexpr std::string_view kNotRead = "Rct3pp_ManagementNotRead";
        constexpr std::string_view kNotSimulated = "Rct3pp_GoalNotSimulated";
        constexpr std::string_view kGoalNow = "Rct3pp_GoalNow";
        constexpr std::string_view kGoalMet = "Rct3pp_GoalMetNow";
        constexpr std::string_view kGoalNotMet = "Rct3pp_GoalNotMet";
        constexpr std::string_view kGoalHeld = "Rct3pp_GoalHeldSoFar";
        constexpr std::string_view kFamiliesNote = "Rct3pp_ManagementFamiliesNote";
        constexpr std::string_view kLeader = "Rct3pp_ManagementLeader";
        constexpr std::string_view kMembers = "Rct3pp_ManagementMembers";
        constexpr std::string_view kCount = "Rct3pp_ManagementCount";
        constexpr std::string_view kResearching = "Rct3pp_Research_Current";
        constexpr std::string_view kMonthLeft = "Rct3pp_Research_MonthLeft";
        constexpr std::string_view kMonthsLeft = "Rct3pp_Research_MonthsLeft";
        constexpr std::string_view kNothingLeft = "Rct3pp_Research_NothingLeft";

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

        /// The weeks gone of a park's series, oldest first: its newest sample is the week still running.
        std::vector<float> weeks_gone(const save::model::Series& series)
        {
            if (series.samples.size() < 2)
                return { };
            return { series.samples.rbegin(), series.samples.rend() - 1 };
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

        PersonFacts guest_facts(const ManagementFacts& f, const save::model::Guest& g)
        {
            return { .guest = &g, .people = f.people, .rides = f.rides };
        }

        /// Adds a weekly series to a page's graph, on a scale up to `full` (its own when 0), its numbers counts or
        /// amounts of money. The graph spans the weeks the longest series keeps.
        void add_series(ManagementPage& page, const StringTable& strings, const InfoUnits& units, std::string_view key, const save::model::Series& series, bool money_values, float full = 0)
        {
            if (series.empty())
                return;
            std::vector<float> values = weeks_gone(series);
            const bool centred = std::ranges::any_of(values, [](float v) { return v < 0; });
            if (full <= 0)
                full = widgets::full_scale(values);
            // Money reads in whole units at least, so a series of nought or a few cents is not on a scale of one.
            if (money_values)
                full = std::max(full, 100.0f);
            std::vector<std::u16string> axis;
            for (const float v : widgets::axis_values(full, centred))
                axis.push_back(money_values ? money(units, std::llround(v)) : whole(std::lround(v)));
            page.graph.push_back({ .name = text(strings, key), .values = std::move(values), .key = std::string(key), .full = full, .centred = centred, .axis = std::move(axis) });
            if (series.capacity > 1)
                page.graph_slots = std::max<std::size_t>(page.graph_slots, series.capacity - 1);
        }

        /// Marks along a weekly graph where each month began, named by the month, back from the week before
        /// `time`'s.
        void month_ticks(ManagementPage& page, const StringTable& strings, const simulation::park::ParkState* state)
        {
            if (state == nullptr)
                return;
            for (std::size_t back = 0; back < page.graph_slots; ++back)
            {
                // Nothing before the calendar began.
                const float time = state->time - static_cast<float>(back + 1) * save::model::GameClock::kUnitsPerWeek;
                if (time < state->zero_time)
                    break;
                const save::model::CalendarDate date = save::model::GameClock::date_at(time, state->zero_time);
                if (date.week == 0)
                    page.graph_ticks.push_back({ static_cast<float>(back), short_month(strings, date.month) });
            }
        }

        // ----- park operations -----

        void park_view(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const save::model::ParkModel* m = f.park != nullptr && f.park->has_park ? f.park : nullptr;
            page.lines.push_back({ text(strings, "Park_Name"), m != nullptr ? park_name(m->park, strings) : std::u16string(kDash), false, "Park_Name" });
            std::u16string status(kDash);
            if (m != nullptr)
                status = text(strings, m->park.flags.has(save::model::ParkFlag::Open) ? "AttractionStatusOpen" : "AttractionStatusClosed");
            page.lines.push_back({ text(strings, "AttractionListDialogParameterStatus"), status, false, "AttractionListDialogParameterStatus" });
            const bool finance = f.park != nullptr && f.park->has_finance;
            page.lines.push_back({ text(strings, "FinancialSummary_EntranceFee"), finance ? money(units, f.park->finance.entrance_fee) : std::u16string(kDash), false, "FinancialSummary_EntranceFee" });
            page.lines.push_back({ text(strings, "Park_OpeningTime"), m != nullptr ? clock_text(m->park.opening_minutes, false) : std::u16string(kDash), false, "Park_OpeningTime" });
            page.lines.push_back({ text(strings, "Park_ClosingTime"), m != nullptr ? clock_text(m->park.closing_minutes, false) : std::u16string(kDash), false, "Park_ClosingTime" });
            page.lines.push_back({ text(strings, "ObjectiveCondition_ParkGuests"), f.state != nullptr ? whole(f.state->guests) : std::u16string(kDash), false, "ObjectiveCondition_ParkGuests" });
            std::u16string rating(kDash);
            if (f.state != nullptr && f.state->rating)
                rating = whole(std::lround(*f.state->rating * save::model::kMaxParkRating));
            page.lines.push_back({ text(strings, "Graph_ParkRating"), rating, false, "Graph_ParkRating" });
            if (finance && f.park->finance.entrance_fee_fixed())
                page.note = text(strings, "FinancialSummary_EntranceFee_CannotBeChanged_TT");

            const bool open = m != nullptr && m->park.flags.has(save::model::ParkFlag::Open);
            const bool for_sale = m != nullptr && !m->park.flags.has(save::model::ParkFlag::LandPurchaseLocked);
            page.buttons.push_back({ open ? "Attraction_General_Open" : "Attraction_General_Closed", { }, text(strings, "Park_OpenCloseHelp"), m != nullptr, { { "Attraction_Button_Open", text(strings, "TT_AttractionDialogIconOpenButton") }, { "Attraction_Button_Closed", text(strings, "TT_AttractionDialogIconClosedButton") } } });
            page.buttons.push_back({ "ParkInfoIcon_PointCameraAtEntrance", { }, text(strings, "Park_CentreViewHelp"), true });
            page.buttons.push_back({ "ParkInfoIcon_BuyLand", { }, text(strings, "Park_BuyLandHelp"), for_sale });
            page.buttons.push_back({ "ParkInfoIcon_BuyConstructionRites", { }, text(strings, "Park_BuyConstructionRitesHelp"), for_sale });
            std::u16string broken = whole(f.broken_items);
            if (f.broken_items > 0)
                broken += u" (" + money(units, f.replace_cost) + u")";
            page.lines.push_back({ text(strings, "Rct3pp_BrokenItems"), std::move(broken), false, "Rct3pp_BrokenItems" });
            const bool affordable = f.state == nullptr || !f.state->cash || *f.state->cash >= f.replace_cost;
            page.buttons.push_back({ "Staff_JobIsMaintenanceIcon", { }, text(strings, "Rct3pp_ReplaceBrokenHelp"), f.broken_items > 0 && affordable });
        }

        void park_graphs(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            // Graphs come from the series alone: a park without its records has them once the simulation starts them.
            if (f.park != nullptr)
            {
                add_series(page, strings, units, "Graph_ParkValue", f.park->finance.park_value, true);
                const save::model::Park& p = f.park->park;
                // The guests by age share the scale of all of them, so they read against it.
                const float guests = widgets::full_scale(weeks_gone(p.guests_in_park));
                add_series(page, strings, units, "Graph_AdultsInPark", p.adults_in_park, false, guests);
                add_series(page, strings, units, "Graph_TeenagersInPark", p.teenagers_in_park, false, guests);
                add_series(page, strings, units, "Graph_ChildrenInPark", p.children_in_park, false, guests);
                add_series(page, strings, units, "Graph_Guests", p.guests_in_park, false, guests);
                add_series(page, strings, units, "Graph_GuestIntake", p.guest_intake, false);
                add_series(page, strings, units, "Graph_PublicityLevel", p.publicity_level, false);
                add_series(page, strings, units, "Graph_ParkRating", p.park_rating, false);
            }
            if (page.graph.empty())
                page.note = text(strings, kNotRead);
            month_ticks(page, strings, f.state);
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
            // The awards of the last months, a list a month.
            for (const auto& month : f.park->awards.earned)
                for (const std::int32_t kind : month)
                {
                    if (const std::string_view name = simulation::award_name(kind); !name.empty())
                        page.lines.push_back({ text(strings, name), std::u16string(), false, std::string(name) });
                }
            if (page.lines.empty())
                page.note = text(strings, "Award_No_Awards");
        }

        // ----- finances -----

        struct LedgerRow
        {
            std::string_view key;
            std::optional<std::int32_t> save::model::LedgerPeriod::* optional = nullptr;
            std::int32_t save::model::LedgerPeriod::* value = nullptr;
        };

        constexpr std::array<LedgerRow, 22> kLedger = { {
            { "Money_RideConstruction", nullptr, &save::model::LedgerPeriod::ride_construction },
            { "Money_RideRunningCosts", nullptr, &save::model::LedgerPeriod::ride_running_costs },
            { "Money_LandPurchase", nullptr, &save::model::LedgerPeriod::land_purchase },
            { "Money_Landscaping", nullptr, &save::model::LedgerPeriod::landscaping },
            { "Money_ParkEntranceTickets", nullptr, &save::model::LedgerPeriod::park_entrance_tickets },
            { "Money_RideTickets", nullptr, &save::model::LedgerPeriod::ride_tickets },
            { "Money_PoolTickets", &save::model::LedgerPeriod::pool_tickets, nullptr },
            { "Money_ViewingGalleryTickets", &save::model::LedgerPeriod::viewing_gallery_tickets, nullptr },
            { "Money_ShopSales", nullptr, &save::model::LedgerPeriod::shop_sales },
            { "Money_ShopStock", nullptr, &save::model::LedgerPeriod::shop_stock },
            { "Money_FoodDrinkSales", nullptr, &save::model::LedgerPeriod::food_drink_sales },
            { "Money_FoodDrinkStock", nullptr, &save::model::LedgerPeriod::food_drink_stock },
            { "Money_StaffWages", nullptr, &save::model::LedgerPeriod::staff_wages },
            { "Money_StaffTraining", nullptr, &save::model::LedgerPeriod::staff_training },
            { "Money_Marketing", nullptr, &save::model::LedgerPeriod::marketing },
            { "Money_Research", nullptr, &save::model::LedgerPeriod::research },
            { "Money_LoanInterest", nullptr, &save::model::LedgerPeriod::loan_interest },
            { "Money_Prizes", nullptr, &save::model::LedgerPeriod::prize_money },
            { "Money_AnimalCosts", &save::model::LedgerPeriod::other_animal_spending, nullptr },
            { "Money_Billboards", &save::model::LedgerPeriod::billboards, nullptr },
            { "Money_Other", nullptr, &save::model::LedgerPeriod::other },
            { "Money_Total", nullptr, nullptr },
        } };

        void finance_summary(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            const save::model::Finance* m = f.park != nullptr && f.park->has_finance ? &f.park->finance : nullptr;
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
            // While the park runs, what it and the company are worth now; otherwise as saved.
            std::u16string value(kDash);
            std::u16string company(kDash);
            if (f.state != nullptr && f.rides != nullptr)
            {
                const std::int64_t worth = simulation::park::park_value(f.state->guests, *f.rides);
                value = money(units, worth);
                company = money(units, f.state->cash.value_or(0) - f.state->loan + worth);
            }
            else if (m != nullptr)
            {
                value = optional_money(units, m->park_value.latest());
                company = optional_money(units, m->company_value.latest());
            }
            line("FinancialSummary_ParkValue", value);
            line("FinancialSummary_CompanyValue", company);
            const auto loan_button = [&](std::string_view key, std::string_view help, bool borrow) {
                const bool movable = f.state != nullptr && m != nullptr && simulation::park::loan_step(*f.state, m->max_loan, borrow) > 0;
                page.buttons.push_back({ { }, text(strings, key), text(strings, help), movable, { }, std::string(key) });
            };
            loan_button("Rct3pp_Borrow", "Rct3pp_BorrowHelp", true);
            loan_button("Rct3pp_Repay", "Rct3pp_RepayHelp", false);
            if (m == nullptr || m->ledger_periods_loaded == 0)
            {
                page.note = text(strings, kNotRead);
                return;
            }

            // One column a month, this month first, going back.
            const std::size_t periods = std::min(m->ledger_periods_loaded, save::model::kLedgerPeriods);
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
                widgets::TableRow out { { text(strings, row.key) }, { std::nullopt }, r };
                bool any = row.optional == nullptr;
                for (std::size_t p = 0; p < periods; ++p)
                {
                    const save::model::LedgerPeriod& period = m->ledger.at(p);
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

        void finance_graphs(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            if (f.park == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const save::model::Finance& m = f.park->finance;
            add_series(page, strings, units, "Graph_CashLessLoan", m.cash_less_loan, true);
            add_series(page, strings, units, "Graph_ParkValue", m.park_value, true);
            add_series(page, strings, units, "Graph_WeeklyProfit", m.weekly_profit, true);
            add_series(page, strings, units, "Graph_CompanyValue", m.company_value, true);
            add_series(page, strings, units, "Graph_Income", m.income, true);
            add_series(page, strings, units, "Graph_MoneySpent", m.expenditure, true);
            if (page.graph.empty())
                page.note = text(strings, kNotRead);
            month_ticks(page, strings, f.state);
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
                const save::model::Guest& g = f.people->guests[i];
                const PersonFacts facts = guest_facts(f, g);
                page.rows.push_back({ { person_name(facts, strings), person_doing(facts, strings), share(g.mood_now.happiness), money(units, g.cash.current) }, { std::nullopt, std::nullopt, g.mood_now.happiness, static_cast<double>(g.cash.current) }, management_row_id({ ManagementRow::Kind::Guest, i }), mood_icon(happiness_mood(g.mood_now.happiness)) });
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
            average("GuestStats_AverageHappiness", [](const save::model::Guest& g) { return std::optional(g.mood_now.happiness); });
            average("GuestStats_AverageEnergy", [](const save::model::Guest& g) { return std::optional(g.mood_now.energy); });
            average("GuestStats_AverageHunger", [](const save::model::Guest& g) { return std::optional(g.needs.food); });
            average("GuestStats_AverageThirst", [](const save::model::Guest& g) { return std::optional(g.needs.drink); });
            average("GuestStats_AverageToilet", [](const save::model::Guest& g) { return std::optional(g.needs.toilet); });
            average("GuestStats_AverageNausea", [](const save::model::Guest& g) { return std::optional(g.mood_now.nausea); });
            average("GuestStats_AverageSunburn", [](const save::model::Guest& g) { return g.mood_now.sunburn; });
            page.lines.push_back({ text(strings, "GuestStats_GroupsOfType"), { }, false, "GuestStats_GroupsOfType" });
            std::array<std::int64_t, kGroupTypes.size()> types { };
            for (const save::model::GuestGroup& g : f.people->groups)
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
            std::unordered_map<save::model::PeopleObjectId, std::size_t> by_id;
            for (std::size_t i = 0; i < f.people->guests.size(); ++i)
                by_id.emplace(f.people->guests[i].person.id, i);
            const auto name_of = [&](save::model::PeopleRef ref) -> std::optional<std::size_t> {
                const auto it = by_id.find(ref.id);
                return ref && it != by_id.end() ? std::optional(it->second) : std::nullopt;
            };
            for (const save::model::GuestGroup& g : f.people->groups)
            {
                if (g.group_type != kFamilyGroup && !g.avatar)
                    continue;
                const std::optional<std::size_t> leader = name_of(g.leader);
                std::u16string members;
                std::size_t count = 0;
                for (const save::model::PeopleRef m : g.members)
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
            // Fans out open and close, for every attraction the page lists.
            page.buttons = { { "Attraction_General_OpenClose", { }, text(strings, "TT_AttractionListDialogIconOpenCloseAll"), true, { { "Attraction_Button_Open", text(strings, "TT_AttractionDialogIconOpenButton") }, { "Attraction_Button_Closed", text(strings, "TT_AttractionDialogIconClosedButton") } } } };
            if (f.rides == nullptr)
                return;
            const auto common_row = [&](const save::model::AttractionCommon& a, ManagementRow ref) {
                widgets::TableRow row { { attraction_name(a, strings), attraction_status(a, strings), whole_percent(a.popularity), whole_percent(a.satisfaction) }, { std::nullopt, std::nullopt, a.popularity, a.satisfaction }, management_row_id(ref) };
                return row;
            };
            const auto others = [&](widgets::TableRow& row, const save::model::AttractionCommon& a) {
                row.cells.push_back(whole(a.total_guests));
                row.keys.emplace_back(static_cast<double>(a.total_guests));
                row.cells.push_back(money(units, a.total_profit));
                row.keys.emplace_back(static_cast<double>(a.total_profit));
            };
            for (std::size_t i = 0; i < f.rides->rides.size(); ++i)
            {
                const save::model::Ride& r = f.rides->rides[i];
                const bool facility = r.kind == save::model::RideKind::Special;
                if ((tab == AttractionListTab::Rides && facility) || (tab == AttractionListTab::Facilities && !facility) || tab == AttractionListTab::Stalls || r.base.demolished)
                    continue;
                widgets::TableRow row = common_row(r.base, { ManagementRow::Kind::Ride, i });
                if (rides)
                {
                    std::int64_t queue = 0;
                    float wait = 0;
                    for (const save::model::Station& s : r.stations)
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
                if (f.rides->stalls[i].base.demolished)
                    continue;
                widgets::TableRow row = common_row(f.rides->stalls[i].base, { ManagementRow::Kind::Stall, i });
                others(row, f.rides->stalls[i].base);
                page.rows.push_back(std::move(row));
            }
        }

        // ----- objectives -----

        /// Figure `v` of a goal like `c`, in the units its target is kept in.
        std::u16string goal_figure(const save::model::Condition& c, std::int64_t v, const InfoUnits& units)
        {
            switch (c.kind)
            {
                case save::model::ConditionKind::RepayLoan:
                case save::model::ConditionKind::ParkValue:
                case save::model::ConditionKind::RideIncome:
                case save::model::ConditionKind::ShopProfit:
                case save::model::ConditionKind::PoolIncome:
                case save::model::ConditionKind::AnimalSalesProfit:
                case save::model::ConditionKind::GalleryIncome:
                    return money(units, v);
                case save::model::ConditionKind::Coaster:
                case save::model::ConditionKind::ParkGuests:
                case save::model::ConditionKind::ParkRating:
                case save::model::ConditionKind::ItemsBought:
                case save::model::ConditionKind::PoolGuests:
                case save::model::ConditionKind::AnimalsOfSpecies:
                case save::model::ConditionKind::GalleryVisitors:
                case save::model::ConditionKind::AnimalsReleased:
                case save::model::ConditionKind::AnimalHealth:
                case save::model::ConditionKind::Dung:
                case save::model::ConditionKind::ViewingGalleries:
                    return whole(v);
                default:
                    return std::u16string(kDash);
            }
        }

        std::u16string condition_target(const save::model::Condition& c, const InfoUnits& units)
        {
            return c.value && c.kind != save::model::ConditionKind::RepayLoan ? goal_figure(c, *c.value, units) : std::u16string(kDash);
        }

        std::u16string held_for(const save::model::Condition& c, const StringTable& strings)
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
            const save::model::ObjectiveSet& set = f.park->objectives;
            const auto level = static_cast<std::int32_t>(difficulty);
            const auto objective = std::ranges::find_if(set.objectives, [&](const save::model::Objective& o) { return o.difficulty == level && o.objective_id == 0; });
            std::u16string state(kDash);
            if (objective != set.objectives.end() && objective->success)
                state = text(strings, "Objectives_Completed");
            page.lines.push_back({ text(strings, "AttractionListDialogParameterStatus"), state, false, "AttractionListDialogParameterStatus" });
            const bool prize = objective != set.objectives.end() && objective->prize_id > 0 && objective->prize_id < simulation::kApprenticePrize;
            page.lines.push_back({ text(strings, "Objectives_Prize"), prize ? text(strings, simulation::prize_name(objective->prize_id)) : std::u16string(kDash), false, "Objectives_Prize" });
            const std::vector<const save::model::Condition*> conditions = set.conditions_for(level, 0);
            if (conditions.empty())
                page.note = text(strings, kNotRead);
            for (const save::model::Condition* c : conditions)
            {
                // The condition and its target, then how long it must hold and whether it has been met.
                const std::string_view key = simulation::condition_name(c->kind);
                page.lines.push_back({ key.empty() ? std::u16string(kDash) : text(strings, key), condition_target(*c, units), false, std::string(key) });
                // A goal the simulation cannot judge yet says so, rather than looking merely unmet.
                const std::size_t place = static_cast<std::size_t>(c - set.conditions.data());
                const std::optional<simulation::GoalReading> reading = place < f.goals.size() ? std::optional(f.goals[place]) : std::nullopt;
                std::u16string met(kDash);
                if (c->completed)
                    met = text(strings, "Objectives_Completed");
                else if (!simulation::judged(c->kind))
                    met = text(strings, kNotSimulated);
                else if (reading)
                    met = text(strings, reading->met ? kGoalMet : kGoalNotMet);
                page.lines.push_back({ held_for(*c, strings), met });
                // Where the park stands on it now, and how long a goal that must hold has held so far.
                if (c->completed || !reading)
                    continue;
                if (reading->now)
                    page.lines.push_back({ text(strings, kGoalNow), goal_figure(*c, *reading->now, units) });
                if (c->months_to_hold > 0 && f.state != nullptr && reading->met)
                    page.lines.push_back({ text(strings, kGoalHeld), ascii(std::format("{:.1f}", simulation::months_held(*c, f.state->time))) });
            }
            if (const std::u16string goal = text(strings, f.park->park.objective_text); !goal.empty() && page.note.empty())
                page.note = goal;
        }

        // ----- research -----

        void research(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            if (f.research == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const simulation::Research& r = *f.research;
            // What is under way, how far it has got and about how long it has to go at this budget, or that nothing is
            // left.
            const auto progress = f.research_left ? simulation::research_progress(r, *f.research_left) : std::nullopt;
            const bool done = f.research_left && f.research_left->empty();
            page.lines.push_back({ text(strings, kResearching), done ? text(strings, kNothingLeft) : progress ? text(strings, f.researching) : std::u16string(kDash), false, std::string(kResearching) });
            if (progress && !done)
            {
                std::u16string left = progress->months ? text(strings, *progress->months == 1 ? kMonthLeft : kMonthsLeft) : std::u16string(kDash);
                if (const auto at = left.find(u'#'); at != std::u16string::npos)
                    left.replace(at, 1, whole(*progress->months));
                page.lines.push_back({ .label = std::move(left), .value = share(progress->done), .name = std::string(kMonthsLeft), .bar = progress->done });
            }

            page.controls.push_back({ .id = kResearchBudgetControl, .name = "Research_Label_Budget", .kind = InfoControlKind::Number, .label = text(strings, "Research_Label_Budget"), .value = static_cast<int>(std::clamp<std::int64_t>(r.budget, 0, kMostResearchBudget)), .maximum = kMostResearchBudget, .step = kResearchBudgetStep, .shown = money(units, r.budget), .help = text(strings, "Research_TT_AdjustBudget") });
            page.controls.push_back({ .id = -1, .name = "Research_Label_TechTree", .kind = InfoControlKind::Words, .label = text(strings, "Research_Label_TechTree") });
            // The areas the park keeps a priority for, and any other that has something left to invent.
            for (std::size_t i = 0; i < kResearchAreas.size(); ++i)
            {
                const bool left = f.research_left && std::ranges::any_of(*f.research_left, [&](const simulation::ResearchItem& item) { return static_cast<std::size_t>(item.area) == i; });
                if (i >= r.priorities.size() && !left)
                    continue;
                const float priority = i < r.priorities.size() ? std::clamp(r.priorities[i], 0.0f, 1.0f) : 1.0f;
                page.controls.push_back({ .id = kFirstPriorityControl + static_cast<int>(i), .name = std::string(kResearchAreas.at(i)), .kind = InfoControlKind::Bar, .label = text(strings, kResearchAreas.at(i)), .value = static_cast<int>(std::lround(priority * 100)), .maximum = 100, .shown = share(priority), .help = text(strings, "Research_TT_PriorityBar") });
            }
        }

        // ----- challenges -----

        void challenges(ManagementPage& page, const ManagementFacts& f, const StringTable& strings, const InfoUnits& units)
        {
            if (f.park == nullptr)
            {
                page.note = text(strings, kNotRead);
                return;
            }
            const std::vector<save::model::Challenge>& list = f.park->objectives.challenges;
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
                const save::model::Challenge& c = list[i];
                std::u16string name = text(strings, c.name_key);
                if (name.empty())
                    name = strings::to_utf16(c.name_key);
                // The time fields' unit is not known yet.
                page.rows.push_back({ { name, std::u16string(kDash), std::u16string(kDash), money(units, c.prize_cash) }, { std::nullopt, std::nullopt, std::nullopt, static_cast<double>(c.prize_cash) }, management_row_id({ ManagementRow::Kind::None, i }) });
            }
        }
    } // namespace

    std::u16string park_name(const save::model::Park& park, const StringTable& strings)
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
            case ManagementTopic::Guests:
                return tabs(kGuestTabs, strings);
            case ManagementTopic::Attractions:
                return tabs(kAttractionTabs, strings);
            case ManagementTopic::Objectives:
                return tabs(kObjectiveTabs, strings);
            case ManagementTopic::Research:
                return tabs(kResearchTabs, strings);
            case ManagementTopic::Staff: // A window of its own (staff_roster.hpp)
                return { };
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
                    park_graphs(page, facts, strings, units);
                else if (tab == 2)
                    park_campaigns(page, strings);
                else
                    park_awards(page, facts, strings);
                break;
            case ManagementTopic::Finances:
                if (tab == 0)
                    finance_summary(page, facts, strings, units);
                else
                    finance_graphs(page, facts, strings, units);
                break;
            case ManagementTopic::Staff: // A window of its own (staff_roster.hpp)
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

} // namespace rct3::ui::screens
