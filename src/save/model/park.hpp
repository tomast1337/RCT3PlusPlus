// SPDX-License-Identifier: GPL-3.0-or-later
//
// In-memory model of a save's park state, economy, scenario and required content, and a loader that fills
// it from a decoded save.
//
// Design: plain data structs and free functions. Nothing here keeps pointers into the save; strings are
// copied, so a ParkModel may outlive the bytes it was read from.
//
// Loading rules, applied everywhere:
//  - The schema comes from the file. An attribute that is absent (older or newer generations of the format
//    omit different ones) leaves the field at its documented default.
//  - An attribute of an unexpected type is treated as absent.
//  - Enumerated values are kept as integers. Values this code has no name for are never an error.
//  - An object reference that is null or whose target is not in the file means "none" (nullopt / skipped).
//  - A float used as "unset" with a negative sentinel becomes nullopt (see GameClock).
//  - Money is an integer count of hundredths of the currency unit.
#pragma once

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace rct3::save::model
{

    // ---------------------------------------------------------------------------------------------------------
    // Series (graphed values)
    // ---------------------------------------------------------------------------------------------------------

    /// A bounded history of samples, newest first: the first sample is the value as it is now, the slot the
    /// game is still filling. `capacity` is the ring size the game used (64 for park and
    /// money series, larger for per-ride series); `samples.size()` may be anything up to it. A series that was
    /// absent from the file is empty with capacity 0.
    struct Series
    {
        std::vector<f32> samples { };
        u32 capacity = 0;

        [[nodiscard]] bool empty() const noexcept
        {
            return samples.empty();
        }
        [[nodiscard]] std::optional<f32> latest() const noexcept
        {
            if (samples.empty())
                return std::nullopt;
            return samples.front();
        }
        /// Puts a sample in front, dropping the oldest ones so that at most `capacity` remain (capacity 0 keeps all).
        void push(f32 v);
    };

    // ---------------------------------------------------------------------------------------------------------
    // Park
    // ---------------------------------------------------------------------------------------------------------

    /// Named bits of Park::flags. Bits 4 and 6 are used by the game but their meaning is unresolved, so they
    /// have no name; they stay available through ParkFlags::raw.
    enum class ParkFlag : u32
    {
        Open = 1u << 0,               ///< The park is open to guests
        ToolbarGate = 1u << 1,        ///< Enables a toolbar button (which one is not recorded here)
        TreesLocked = 1u << 2,        ///< Trees cannot be removed
        LandPurchaseLocked = 1u << 3, ///< Land cannot be bought
        HarderRating = 1u << 5,       ///< The park's rating starts lower
    };

    struct ParkFlags
    {
        u32 raw = 0;
        [[nodiscard]] bool has(ParkFlag f) const noexcept
        {
            return (raw & static_cast<u32>(f)) != 0;
        }
    };

    inline constexpr i32 kMinutesPerDay = 1440;
    inline constexpr f32 kMaxParkRating = 1000.0f;

    struct Park
    {
        std::string name { };                                 ///< A placeholder text means "player names it"; kept verbatim
        std::string scenario_name { };                        ///< Text key or literal
        std::string scenario_description { };                 ///< Text key
        std::string objective_text { };                       ///< Text key
        std::optional<std::string> rating_breakdown_text { }; ///< Cached breakdown, newer generations only

        ParkFlags flags { };
        i32 opening_minutes = 0;          ///< Minutes since midnight
        i32 closing_minutes = 0;          ///< Minutes since midnight
        f32 guest_generation_rate = 1.0f; ///< Multiplier on the arrival rate
        f32 ride_intensity_preference = 0.5f;
        f32 initial_happiness = 0.5f; ///< 0..1, applied to newly arriving guests
        f32 initial_hunger = 1.0f;
        f32 initial_thirst = 1.0f;
        i32 rating_penalty = 0;             ///< Raw; seen as zero in every sample
        i32 guest_think_cycle = 0;          ///< Raw; seen as zero in every sample
        std::optional<u64> fence_entry { }; ///< Object id of the park fence database entry
        std::string fence_symbol { };       ///< Asset symbol of the fence round the park's land, empty when none
        std::string fence_overlay { };      ///< Overlay holding it, as PlacedObject::overlay
        bool pools_wanted = false;
        bool allow_adult_animals = false;
        bool allow_baby_animals = false;
        bool rescued_animals = false;

        Series guests_in_park { }, adults_in_park { }, teenagers_in_park { }, children_in_park { };
        Series guests_on_way { }, ideal_guests { }, guest_intake { };
        Series adult_appeal { }, teenager_appeal { }, children_appeal { };
        Series assessed_entrance_fee { };
        Series park_rating { }; ///< 0..1000
        Series publicity_level { };

        /// Newest park rating sample, if any.
        [[nodiscard]] std::optional<f32> rating() const noexcept
        {
            return park_rating.latest();
        }
    };

    // ---------------------------------------------------------------------------------------------------------
    // Finance
    // ---------------------------------------------------------------------------------------------------------

    /// Counters of one accounting period. All are integers in the money unit of the save. Members of the second
    /// group exist only in newer generations and stay nullopt when the file has no such attribute. The per-species
    /// animal counters of the newest generation are not loaded.
    struct LedgerPeriod
    {
        i32 food_drink_sales = 0;
        i32 food_drink_stock = 0;
        i32 land_purchase = 0;
        i32 landscaping = 0;
        i32 loan_interest = 0;
        i32 marketing = 0;
        i32 other = 0;
        i32 park_entrance_tickets = 0;
        i32 prize_money = 0;
        i32 research = 0;
        i32 ride_construction = 0;
        i32 ride_running_costs = 0;
        i32 ride_tickets = 0;
        i32 shop_sales = 0;
        i32 shop_stock = 0;
        i32 staff_training = 0;
        i32 staff_wages = 0;

        std::optional<i32> pool_tickets { };
        std::optional<i32> billboards { };
        std::optional<i32> viewing_gallery_tickets { };
        std::optional<i32> other_animal_spending { };

        /// Plain sum of every counter as stored (signs as in the file). For diagnostics, not accounting.
        [[nodiscard]] i64 raw_sum() const noexcept;
    };

    inline constexpr usize kLedgerPeriods = 5;

    struct Finance
    {
        i32 cash = 0;
        i32 loan = 0;
        i32 max_loan = 0;
        i32 entrance_fee = 0;
        f32 interest_rate_percent = 5.0f;
        i32 land_buy_cost = 0;
        i32 land_sell_cost = 0;   ///< Stored negative
        i32 rights_buy_cost = 0;  ///< Construction rights
        i32 rights_sell_cost = 0; ///< Stored negative
        i32 average_guest_cash = 0;
        i32 average_guest_bank_funds = 0;
        u32 flags = 0; ///< Raw; the bits below are the known ones

        /// Set in parks where money never runs out. A setting of the park's own: a new sandbox has it, and any
        /// scenario may; it does not say how the park is played.
        static constexpr u32 kUnlimitedCash = 1u << 0;
        [[nodiscard]] bool unlimited() const noexcept
        {
            return (flags & kUnlimitedCash) != 0;
        }
        /// Set where the player may not change the entrance fee.
        static constexpr u32 kEntranceFeeFixed = 1u << 1;
        /// Set where rides charge nothing.
        static constexpr u32 kFreeRides = 1u << 2;
        [[nodiscard]] bool entrance_fee_fixed() const noexcept
        {
            return (flags & kEntranceFeeFixed) != 0;
        }
        [[nodiscard]] bool free_rides() const noexcept
        {
            return (flags & kFreeRides) != 0;
        }

        Series cash_less_loan { }, company_value { }, expenditure { }, income { }, park_value { }, share_price { }, weekly_profit { };

        /// A period a month, the current one first (that order is assumed, not confirmed). A park file gives up to
        /// kLedgerPeriods; a running park keeps up to kKeptMonths (simulation::park::ledger_month). Empty when the
        /// file gave none.
        std::vector<LedgerPeriod> ledger { };
        std::optional<LedgerPeriod> cumulative { };
    };

    // ---------------------------------------------------------------------------------------------------------
    // Clock
    // ---------------------------------------------------------------------------------------------------------

    struct CalendarDate
    {
        i64 year = 0;         ///< 0-based
        i32 month = 0;        ///< 0..7
        i32 week = 0;         ///< 0..3
        f32 into_week = 0.0f; ///< Units elapsed in the week, 0..90
        i32 day = 1;          ///< Of the month, from 1: the month's units spread over its days (kDaysInMonth)
    };

    struct GameClock
    {
        static constexpr f32 kUnitsPerWeek = 90.0f;
        static constexpr f32 kUnitsPerMonth = 360.0f; ///< 4 weeks
        static constexpr f32 kUnitsPerYear = 2880.0f; ///< 8 months
        /// Days in each of the calendar's months, March to October, as the real calendar has them.
        static constexpr std::array<i32, 8> kDaysInMonth { 31, 30, 31, 30, 31, 31, 30, 31 };

        f32 time = 0.0f;
        f32 zero_time = 0.0f;
        /// Fraction of the day. A negative stored value is the "not set yet" sentinel and gives nullopt.
        std::optional<f32> day_night_phase { };
        std::optional<i32> day_night_mode { };           ///< Newer generations only
        std::optional<f32> day_night_rendered_phase { }; ///< Newest generation only, same sentinel rule

        /// Splits a time in units into calendar parts. Negative and non-finite input is treated as zero.
        [[nodiscard]] static CalendarDate split(f32 units) noexcept;
        /// The date at `when`: the calendar counts its weeks, months and years from `zero_time`.
        [[nodiscard]] static CalendarDate date_at(f32 when, f32 zero_time) noexcept
        {
            return split(when - zero_time);
        }
        [[nodiscard]] CalendarDate date() const noexcept
        {
            return date_at(time, zero_time);
        }
    };

    /// How far back a running park keeps its history: its ledger a period a month (Finance::ledger) and its weekly
    /// graphs a sample a week (Series). Chosen for a long career park; the park file keeps much less.
    inline constexpr usize kKeptYears = 20;
    inline constexpr auto kKeptMonths = static_cast<usize>(kKeptYears * GameClock::kUnitsPerYear / GameClock::kUnitsPerMonth);
    inline constexpr auto kKeptWeeks = static_cast<usize>(kKeptMonths * GameClock::kUnitsPerMonth / GameClock::kUnitsPerWeek);

    // ---------------------------------------------------------------------------------------------------------
    // Awards
    // ---------------------------------------------------------------------------------------------------------

    struct Awards
    {
        u32 available_mask = 0; ///< Bit per award type
        i32 months_before_start = 0;
        std::vector<std::vector<i32>> earned { }; ///< A list a month, of the last six: the kinds of award given that month
        std::vector<i32> prize_money { };         ///< Per award slot; negative for the penalty slots
        std::optional<i32> dangerous_coaster_events { };
        std::optional<i32> dangerous_pool_events { };
    };

    // ---------------------------------------------------------------------------------------------------------
    // Objectives
    // ---------------------------------------------------------------------------------------------------------

    struct Objective
    {
        i32 difficulty = 0;   ///< 0, 1, 2 for the three levels
        i32 objective_id = 0; ///< 0 is the main one; other values are kept raw
        i32 prize_id = 0;     ///< Raw index into the game's prize list; 0..9 are valid, which kind is open
        bool success = false;

        [[nodiscard]] bool prize_in_range() const noexcept
        {
            return prize_id >= 0 && prize_id <= 9;
        }
    };

    /// Condition families the loader recognises by class. Everything else is Other and keeps its class name.
    enum class ConditionKind : u8
    {
        Other,
        ParkValue,
        ParkGuests,
        ParkRating,
        RideIncome,
        ShopProfit,
        RepayLoan,
        Coaster,
        ItemsBought,
        Vip,
        PoolGuests,
        PoolIncome,
        AnimalsOfSpecies,
        AnimalSalesProfit,
        GalleryVisitors, ///< Visitors to the animal viewing galleries
        GalleryIncome,
        AnimalsReleased,
        AnimalHealth, ///< The animals' average health
        Dung,         ///< Dung lying about, at most
        ViewingGalleries,
    };

    /// Named bits of Condition::consistency_flags. Values beyond these bits are preserved in the raw word.
    enum class ConditionFlag : u32
    {
        TimerRunning = 1u << 0,
        Satisfied = 1u << 1,
        HasDeadline = 1u << 2,
        WarnedOnce = 1u << 3,
        WarnedTwice = 1u << 4,
    };

    /// What a coaster condition asks of the park's rides, as the file keeps it.
    struct CoasterTarget
    {
        f32 value = 0; ///< The figure a ride must reach
        /// The low four bits pick the figure: excitement, intensity, nausea, length, most negative and most
        /// positive vertical G, most lateral G, top speed, height span, air time, inversions, highest drop.
        /// kAtMost asks for at most the value rather than at least; kEveryRide that every ride of the group
        /// reaches it; kPoolSlides counts the pools' slides rather than the coasters.
        u32 flags = 0;
        i32 rides = 0; ///< How many rides must reach it

        static constexpr u32 kFigure = 0xf;
        static constexpr u32 kAtMost = 1u << 4;
        static constexpr u32 kEveryRide = 1u << 5;
        static constexpr u32 kPoolSlides = 1u << 6;
    };

    /// What a VIP goal asks: when the guest of honour comes, how long they stay, what they want to see and
    /// what they put up with, as the file keeps it. Times are game time units.
    struct VipTarget
    {
        /// Bits of `wishes`. kLitter and kBreakdowns are things they put up with only so far; the others are
        /// things they want to do while in the park.
        static constexpr u32 kLitter = 1u << 0;
        static constexpr u32 kHappiness = 1u << 1; ///< Their happiness on average must reach target_happiness
        static constexpr u32 kFireworks = 1u << 2;
        static constexpr u32 kThemedArea = 1u << 3;
        static constexpr u32 kCoasters = 1u << 4;
        static constexpr u32 kBreakdowns = 1u << 5;
        static constexpr u32 kAutographs = 1u << 6;
        static constexpr u32 kPool = 1u << 7;
        static constexpr u32 kDung = 1u << 8;
        static constexpr u32 kAnimals = 1u << 9;
        static constexpr u32 kGalleries = 1u << 10;

        u32 wishes = 0;
        u8 who = 0;          ///< Which celebrity comes, 1..6
        f32 first_visit = 0; ///< The first visit's date; later years' visits fall on the same day
        f32 next_visit = 0;
        i32 retry_months = 0; ///< After a visit that went badly, months until they come again
        i32 months_to_stay = 0;
        i32 guards = 0;                                 ///< Bodyguards who come with them
        i32 coasters_needed = 0;                        ///< kCoasters: how many different coasters they want to ride
        f32 min_excitement = 0;                         ///< kCoasters: how exciting each must be, as rides are rated
        i32 theme = 0;                                  ///< kThemedArea: the theme they want to see
        i32 scenery_needed = 0;                         ///< kThemedArea: how many pieces of it
        f32 litter_tolerance = 0;                       ///< Larger takes more litter
        f32 breakdown_tolerance = 0;                    ///< Larger takes more time on broken rides
        f32 autograph_tolerance = 0;                    ///< Larger takes more autograph hunters
        f32 target_happiness = 0;                       ///< kHappiness, 0..1
        bool met = false;                               ///< A visit went well; the goal stays met
        bool warned_month = false, warned_week = false; ///< The player was told the visit is near
    };

    struct Condition
    {
        ConditionKind kind = ConditionKind::Other;
        std::string class_name { }; ///< The file's class, always set
        u64 id = 0;
        i32 difficulty = 0;
        i32 objective_id = 0;
        i32 slot = 0; ///< 0..7 within the objective
        bool completed = false;
        i32 months_to_hold = 0;
        f32 start_time = 0.0f;      ///< Game time units
        f32 next_check_time = 0.0f; ///< Game time units; the deadline when HasDeadline is set
        u32 consistency_flags = 0;
        std::optional<i64> value { };             ///< Threshold, for the families that have one
        std::optional<bool> greater_than { };     ///< Comparison direction, newer generations only
        std::optional<CoasterTarget> coaster { }; ///< Coaster conditions only
        std::optional<VipTarget> vip { };         ///< VIP conditions only

        [[nodiscard]] bool has(ConditionFlag f) const noexcept
        {
            return (consistency_flags & static_cast<u32>(f)) != 0;
        }
    };

    /// A scripted side goal of the newer generations.
    struct Challenge
    {
        std::string name_key { }, description_key { }, acceptance_key { }, completion_key { }; ///< Text keys
        i32 challenge_id = 0;
        i32 state = 0; ///< Raw; labels unknown
        i32 prize_cash = 0;
        i32 prize_publicity = 0;
        i32 prize_research_boost = 0;
        f32 time_limit = 0.0f;
        f32 time_remaining = 0.0f;
        i32 times_postponed = 0;
    };

    struct ObjectiveSet
    {
        std::vector<Objective> objectives { };
        std::vector<Condition> conditions { };
        std::vector<Challenge> challenges { };

        /// Conditions that belong to the given objective, in slot order.
        [[nodiscard]] std::vector<const Condition*> conditions_for(i32 difficulty, i32 objective_id) const;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Research
    // ---------------------------------------------------------------------------------------------------------

    struct ResearchCategory
    {
        std::string name { };
        f32 budget = 0.0f;
        f32 current_level = 0.0f;
        f32 develop_cost = 0.0f;
        bool available = false;
        bool hidden = false;
        std::optional<bool> invented { }; ///< Missing in some older builds
    };

    struct ResearchState
    {
        std::vector<ResearchCategory> categories { };
        bool has_simple_research = false;
        std::optional<u64> current_item { }; ///< Object id of the item being researched
        std::optional<u64> previous_item { };
        i32 money_spent_on_current = 0;
        i32 monthly_budget = 0;
        std::vector<f32> priorities { }; ///< Per category weight, order as in the file
        f32 time_since_last_spend = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Messages
    // ---------------------------------------------------------------------------------------------------------

    struct Message
    {
        u64 id = 0;
        std::string class_name { };
        std::string text_key { };
        f32 time = 0.0f;               ///< Game time units
        std::optional<u64> object { }; ///< Subject object, when the entry kind has one
        std::optional<i32> panel { };
        std::string object_name { };  ///< The subject's name when the message was posted; may be empty
        std::optional<i32> award { }; ///< Award messages: which award
    };

    /// Indices 0..count-1 of a ring buffer visited oldest to newest, given the index the next write will use.
    /// A `next_write` outside the ring is treated as 0.
    [[nodiscard]] std::vector<usize> ring_order(usize count, i64 next_write);

    struct MessageLog
    {
        usize ring_size = 0;              ///< Slots in the ring as stored
        i32 next_write = 0;               ///< As stored
        std::vector<Message> ordered { }; ///< Oldest first; empty and dangling slots are skipped
    };

    // ---------------------------------------------------------------------------------------------------------
    // Required content
    // ---------------------------------------------------------------------------------------------------------

    enum class AssetKind : u8
    {
        Other,
        Scenery,       ///< Scenery pieces and track sections
        Attraction,    ///< Rides, stalls, facilities
        AnimalSpecies, ///< Zoo animals; these record only whether the park has unlocked them
        PathType,      ///< Path styles, named by the style's own name with no overlay
        QueueType,     ///< Queue styles, the same
    };

    /// One database entry of the save: an installed overlay and a symbol within it, plus the research flags. Path and
    /// queue styles name only the style, which goes in `symbol`.
    struct AssetRequirement
    {
        AssetKind kind = AssetKind::Other;
        std::string class_name { };   ///< The file's entry class, always set
        std::string overlay_stem { }; ///< Normalised, install-relative, '/' separated, no extension
        std::string symbol { };
        bool available = false;
        bool hidden = false;
        bool invented = false;
        bool unlocked = false; ///< Animal species only
        u64 id = 0;
    };

    struct RequiredContent
    {
        std::vector<AssetRequirement> entries { }; ///< Entries that name an overlay, and path and queue styles
        usize entries_without_overlay = 0;         ///< Database entries that carry only a symbol or a type name
        [[nodiscard]] usize invented_count() const noexcept;
    };

    /// Normalises an overlay stem as written in a save: backslashes become '/', empty and "." components and
    /// leading separators are dropped, repeated separators collapse, and a trailing ".ovl" (with an optional
    /// ".common"/".unique" part) is removed. Case is preserved. Returns nullopt for an empty result or when any
    /// component is "..", so a stem never escapes the install root.
    [[nodiscard]] std::optional<std::string> normalise_overlay_stem(std::string_view raw);

    struct OverlayFiles
    {
        std::filesystem::path common { };                ///< Existing common half
        std::optional<std::filesystem::path> unique { }; ///< Existing unique half, if present
        bool exact_case = true;                          ///< False when a name matched only ignoring ASCII case
    };

    /// Finds overlay files for a stem under an install root, comparing names case-insensitively (ASCII) one
    /// directory at a time, so it behaves the same on case-sensitive and case-insensitive file systems and with
    /// either slash style in the stem. Directory listings are cached for the lifetime of the object.
    class OverlayResolver
    {
    public:
        explicit OverlayResolver(std::filesystem::path install_root)
            : root_(std::move(install_root))
        {
        }
        /// The overlay pair named by `stem` (normalised or raw), or nullopt when the common half is missing.
        [[nodiscard]] std::optional<OverlayFiles> resolve(std::string_view stem);

    private:
        struct Listing
        {
            bool ok = false;
            /// ASCII-lowercased name -> the real spellings with that lowercase form, sorted.
            std::unordered_map<std::string, std::vector<std::string>> by_lower { };
        };
        const Listing& list(const std::filesystem::path& dir);
        struct Candidate
        {
            std::string name { }; ///< As spelled in the directory
            bool exact { };       ///< Spelled as asked
        };
        /// Entries of `dir` matching `name` ignoring ASCII case, the exact spelling first.
        std::vector<Candidate> candidates(const std::filesystem::path& dir, std::string_view name);
        std::optional<OverlayFiles> descend(const std::filesystem::path& dir, std::span<const std::string_view> parts, bool exact);

        std::filesystem::path root_;
        std::unordered_map<std::string, Listing> cache_;
    };

    /// Convenience: one-shot resolution as a single path (the common half), without caching.
    [[nodiscard]] std::optional<std::filesystem::path> resolve_overlay(const std::filesystem::path& install_root, std::string_view stem);

    struct ContentReport
    {
        usize entries = 0;
        usize resolved = 0;                         ///< Common half found
        usize exact_case = 0;                       ///< Of those, spelled exactly
        usize with_unique = 0;                      ///< Of those, unique half found too
        usize missing = 0;                          ///< entries - resolved
        std::vector<std::string> missing_stems { }; ///< Distinct stems with nothing installed, sorted
    };

    /// Checks every requirement against an install. This is how missing content is reported before anything
    /// else of the save is read.
    [[nodiscard]] ContentReport check_content(const RequiredContent& content, const std::filesystem::path& install_root);

    // ---------------------------------------------------------------------------------------------------------
    // Aggregate and loader
    // ---------------------------------------------------------------------------------------------------------

    /// How often each kind of guest group comes to the park, as relative weights in the order lone teenager,
    /// lone adult, teenage couple, adult couple, teenage gang, adult party, family, day trip. Scenarios carry
    /// them; parks saved after play keep the object without values.
    struct GroupMix
    {
        std::array<f32, 8> weights { };
        f32 single_parent = 0; ///< Chance a family comes with one parent
    };

    struct ParkModel
    {
        Park park { };
        Finance finance { };
        GameClock clock { };
        Awards awards { };
        ObjectiveSet objectives { };
        ResearchState research { };
        MessageLog messages { };
        RequiredContent content { };
        std::optional<GroupMix> group_mix { }; ///< None when the file stores no weights

        // Which singletons the file had; the rest of the model holds defaults when false.
        bool has_park = false;
        bool has_finance = false;
        bool has_clock = false;
        bool has_awards = false;

        /// Particle descriptor, emitter and source objects. They are effect runtime state and are only counted.
        usize particle_objects = 0;
    };

    /// Builds the model. Fails only when an object's framing cannot be decoded, which read_save already rules
    /// out for objects it returned. A save without a park or economy object loads with the has_* flags false.
    [[nodiscard]] std::expected<ParkModel, Error> load_park(const SaveFile& file, const ObjectIndex& index);

    /// Checks invariants of a loaded model and returns one line per violation (empty when sound): rating inside
    /// 0..1000, series no longer than their capacity, opening and closing inside a day, objective prizes in
    /// range, loan within the maximum, finite clock.
    [[nodiscard]] std::vector<std::string> validate(const ParkModel& model);

} // namespace rct3::save::model
