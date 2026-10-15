// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/park.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstring>
#include <set>
#include <system_error>

namespace rct3::save
{

    namespace
    {

        namespace fs = std::filesystem;

        // ---------------------------------------------------------------------------------------------------------
        // Format identifiers. Every class and attribute name the loader keys on lives here and nowhere else.
        // ---------------------------------------------------------------------------------------------------------
        namespace names
        {

            // Classes
            constexpr std::string_view kParkClass = "Park";
            constexpr std::string_view kMoneyClass = "Money";
            constexpr std::string_view kClockClass = "GameTime";
            constexpr std::string_view kAwardsClass = "Awards";
            constexpr std::string_view kObjectiveClass = "Objective";
            constexpr std::string_view kChallengeClass = "Challenge";
            constexpr std::string_view kResearchCategoryClass = "ResearchCategoryInstance";
            constexpr std::string_view kSimpleResearchClass = "SimpleResearch";
            constexpr std::string_view kMessageConsoleClass = "MessageConsole";
            constexpr std::string_view kDatabaseEntrySuffix = "DatabaseEntry";
            constexpr std::string_view kParticlePrefix = "Particle";
            constexpr std::string_view kSceneryEntryClass = "SIDDatabaseEntry";
            constexpr std::string_view kAttractionEntryClass = "AIDDatabaseEntry";
            constexpr std::string_view kWildStyleEntryClass = "WASDatabaseEntry";
            // Condition classes are found by owning the slot attribute; these classes give the family.
            constexpr std::array kConditionKinds = {
                std::pair { std::string_view { "ParkValueCondition" }, ConditionKind::ParkValue },   std::pair { std::string_view { "ParkGuestsCondition" }, ConditionKind::ParkGuests },   std::pair { std::string_view { "ParkRatingCondition" }, ConditionKind::ParkRating },
                std::pair { std::string_view { "RideIncomeCondition" }, ConditionKind::RideIncome }, std::pair { std::string_view { "ShopProfitCondition" }, ConditionKind::ShopProfit },   std::pair { std::string_view { "RepayLoanCondition" }, ConditionKind::RepayLoan },
                std::pair { std::string_view { "CoasterCondition" }, ConditionKind::Coaster },       std::pair { std::string_view { "ItemsBoughtCondition" }, ConditionKind::ItemsBought }, std::pair { std::string_view { "VIPCondition" }, ConditionKind::Vip },
            };

            // Park attributes
            constexpr std::string_view kName = "Name";
            constexpr std::string_view kScenarioName = "ScenarioName";
            constexpr std::string_view kScenarioDescription = "ScenarioDescription";
            constexpr std::string_view kObjectiveText = "ObjectiveText";
            constexpr std::string_view kBreakdownText = "ParkRatingBreakdownText";
            constexpr std::string_view kFlags = "Flags";
            constexpr std::string_view kOpening = "OpeningTimeMinutes";
            constexpr std::string_view kClosing = "ClosingTimeMinutes";
            constexpr std::string_view kGenerationRate = "GuestGenerationRate";
            constexpr std::string_view kIntensity = "GuestRideIntensityPreference";
            constexpr std::string_view kInitialHappiness = "InitialGuestHappiness";
            constexpr std::string_view kInitialHunger = "InitialGuestHunger";
            constexpr std::string_view kInitialThirst = "InitialGuestThirst";
            constexpr std::string_view kThinkCycle = "GuestThinkCycle";
            constexpr std::string_view kRatingPenalty = "ParkRatingPenalty";
            constexpr std::string_view kFenceEntry = "ParkFenceEntry";
            constexpr std::string_view kPools = "PeepsWantToUsePools";
            constexpr std::string_view kAdultAnimals = "AllowAdultAnimals";
            constexpr std::string_view kBabyAnimals = "AllowBabyAnimals";
            constexpr std::string_view kRescued = "RescuedAnimals";
            constexpr std::string_view kGuestsInPark = "GuestsInPark";
            constexpr std::string_view kAdultsInPark = "AdultsInPark";
            constexpr std::string_view kTeenagersInPark = "TeenagersInPark";
            constexpr std::string_view kChildrenInPark = "ChildrenInPark";
            constexpr std::string_view kGuestsOnWay = "GuestsOnWayToPark";
            constexpr std::string_view kIdealGuests = "IdealGuests";
            constexpr std::string_view kGuestIntake = "GuestIntake";
            constexpr std::string_view kAdultAppeal = "AdultAppeal";
            constexpr std::string_view kTeenagerAppeal = "TeenagerAppeal";
            constexpr std::string_view kChildrenAppeal = "ChildrenAppeal";
            constexpr std::string_view kAssessedFee = "AssessedEntranceFee";
            constexpr std::string_view kParkRating = "ParkRating";
            constexpr std::string_view kPublicity = "PublicityLevel";

            // Money attributes
            constexpr std::string_view kCash = "Cash";
            constexpr std::string_view kLoan = "Loan";
            constexpr std::string_view kMaxLoan = "MaxLoan";
            constexpr std::string_view kEntranceFee = "EntranceFee";
            constexpr std::string_view kInterest = "InterestRate";
            constexpr std::string_view kLandBuy = "LandBuyCost";
            constexpr std::string_view kLandSell = "LandSellCost";
            constexpr std::string_view kRightsBuy = "BuildRitesBuyCost";
            constexpr std::string_view kRightsSell = "BuildRitesSellCost";
            constexpr std::string_view kGuestCash = "AverageGuestCash";
            constexpr std::string_view kGuestBank = "AverageGuestATMFunds";
            constexpr std::string_view kCashLessLoan = "CashLessLoan";
            constexpr std::string_view kCompanyValue = "CompanyValue";
            constexpr std::string_view kExpenditure = "Expendature";
            constexpr std::string_view kIncome = "Income";
            constexpr std::string_view kParkValue = "ParkValue";
            constexpr std::string_view kSharePrice = "SharePrice";
            constexpr std::string_view kWeeklyProfit = "WeeklyProfit";
            constexpr std::string_view kLedger = "MonthlyExpendature";
            constexpr std::string_view kCumulative = "CumulativeExpendature";

            // Clock attributes
            constexpr std::string_view kTime = "Time";
            constexpr std::string_view kZeroTime = "ZeroTime";
            constexpr std::string_view kDayNight = "DayNightTime";
            constexpr std::string_view kDayNightMode = "DayNightMode";
            constexpr std::string_view kDayNightRendered = "DayNightTimeAsRendered";

            // Awards attributes
            constexpr std::string_view kAwardsArray = "AwardsArray";
            constexpr std::string_view kAwardTypes = "AwardTypes";
            constexpr std::string_view kAwardType = "AwardType";
            constexpr std::string_view kAwardsAvailable = "AwardsAvailable";
            constexpr std::string_view kMonthsBeforeAwards = "MonthsBeforeAwardsStart";
            constexpr std::string_view kSettingArray = "SettingArray";
            constexpr std::string_view kPrizeMoney = "PrizeMoney";
            constexpr std::string_view kDangerousCoaster = "NumDangerousCoasterEvents";
            constexpr std::string_view kDangerousPool = "NumDangerousPoolEvents";

            // Objective attributes
            constexpr std::string_view kDifficulty = "Difficulty";
            constexpr std::string_view kObjectiveId = "ObjectiveId";
            constexpr std::string_view kPrize = "Prize";
            constexpr std::string_view kSuccess = "Success";
            constexpr std::string_view kConditionId = "ConditionId";
            constexpr std::string_view kCompleted = "Completed";
            constexpr std::string_view kConsistant = "Consistant";
            constexpr std::string_view kConsistantFlags = "ConsistantFlags";
            constexpr std::string_view kStartTime = "StartTime";
            constexpr std::string_view kNextCheckTime = "NextCheckTime";
            constexpr std::string_view kValue = "Value";
            constexpr std::string_view kGreaterThan = "GT";

            // Challenge attributes
            constexpr std::string_view kChallengeName = "Name";
            constexpr std::string_view kChallengeDescription = "Description";
            constexpr std::string_view kChallengeAcceptance = "AcceptanceText";
            constexpr std::string_view kChallengeCompletion = "CompletionText";
            constexpr std::string_view kChallengeId = "ChallengeId";
            constexpr std::string_view kChallengeState = "State";
            constexpr std::string_view kChallengePrizeCash = "PrizeCash";
            constexpr std::string_view kChallengePrizePublicity = "PrizePublicity";
            constexpr std::string_view kChallengePrizeResearch = "PrizeResearchBoost";
            constexpr std::string_view kChallengeTimeLimit = "TimeLimit";
            constexpr std::string_view kChallengeTimeRemaining = "TimeRemaining";
            constexpr std::string_view kChallengePostponed = "NumTimesPostponed";

            // Research attributes
            constexpr std::string_view kCategoryName = "CategoryName";
            constexpr std::string_view kBudget = "Budget";
            constexpr std::string_view kCurrentLevel = "CurrentLevel";
            constexpr std::string_view kDevelopCost = "DevelopCost";
            constexpr std::string_view kIsAvailable = "IsAvailable";
            constexpr std::string_view kIsHidden = "IsHidden";
            constexpr std::string_view kIsInvented = "IsInvented";
            constexpr std::string_view kCurrentItem = "CurrentResearchItem";
            constexpr std::string_view kPreviousItem = "PreviousResearchItem";
            constexpr std::string_view kSpentOnCurrent = "MoneySpentOnCurrent";
            constexpr std::string_view kMonthlyBudget = "MonthlyBudget";
            constexpr std::string_view kPriorities = "ResearchPriorities";
            constexpr std::string_view kPriority = "Priority";
            constexpr std::string_view kSinceLastSpend = "TimeSinceLastSpend";

            // Message attributes
            constexpr std::string_view kMessages = "Messages";
            constexpr std::string_view kNextMessage = "NextMessage";
            constexpr std::string_view kMessageText = "Text";
            constexpr std::string_view kMessageTime = "Time";
            constexpr std::string_view kMessageObject = "Object";
            constexpr std::string_view kMessagePanel = "Panel";
            constexpr std::string_view kMessageObjectName = "ObjectString";
            constexpr std::string_view kMessageAward = "Type";

            // Database entry attributes
            constexpr std::string_view kOverlayFile = "OVERLAYFILENAME";
            constexpr std::string_view kSymbolName = "SYMBOLNAME";
            constexpr std::string_view kEntryAvailable = "IsAvailable";
            constexpr std::string_view kEntryHidden = "IsHidden";
            constexpr std::string_view kEntryInvented = "IsInvented";

            // Graph storage layout (the value decoder's names for the parts of a graphedValue)
            constexpr std::string_view kGraphCapacity = "capacity";
            constexpr std::string_view kGraphValues = "values";

            // Overlay file name parts
            constexpr std::string_view kOvlExt = ".ovl";
            constexpr std::string_view kCommonPart = ".common";
            constexpr std::string_view kUniquePart = ".unique";

        } // namespace names

        // ---------------------------------------------------------------------------------------------------------
        // Attribute access with defaults
        // ---------------------------------------------------------------------------------------------------------

        [[nodiscard]] bool is_numeric(const Value* v) noexcept
        {
            return v && (v->kind() == ValueKind::Integer || v->kind() == ValueKind::Bool);
        }

        [[nodiscard]] std::int64_t int_of(const Value* v, std::int64_t def) noexcept
        {
            return is_numeric(v) ? v->as_integer() : def;
        }

        [[nodiscard]] std::int32_t i32_of(const Value* v, std::int32_t def) noexcept
        {
            return is_numeric(v) ? static_cast<std::int32_t>(v->as_integer()) : def;
        }

        [[nodiscard]] float float_of(const Value* v, float def) noexcept
        {
            if (!v)
                return def;
            if (v->kind() == ValueKind::Float)
                return v->as_float();
            if (v->kind() == ValueKind::Integer)
                return static_cast<float>(v->as_integer());
            return def;
        }

        [[nodiscard]] bool bool_of(const Value* v, bool def) noexcept
        {
            if (!v)
                return def;
            if (v->kind() == ValueKind::Bool)
                return v->as_bool();
            if (v->kind() == ValueKind::Integer)
                return v->as_integer() != 0;
            return def;
        }

        [[nodiscard]] std::string string_of(const Value* v)
        {
            if (v && v->kind() == ValueKind::String)
                return std::string(v->text());
            return { };
        }

        /// The object id a reference points at, when it is non-null and present in the file.
        [[nodiscard]] std::optional<std::uint64_t> ref_of(const Value* v, const ObjectIndex& index) noexcept
        {
            if (!v || v->kind() != ValueKind::Reference || v->as_reference() == 0)
                return std::nullopt;
            if (!index.find(v->as_reference()))
                return std::nullopt;
            return v->as_reference();
        }

        /// An integer that is either the item itself or the named field of a one-field struct.
        [[nodiscard]] std::optional<std::int32_t> item_int(const Value& item, std::string_view field) noexcept
        {
            if (is_numeric(&item))
                return static_cast<std::int32_t>(item.as_integer());
            if (item.kind() == ValueKind::Struct)
            {
                if (const auto* f = item.field(field); is_numeric(f))
                    return static_cast<std::int32_t>(f->as_integer());
            }
            return std::nullopt;
        }

        [[nodiscard]] float load_f32(const std::byte* p) noexcept
        {
            float f;
            std::memcpy(&f, p, 4);
            return f;
        }
        [[nodiscard]] std::uint32_t load_u32(const std::byte* p) noexcept
        {
            std::uint32_t u;
            std::memcpy(&u, p, 4);
            return u;
        }

        /// Series from a graphed value. The value decoder yields a struct for the current layout; an older layout
        /// (capacity, count, floats with no leading marker) arrives as raw bytes and is parsed here.
        [[nodiscard]] Series series_of(const Value* v)
        {
            Series s;
            if (!v)
                return s;
            if (v->kind() == ValueKind::Struct)
            {
                const auto* values = v->field(names::kGraphValues);
                if (!values || values->kind() != ValueKind::FloatArray)
                    return s;
                const auto f = values->floats();
                s.samples.assign(f.begin(), f.end());
                const auto cap = int_of(v->field(names::kGraphCapacity), 0);
                s.capacity = cap > 0 && cap <= 0xFFFFFFFFLL ? static_cast<std::uint32_t>(cap) : 0;
            }
            else if (v->kind() == ValueKind::Raw)
            {
                const auto raw = v->raw();
                if (raw.size() < 8)
                    return s;
                const auto cap = load_u32(raw.data());
                const auto count = load_u32(raw.data() + 4);
                if (count > (raw.size() - 8) / 4 || 8 + std::size_t { count } * 4 != raw.size())
                    return s;
                s.samples.resize(count);
                for (std::uint32_t i = 0; i < count; ++i)
                    s.samples[i] = load_f32(raw.data() + 8 + std::size_t { i } * 4);
                s.capacity = cap;
            }
            return s;
        }

        // ---------------------------------------------------------------------------------------------------------
        // Per-section loaders
        // ---------------------------------------------------------------------------------------------------------

        void load_park_object(const DecodedObject& o, const ObjectIndex& index, Park& p)
        {
            using namespace names;
            p.name = string_of(o.find(kName));
            p.scenario_name = string_of(o.find(kScenarioName));
            p.scenario_description = string_of(o.find(kScenarioDescription));
            p.objective_text = string_of(o.find(kObjectiveText));
            if (const auto* t = o.find(kBreakdownText); t && t->kind() == ValueKind::String)
            {
                p.rating_breakdown_text = std::string(t->text());
            }
            p.flags.raw = static_cast<std::uint32_t>(i32_of(o.find(kFlags), 0));
            p.opening_minutes = i32_of(o.find(kOpening), 0);
            p.closing_minutes = i32_of(o.find(kClosing), 0);
            p.guest_generation_rate = float_of(o.find(kGenerationRate), 1.0f);
            p.ride_intensity_preference = float_of(o.find(kIntensity), 0.5f);
            p.initial_happiness = float_of(o.find(kInitialHappiness), 0.5f);
            p.initial_hunger = float_of(o.find(kInitialHunger), 1.0f);
            p.initial_thirst = float_of(o.find(kInitialThirst), 1.0f);
            p.guest_think_cycle = i32_of(o.find(kThinkCycle), 0);
            p.rating_penalty = i32_of(o.find(kRatingPenalty), 0);
            p.fence_entry = ref_of(o.find(kFenceEntry), index);
            p.pools_wanted = bool_of(o.find(kPools), false);
            p.allow_adult_animals = bool_of(o.find(kAdultAnimals), false);
            p.allow_baby_animals = bool_of(o.find(kBabyAnimals), false);
            p.rescued_animals = bool_of(o.find(kRescued), false);

            p.guests_in_park = series_of(o.find(kGuestsInPark));
            p.adults_in_park = series_of(o.find(kAdultsInPark));
            p.teenagers_in_park = series_of(o.find(kTeenagersInPark));
            p.children_in_park = series_of(o.find(kChildrenInPark));
            p.guests_on_way = series_of(o.find(kGuestsOnWay));
            p.ideal_guests = series_of(o.find(kIdealGuests));
            p.guest_intake = series_of(o.find(kGuestIntake));
            p.adult_appeal = series_of(o.find(kAdultAppeal));
            p.teenager_appeal = series_of(o.find(kTeenagerAppeal));
            p.children_appeal = series_of(o.find(kChildrenAppeal));
            p.assessed_entrance_fee = series_of(o.find(kAssessedFee));
            p.park_rating = series_of(o.find(kParkRating));
            p.publicity_level = series_of(o.find(kPublicity));
        }

        struct RequiredCounter
        {
            std::string_view name;
            std::int32_t LedgerPeriod::* member;
        };
        struct OptionalCounter
        {
            std::string_view name;
            std::optional<std::int32_t> LedgerPeriod::* member;
        };

        constexpr std::array kRequiredCounters = {
            RequiredCounter { "FoodDrinkSales", &LedgerPeriod::food_drink_sales },
            RequiredCounter { "FoodDrinkStock", &LedgerPeriod::food_drink_stock },
            RequiredCounter { "LandPurchase", &LedgerPeriod::land_purchase },
            RequiredCounter { "Landscaping", &LedgerPeriod::landscaping },
            RequiredCounter { "LoanInterest", &LedgerPeriod::loan_interest },
            RequiredCounter { "Marketing", &LedgerPeriod::marketing },
            RequiredCounter { "Other", &LedgerPeriod::other },
            RequiredCounter { "ParkEntranceTickets", &LedgerPeriod::park_entrance_tickets },
            RequiredCounter { "PrizeMoney", &LedgerPeriod::prize_money },
            RequiredCounter { "Research", &LedgerPeriod::research },
            RequiredCounter { "RideConstruction", &LedgerPeriod::ride_construction },
            RequiredCounter { "RideRunningCosts", &LedgerPeriod::ride_running_costs },
            RequiredCounter { "RideTickets", &LedgerPeriod::ride_tickets },
            RequiredCounter { "ShopSales", &LedgerPeriod::shop_sales },
            RequiredCounter { "ShopStock", &LedgerPeriod::shop_stock },
            RequiredCounter { "StaffTraining", &LedgerPeriod::staff_training },
            RequiredCounter { "StaffWages", &LedgerPeriod::staff_wages },
        };

        constexpr std::array kOptionalCounters = {
            OptionalCounter { "PoolTickets", &LedgerPeriod::pool_tickets },
            OptionalCounter { "Billboards", &LedgerPeriod::billboards },
            OptionalCounter { "ViewingGalleryTickets", &LedgerPeriod::viewing_gallery_tickets },
            OptionalCounter { "OtherAnimalSpending", &LedgerPeriod::other_animal_spending },
        };

        [[nodiscard]] LedgerPeriod ledger_of(const Value& v)
        {
            LedgerPeriod p;
            if (v.kind() != ValueKind::Struct)
                return p;
            for (const auto& c : kRequiredCounters)
                p.*c.member = i32_of(v.field(c.name), 0);
            for (const auto& c : kOptionalCounters)
            {
                if (const auto* f = v.field(c.name); is_numeric(f))
                    p.*c.member = static_cast<std::int32_t>(f->as_integer());
            }
            return p;
        }

        void load_money_object(const DecodedObject& o, Finance& f)
        {
            using namespace names;
            f.cash = i32_of(o.find(kCash), 0);
            f.loan = i32_of(o.find(kLoan), 0);
            f.max_loan = i32_of(o.find(kMaxLoan), 0);
            f.entrance_fee = i32_of(o.find(kEntranceFee), 0);
            f.interest_rate_percent = float_of(o.find(kInterest), 5.0f);
            f.land_buy_cost = i32_of(o.find(kLandBuy), 0);
            f.land_sell_cost = i32_of(o.find(kLandSell), 0);
            f.rights_buy_cost = i32_of(o.find(kRightsBuy), 0);
            f.rights_sell_cost = i32_of(o.find(kRightsSell), 0);
            f.average_guest_cash = i32_of(o.find(kGuestCash), 0);
            f.average_guest_bank_funds = i32_of(o.find(kGuestBank), 0);
            f.flags = static_cast<std::uint32_t>(i32_of(o.find(kFlags), 0));
            f.cash_less_loan = series_of(o.find(kCashLessLoan));
            f.company_value = series_of(o.find(kCompanyValue));
            f.expenditure = series_of(o.find(kExpenditure));
            f.income = series_of(o.find(kIncome));
            f.park_value = series_of(o.find(kParkValue));
            f.share_price = series_of(o.find(kSharePrice));
            f.weekly_profit = series_of(o.find(kWeeklyProfit));
            if (const auto* l = o.find(kLedger); l && l->kind() == ValueKind::List)
            {
                const auto items = l->items();
                const auto n = std::min(items.size(), kLedgerPeriods);
                for (std::size_t i = 0; i < n; ++i)
                    f.ledger[i] = ledger_of(items[i]);
                f.ledger_periods_loaded = n;
            }
            if (const auto* c = o.find(kCumulative); c && c->kind() == ValueKind::Struct)
                f.cumulative = ledger_of(*c);
        }

        [[nodiscard]] std::optional<float> unset_if_negative(const Value* v) noexcept
        {
            if (!v || v->kind() != ValueKind::Float)
                return std::nullopt;
            const float f = v->as_float();
            if (!(f >= 0.0f))
                return std::nullopt; // negative sentinel, also rejects NaN
            return f;
        }

        void load_clock_object(const DecodedObject& o, GameClock& c)
        {
            using namespace names;
            c.time = float_of(o.find(kTime), 0.0f);
            c.zero_time = float_of(o.find(kZeroTime), 0.0f);
            c.day_night_phase = unset_if_negative(o.find(kDayNight));
            if (const auto* m = o.find(kDayNightMode); is_numeric(m))
                c.day_night_mode = static_cast<std::int32_t>(m->as_integer());
            c.day_night_rendered_phase = unset_if_negative(o.find(kDayNightRendered));
        }

        void load_awards_object(const DecodedObject& o, Awards& a)
        {
            using namespace names;
            a.available_mask = static_cast<std::uint32_t>(i32_of(o.find(kAwardsAvailable), 0));
            a.months_before_start = i32_of(o.find(kMonthsBeforeAwards), 0);
            if (const auto* arr = o.find(kAwardsArray); arr && arr->kind() == ValueKind::List)
            {
                for (const auto& cat : arr->items())
                {
                    std::vector<std::int32_t> ids;
                    const Value* list = &cat;
                    if (cat.kind() == ValueKind::Struct)
                        list = cat.field(kAwardTypes);
                    if (list && list->kind() == ValueKind::List)
                    {
                        for (const auto& item : list->items())
                        {
                            if (const auto id = item_int(item, kAwardType))
                                ids.push_back(*id);
                        }
                    }
                    a.earned.push_back(std::move(ids));
                }
            }
            if (const auto* arr = o.find(kSettingArray); arr && arr->kind() == ValueKind::List)
            {
                for (const auto& item : arr->items())
                    a.prize_money.push_back(item_int(item, kPrizeMoney).value_or(0));
            }
            if (const auto* v = o.find(kDangerousCoaster); is_numeric(v))
                a.dangerous_coaster_events = i32_of(v, 0);
            if (const auto* v = o.find(kDangerousPool); is_numeric(v))
                a.dangerous_pool_events = i32_of(v, 0);
        }

        void load_objective_object(const DecodedObject& o, ObjectiveSet& set)
        {
            using namespace names;
            Objective ob;
            ob.difficulty = i32_of(o.find(kDifficulty), 0);
            ob.objective_id = i32_of(o.find(kObjectiveId), 0);
            ob.prize_id = i32_of(o.find(kPrize), 0);
            ob.success = bool_of(o.find(kSuccess), false);
            set.objectives.push_back(ob);
        }

        void load_condition_object(const DecodedObject& o, ObjectiveSet& set)
        {
            using namespace names;
            Condition c;
            c.class_name = std::string(o.class_name);
            for (const auto& [name, kind] : kConditionKinds)
            {
                if (o.class_name == name)
                    c.kind = kind;
            }
            c.id = o.id;
            c.difficulty = i32_of(o.find(kDifficulty), 0);
            c.objective_id = i32_of(o.find(kObjectiveId), 0);
            c.slot = i32_of(o.find(kConditionId), 0);
            c.completed = bool_of(o.find(kCompleted), false);
            c.months_to_hold = i32_of(o.find(kConsistant), 0);
            c.start_time = float_of(o.find(kStartTime), 0.0f);
            c.next_check_time = float_of(o.find(kNextCheckTime), 0.0f);
            c.consistency_flags = static_cast<std::uint32_t>(i32_of(o.find(kConsistantFlags), 0));
            if (const auto* v = o.find(kValue); is_numeric(v))
                c.value = v->as_integer();
            if (const auto* v = o.find(kGreaterThan); v && v->kind() == ValueKind::Bool)
                c.greater_than = v->as_bool();
            set.conditions.push_back(std::move(c));
        }

        void load_challenge_object(const DecodedObject& o, ObjectiveSet& set)
        {
            using namespace names;
            Challenge c;
            c.name_key = string_of(o.find(kChallengeName));
            c.description_key = string_of(o.find(kChallengeDescription));
            c.acceptance_key = string_of(o.find(kChallengeAcceptance));
            c.completion_key = string_of(o.find(kChallengeCompletion));
            c.challenge_id = i32_of(o.find(kChallengeId), 0);
            c.state = i32_of(o.find(kChallengeState), 0);
            c.prize_cash = i32_of(o.find(kChallengePrizeCash), 0);
            c.prize_publicity = i32_of(o.find(kChallengePrizePublicity), 0);
            c.prize_research_boost = i32_of(o.find(kChallengePrizeResearch), 0);
            c.time_limit = float_of(o.find(kChallengeTimeLimit), 0.0f);
            c.time_remaining = float_of(o.find(kChallengeTimeRemaining), 0.0f);
            c.times_postponed = i32_of(o.find(kChallengePostponed), 0);
            set.challenges.push_back(std::move(c));
        }

        void load_category_object(const DecodedObject& o, ResearchState& r)
        {
            using namespace names;
            ResearchCategory c;
            c.name = string_of(o.find(kCategoryName));
            c.budget = float_of(o.find(kBudget), 0.0f);
            c.current_level = float_of(o.find(kCurrentLevel), 0.0f);
            c.develop_cost = float_of(o.find(kDevelopCost), 0.0f);
            c.available = bool_of(o.find(kIsAvailable), false);
            c.hidden = bool_of(o.find(kIsHidden), false);
            if (const auto* v = o.find(kIsInvented); v && v->kind() == ValueKind::Bool)
                c.invented = v->as_bool();
            r.categories.push_back(std::move(c));
        }

        void load_simple_research_object(const DecodedObject& o, const ObjectIndex& index, ResearchState& r)
        {
            using namespace names;
            r.has_simple_research = true;
            r.current_item = ref_of(o.find(kCurrentItem), index);
            r.previous_item = ref_of(o.find(kPreviousItem), index);
            r.money_spent_on_current = i32_of(o.find(kSpentOnCurrent), 0);
            r.monthly_budget = i32_of(o.find(kMonthlyBudget), 0);
            r.time_since_last_spend = float_of(o.find(kSinceLastSpend), 0.0f);
            if (const auto* p = o.find(kPriorities); p && p->kind() == ValueKind::List)
            {
                for (const auto& item : p->items())
                {
                    if (item.kind() == ValueKind::Float)
                        r.priorities.push_back(item.as_float());
                    else if (item.kind() == ValueKind::Struct)
                        r.priorities.push_back(float_of(item.field(kPriority), 0.0f));
                }
            }
        }

        [[nodiscard]] std::string lower_ascii(std::string_view s)
        {
            std::string out(s);
            for (auto& c : out)
                c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
            return out;
        }

        [[nodiscard]] bool ends_with_ci(std::string_view s, std::string_view suffix) noexcept
        {
            if (s.size() < suffix.size())
                return false;
            const auto tail = s.substr(s.size() - suffix.size());
            for (std::size_t i = 0; i < suffix.size(); ++i)
            {
                if (std::tolower(static_cast<unsigned char>(tail[i])) != std::tolower(static_cast<unsigned char>(suffix[i])))
                {
                    return false;
                }
            }
            return true;
        }

        [[nodiscard]] AssetKind kind_of_entry(std::string_view cls) noexcept
        {
            if (cls == names::kSceneryEntryClass)
                return AssetKind::Scenery;
            if (cls == names::kAttractionEntryClass)
                return AssetKind::Attraction;
            if (cls == names::kWildStyleEntryClass)
                return AssetKind::WildAreaStyle;
            return AssetKind::Other;
        }

        enum class Role : std::uint8_t
        {
            None,
            Park,
            Money,
            Clock,
            Awards,
            Objective,
            Condition,
            Challenge,
            Category,
            SimpleResearch,
            Console,
            DatabaseEntry,
            Particle,
        };

        [[nodiscard]] Role role_of(const Class& c)
        {
            using namespace names;
            const auto n = c.name;
            if (n == kParkClass)
                return Role::Park;
            if (n == kMoneyClass)
                return Role::Money;
            if (n == kClockClass)
                return Role::Clock;
            if (n == kAwardsClass)
                return Role::Awards;
            if (n == kObjectiveClass)
                return Role::Objective;
            if (n == kChallengeClass)
                return Role::Challenge;
            if (n == kResearchCategoryClass)
                return Role::Category;
            if (n == kSimpleResearchClass)
                return Role::SimpleResearch;
            if (n == kMessageConsoleClass)
                return Role::Console;
            if (n.size() > kDatabaseEntrySuffix.size() && n.ends_with(kDatabaseEntrySuffix))
                return Role::DatabaseEntry;
            if (n.starts_with(kParticlePrefix))
                return Role::Particle;
            for (const auto& a : c.attributes)
            {
                if (a.name == kConditionId)
                    return Role::Condition;
            }
            return Role::None;
        }

        void load_entry_object(const DecodedObject& o, RequiredContent& content)
        {
            using namespace names;
            const auto* overlay = o.find(kOverlayFile);
            std::optional<std::string> stem;
            if (overlay && overlay->kind() == ValueKind::String)
                stem = normalise_overlay_stem(overlay->text());
            if (!stem)
            {
                ++content.entries_without_overlay;
                return;
            }
            AssetRequirement r;
            r.kind = kind_of_entry(o.class_name);
            r.class_name = std::string(o.class_name);
            r.overlay_stem = std::move(*stem);
            r.symbol = string_of(o.find(kSymbolName));
            r.available = bool_of(o.find(kEntryAvailable), false);
            r.hidden = bool_of(o.find(kEntryHidden), false);
            r.invented = bool_of(o.find(kEntryInvented), false);
            r.id = o.id;
            content.entries.push_back(std::move(r));
        }

        void load_console(const SaveFile& file, const DecodedObject& o, const ObjectIndex& index, MessageLog& log)
        {
            using namespace names;
            const auto* list = o.find(kMessages);
            if (!list || list->kind() != ValueKind::List)
                return;
            const auto slots = list->items();
            log.ring_size = slots.size();
            log.next_write = i32_of(o.find(kNextMessage), 0);
            for (const auto slot : ring_order(slots.size(), log.next_write))
            {
                const auto& ref = slots[slot];
                if (ref.kind() != ValueKind::Reference || ref.as_reference() == 0)
                    continue;
                const auto* target = index.find(ref.as_reference());
                if (!target)
                    continue;
                const auto decoded = decode_object(file, *target);
                if (!decoded)
                    continue;
                const auto* text = decoded->find(kMessageText);
                const auto* time = decoded->find(kMessageTime);
                if (!text && !time)
                    continue; // not a message entry
                Message m;
                m.id = decoded->id;
                m.class_name = std::string(decoded->class_name);
                m.text_key = string_of(text);
                m.time = float_of(time, 0.0f);
                m.object = ref_of(decoded->find(kMessageObject), index);
                if (const auto* p = decoded->find(kMessagePanel); is_numeric(p))
                    m.panel = i32_of(p, 0);
                m.object_name = string_of(decoded->find(kMessageObjectName));
                if (const auto* a = decoded->find(kMessageAward); is_numeric(a))
                    m.award = i32_of(a, 0);
                log.ordered.push_back(std::move(m));
            }
        }

    } // namespace

    // ---------------------------------------------------------------------------------------------------------
    // Public helpers
    // ---------------------------------------------------------------------------------------------------------

    void Series::push(float v)
    {
        samples.insert(samples.begin(), v);
        if (capacity > 0 && samples.size() > capacity)
            samples.resize(capacity);
    }

    std::int64_t LedgerPeriod::raw_sum() const noexcept
    {
        std::int64_t sum = 0;
        for (const auto& c : kRequiredCounters)
            sum += this->*c.member;
        for (const auto& c : kOptionalCounters)
            sum += (this->*c.member).value_or(0);
        return sum;
    }

    CalendarDate GameClock::split(float units) noexcept
    {
        CalendarDate d;
        if (!(units > 0.0f) || !std::isfinite(units))
            return d;
        const double u = units;
        const double years = std::floor(u / kUnitsPerYear);
        d.year = years >= 9.0e18 ? std::int64_t { 9'000'000'000'000'000'000 } : static_cast<std::int64_t>(years);
        const double in_year = std::fmod(u, static_cast<double>(kUnitsPerYear));
        d.month = static_cast<std::int32_t>(in_year / kUnitsPerMonth);
        const double in_month = std::fmod(in_year, static_cast<double>(kUnitsPerMonth));
        d.week = static_cast<std::int32_t>(in_month / kUnitsPerWeek);
        d.into_week = static_cast<float>(std::fmod(in_month, static_cast<double>(kUnitsPerWeek)));
        return d;
    }

    std::vector<const Condition*> ObjectiveSet::conditions_for(std::int32_t difficulty, std::int32_t objective_id) const
    {
        std::vector<const Condition*> out;
        for (const auto& c : conditions)
        {
            if (c.difficulty == difficulty && c.objective_id == objective_id)
                out.push_back(&c);
        }
        std::stable_sort(out.begin(), out.end(), [](const Condition* a, const Condition* b) { return a->slot < b->slot; });
        return out;
    }

    std::vector<std::size_t> ring_order(std::size_t count, std::int64_t next_write)
    {
        std::vector<std::size_t> out;
        if (count == 0)
            return out;
        const std::size_t start = next_write >= 0 && static_cast<std::uint64_t>(next_write) < count ? static_cast<std::size_t>(next_write) : 0;
        out.reserve(count);
        for (std::size_t i = 0; i < count; ++i)
            out.push_back((start + i) % count);
        return out;
    }

    std::size_t RequiredContent::invented_count() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(entries.begin(), entries.end(), [](const AssetRequirement& r) { return r.invented; }));
    }

    std::optional<std::string> normalise_overlay_stem(std::string_view raw)
    {
        std::string out;
        std::size_t i = 0;
        while (i < raw.size())
        {
            std::size_t j = i;
            while (j < raw.size() && raw[j] != '/' && raw[j] != '\\')
                ++j;
            const auto part = raw.substr(i, j - i);
            i = j + 1;
            if (part.empty() || part == ".")
                continue;
            if (part == "..")
                return std::nullopt;
            // A part containing a NUL or a drive-like colon is not a plain relative name.
            if (part.find('\0') != std::string_view::npos || part.find(':') != std::string_view::npos)
                return std::nullopt;
            if (!out.empty())
                out += '/';
            out += part;
        }
        if (ends_with_ci(out, names::kOvlExt))
        {
            out.resize(out.size() - names::kOvlExt.size());
            if (ends_with_ci(out, names::kCommonPart))
                out.resize(out.size() - names::kCommonPart.size());
            else if (ends_with_ci(out, names::kUniquePart))
                out.resize(out.size() - names::kUniquePart.size());
        }
        if (out.empty())
            return std::nullopt;
        return out;
    }

    const OverlayResolver::Listing& OverlayResolver::list(const fs::path& dir)
    {
        auto [it, inserted] = cache_.try_emplace(dir.string());
        if (!inserted)
            return it->second;
        std::error_code ec;
        fs::directory_iterator iter(dir, fs::directory_options::skip_permission_denied, ec);
        if (ec)
            return it->second;
        it->second.ok = true;
        for (const fs::directory_iterator end; iter != end; iter.increment(ec))
        {
            if (ec)
                break;
            auto name = iter->path().filename().string();
            auto key = lower_ascii(name);
            it->second.by_lower[std::move(key)].push_back(std::move(name));
        }
        for (auto& [_, v] : it->second.by_lower)
            std::sort(v.begin(), v.end());
        return it->second;
    }

    std::vector<std::pair<std::string, bool>> OverlayResolver::candidates(const fs::path& dir, std::string_view name)
    {
        std::vector<std::pair<std::string, bool>> out;
        const auto& l = list(dir);
        if (!l.ok)
            return out;
        const auto it = l.by_lower.find(lower_ascii(name));
        if (it == l.by_lower.end())
            return out;
        for (const auto& real : it->second)
        {
            if (real == name)
                out.emplace_back(real, true);
        }
        for (const auto& real : it->second)
        {
            if (real != name)
                out.emplace_back(real, false);
        }
        return out;
    }

    std::optional<OverlayFiles> OverlayResolver::descend(const fs::path& dir, std::span<const std::string_view> parts, bool exact)
    {
        if (parts.size() > 1)
        {
            // Directories that differ only by case can coexist on a case-sensitive file system, so try each.
            for (const auto& [real, same] : candidates(dir, parts.front()))
            {
                if (auto r = descend(dir / real, parts.subspan(1), exact && same))
                    return r;
            }
            return std::nullopt;
        }
        const std::string base(parts.front());
        const auto common = candidates(dir, base + std::string(names::kCommonPart) + std::string(names::kOvlExt));
        if (common.empty())
            return std::nullopt;
        OverlayFiles out;
        out.common = dir / common.front().first;
        out.exact_case = exact && common.front().second;
        const auto unique = candidates(dir, base + std::string(names::kUniquePart) + std::string(names::kOvlExt));
        if (!unique.empty())
        {
            out.unique = dir / unique.front().first;
            out.exact_case = out.exact_case && unique.front().second;
        }
        return out;
    }

    std::optional<OverlayFiles> OverlayResolver::resolve(std::string_view stem)
    {
        const auto norm = normalise_overlay_stem(stem);
        if (!norm)
            return std::nullopt;
        std::vector<std::string_view> parts;
        for (std::size_t i = 0; i <= norm->size();)
        {
            const auto j = norm->find('/', i);
            const auto end = j == std::string::npos ? norm->size() : j;
            parts.emplace_back(std::string_view(*norm).substr(i, end - i));
            i = end + 1;
        }
        return descend(root_, parts, true);
    }

    std::optional<fs::path> resolve_overlay(const fs::path& install_root, std::string_view stem)
    {
        OverlayResolver r(install_root);
        if (auto f = r.resolve(stem))
            return std::move(f->common);
        return std::nullopt;
    }

    ContentReport check_content(const RequiredContent& content, const fs::path& install_root)
    {
        ContentReport rep;
        OverlayResolver resolver(install_root);
        std::set<std::string> missing;
        std::unordered_map<std::string, std::optional<OverlayFiles>> memo;
        for (const auto& e : content.entries)
        {
            ++rep.entries;
            auto it = memo.find(e.overlay_stem);
            if (it == memo.end())
                it = memo.emplace(e.overlay_stem, resolver.resolve(e.overlay_stem)).first;
            if (!it->second)
            {
                ++rep.missing;
                missing.insert(e.overlay_stem);
                continue;
            }
            ++rep.resolved;
            if (it->second->exact_case)
                ++rep.exact_case;
            if (it->second->unique)
                ++rep.with_unique;
        }
        rep.missing_stems.assign(missing.begin(), missing.end());
        return rep;
    }

    std::expected<ParkModel, Error> load_park(const SaveFile& file, const ObjectIndex& index)
    {
        std::vector<Role> roles(file.classes.size());
        for (std::size_t i = 0; i < file.classes.size(); ++i)
            roles[i] = role_of(file.classes[i]);

        ParkModel m;
        const Object* console = nullptr;
        for (const auto& obj : file.objects)
        {
            if (obj.class_index >= roles.size())
                continue;
            const Role role = roles[obj.class_index];
            if (role == Role::None)
                continue;
            if (role == Role::Particle)
            {
                ++m.particle_objects;
                continue;
            }
            if (role == Role::Park && m.has_park)
                continue;
            if (role == Role::Money && m.has_finance)
                continue;
            if (role == Role::Clock && m.has_clock)
                continue;
            if (role == Role::Awards && m.has_awards)
                continue;
            if (role == Role::Console)
            {
                if (!console)
                    console = &obj;
                continue;
            }
            if (role == Role::SimpleResearch && m.research.has_simple_research)
                continue;

            const auto decoded = decode_object(file, obj);
            if (!decoded)
                return std::unexpected(decoded.error());
            switch (role)
            {
                case Role::Park:
                    load_park_object(*decoded, index, m.park);
                    m.has_park = true;
                    break;
                case Role::Money:
                    load_money_object(*decoded, m.finance);
                    m.has_finance = true;
                    break;
                case Role::Clock:
                    load_clock_object(*decoded, m.clock);
                    m.has_clock = true;
                    break;
                case Role::Awards:
                    load_awards_object(*decoded, m.awards);
                    m.has_awards = true;
                    break;
                case Role::Objective:
                    load_objective_object(*decoded, m.objectives);
                    break;
                case Role::Condition:
                    load_condition_object(*decoded, m.objectives);
                    break;
                case Role::Challenge:
                    load_challenge_object(*decoded, m.objectives);
                    break;
                case Role::Category:
                    load_category_object(*decoded, m.research);
                    break;
                case Role::SimpleResearch:
                    load_simple_research_object(*decoded, index, m.research);
                    break;
                case Role::DatabaseEntry:
                    load_entry_object(*decoded, m.content);
                    break;
                case Role::None:
                case Role::Console:
                case Role::Particle:
                    break;
            }
        }
        if (console)
        {
            const auto decoded = decode_object(file, *console);
            if (!decoded)
                return std::unexpected(decoded.error());
            load_console(file, *decoded, index, m.messages);
        }
        return m;
    }

    std::vector<std::string> validate(const ParkModel& m)
    {
        std::vector<std::string> out;
        const auto add = [&](std::string s) { out.push_back(std::move(s)); };
        const auto check_series = [&](const char* label, const Series& s) {
            if (s.capacity > 0 && s.samples.size() > s.capacity)
                add(std::string(label) + ": more samples than capacity");
            for (const float v : s.samples)
            {
                if (!std::isfinite(v))
                {
                    add(std::string(label) + ": non-finite sample");
                    break;
                }
            }
        };
        if (!m.has_park)
            add("no park object");
        if (!m.has_finance)
            add("no money object");
        if (!m.has_clock)
            add("no game time object");

        const Park& p = m.park;
        for (const float v : p.park_rating.samples)
        {
            if (std::isfinite(v) && (v < 0.0f || v > kMaxParkRating))
            {
                add("park rating sample outside 0..1000");
                break;
            }
        }
        if (p.opening_minutes < 0 || p.opening_minutes >= kMinutesPerDay)
            add("opening time outside a day");
        if (p.closing_minutes < 0 || p.closing_minutes >= kMinutesPerDay)
            add("closing time outside a day");
        check_series("guests_in_park", p.guests_in_park);
        check_series("park_rating", p.park_rating);
        check_series("publicity_level", p.publicity_level);
        check_series("park_value", m.finance.park_value);
        check_series("cash_less_loan", m.finance.cash_less_loan);
        check_series("income", m.finance.income);
        check_series("expenditure", m.finance.expenditure);

        const Finance& f = m.finance;
        if (f.loan < 0)
            add("negative loan");
        if (f.max_loan > 0 && f.loan > f.max_loan)
            add("loan above maximum loan");
        if (!std::isfinite(f.interest_rate_percent))
            add("non-finite interest rate");

        if (!std::isfinite(m.clock.time) || m.clock.time < 0.0f)
            add("clock time not a non-negative number");
        for (const auto& o : m.objectives.objectives)
        {
            if (!o.prize_in_range())
                add("objective prize id outside 0..9");
        }
        for (const auto& c : m.objectives.conditions)
        {
            if (c.slot < 0 || c.slot > 7)
                add("condition slot outside 0..7");
        }
        return out;
    }

} // namespace rct3::save
