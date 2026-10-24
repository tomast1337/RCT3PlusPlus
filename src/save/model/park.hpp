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

namespace rct3::save
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
        std::vector<float> samples { };
        std::uint32_t capacity = 0;

        [[nodiscard]] bool empty() const noexcept
        {
            return samples.empty();
        }
        [[nodiscard]] std::optional<float> latest() const noexcept
        {
            if (samples.empty())
                return std::nullopt;
            return samples.front();
        }
        /// Puts a sample in front, dropping the oldest ones so that at most `capacity` remain (capacity 0 keeps all).
        void push(float v);
    };

    // ---------------------------------------------------------------------------------------------------------
    // Park
    // ---------------------------------------------------------------------------------------------------------

    /// Named bits of Park::flags. Bits 4 and 6 are used by the game but their meaning is unresolved, so they
    /// have no name; they stay available through ParkFlags::raw.
    enum class ParkFlag : std::uint32_t
    {
        Open = 1u << 0,               ///< The park is open to guests
        ToolbarGate = 1u << 1,        ///< Enables a toolbar button (which one is not recorded here)
        TreesLocked = 1u << 2,        ///< Trees cannot be removed
        LandPurchaseLocked = 1u << 3, ///< Land cannot be bought
        HarderRating = 1u << 5,       ///< The park's rating starts lower
    };

    struct ParkFlags
    {
        std::uint32_t raw = 0;
        [[nodiscard]] bool has(ParkFlag f) const noexcept
        {
            return (raw & static_cast<std::uint32_t>(f)) != 0;
        }
    };

    inline constexpr std::int32_t kMinutesPerDay = 1440;
    inline constexpr float kMaxParkRating = 1000.0f;

    struct Park
    {
        std::string name { };                                 ///< A placeholder text means "player names it"; kept verbatim
        std::string scenario_name { };                        ///< Text key or literal
        std::string scenario_description { };                 ///< Text key
        std::string objective_text { };                       ///< Text key
        std::optional<std::string> rating_breakdown_text { }; ///< Cached breakdown, newer generations only

        ParkFlags flags { };
        std::int32_t opening_minutes = 0;   ///< Minutes since midnight
        std::int32_t closing_minutes = 0;   ///< Minutes since midnight
        float guest_generation_rate = 1.0f; ///< Multiplier on the arrival rate
        float ride_intensity_preference = 0.5f;
        float initial_happiness = 0.5f; ///< 0..1, applied to newly arriving guests
        float initial_hunger = 1.0f;
        float initial_thirst = 1.0f;
        std::int32_t rating_penalty = 0;              ///< Raw; seen as zero in every sample
        std::int32_t guest_think_cycle = 0;           ///< Raw; seen as zero in every sample
        std::optional<std::uint64_t> fence_entry { }; ///< Object id of the park fence database entry
        std::string fence_symbol { };                 ///< Asset symbol of the fence round the park's land, empty when none
        std::string fence_overlay { };                ///< Overlay holding it, as PlacedObject::overlay
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
        [[nodiscard]] std::optional<float> rating() const noexcept
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
        std::int32_t food_drink_sales = 0;
        std::int32_t food_drink_stock = 0;
        std::int32_t land_purchase = 0;
        std::int32_t landscaping = 0;
        std::int32_t loan_interest = 0;
        std::int32_t marketing = 0;
        std::int32_t other = 0;
        std::int32_t park_entrance_tickets = 0;
        std::int32_t prize_money = 0;
        std::int32_t research = 0;
        std::int32_t ride_construction = 0;
        std::int32_t ride_running_costs = 0;
        std::int32_t ride_tickets = 0;
        std::int32_t shop_sales = 0;
        std::int32_t shop_stock = 0;
        std::int32_t staff_training = 0;
        std::int32_t staff_wages = 0;

        std::optional<std::int32_t> pool_tickets { };
        std::optional<std::int32_t> billboards { };
        std::optional<std::int32_t> viewing_gallery_tickets { };
        std::optional<std::int32_t> other_animal_spending { };

        /// Plain sum of every counter as stored (signs as in the file). For diagnostics, not accounting.
        [[nodiscard]] std::int64_t raw_sum() const noexcept;
    };

    inline constexpr std::size_t kLedgerPeriods = 5;

    struct Finance
    {
        std::int32_t cash = 0;
        std::int32_t loan = 0;
        std::int32_t max_loan = 0;
        std::int32_t entrance_fee = 0;
        float interest_rate_percent = 5.0f;
        std::int32_t land_buy_cost = 0;
        std::int32_t land_sell_cost = 0;   ///< Stored negative
        std::int32_t rights_buy_cost = 0;  ///< Construction rights
        std::int32_t rights_sell_cost = 0; ///< Stored negative
        std::int32_t average_guest_cash = 0;
        std::int32_t average_guest_bank_funds = 0;
        std::uint32_t flags = 0; ///< Raw; the bits below are the known ones

        /// Set in parks where money never runs out, as in sandbox ones.
        static constexpr std::uint32_t kUnlimitedCash = 1u << 0;
        [[nodiscard]] bool unlimited() const noexcept
        {
            return (flags & kUnlimitedCash) != 0;
        }
        /// Set where the player may not change the entrance fee.
        static constexpr std::uint32_t kEntranceFeeFixed = 1u << 1;
        /// Set where rides charge nothing.
        static constexpr std::uint32_t kFreeRides = 1u << 2;
        [[nodiscard]] bool entrance_fee_fixed() const noexcept
        {
            return (flags & kEntranceFeeFixed) != 0;
        }
        [[nodiscard]] bool free_rides() const noexcept
        {
            return (flags & kFreeRides) != 0;
        }

        Series cash_less_loan { }, company_value { }, expenditure { }, income { }, park_value { }, share_price { }, weekly_profit { };

        /// Five periods, the current one first (that order is assumed, not confirmed). Periods the file did not
        /// provide are default-constructed; `ledger_periods_loaded` says how many were real.
        std::array<LedgerPeriod, kLedgerPeriods> ledger { };
        std::size_t ledger_periods_loaded = 0;
        std::optional<LedgerPeriod> cumulative { };
    };

    // ---------------------------------------------------------------------------------------------------------
    // Clock
    // ---------------------------------------------------------------------------------------------------------

    struct CalendarDate
    {
        std::int64_t year = 0;  ///< 0-based
        std::int32_t month = 0; ///< 0..7
        std::int32_t week = 0;  ///< 0..3
        float into_week = 0.0f; ///< Units elapsed in the week, 0..90
    };

    struct GameClock
    {
        static constexpr float kUnitsPerWeek = 90.0f;
        static constexpr float kUnitsPerMonth = 360.0f; ///< 4 weeks
        static constexpr float kUnitsPerYear = 2880.0f; ///< 8 months

        float time = 0.0f;
        float zero_time = 0.0f;
        /// Fraction of the day. A negative stored value is the "not set yet" sentinel and gives nullopt.
        std::optional<float> day_night_phase { };
        std::optional<std::int32_t> day_night_mode { };    ///< Newer generations only
        std::optional<float> day_night_rendered_phase { }; ///< Newest generation only, same sentinel rule

        /// Splits a time in units into calendar parts. Negative and non-finite input is treated as zero.
        [[nodiscard]] static CalendarDate split(float units) noexcept;
        [[nodiscard]] CalendarDate date() const noexcept
        {
            return split(time);
        }
    };

    // ---------------------------------------------------------------------------------------------------------
    // Awards
    // ---------------------------------------------------------------------------------------------------------

    struct Awards
    {
        std::uint32_t available_mask = 0; ///< Bit per award type
        std::int32_t months_before_start = 0;
        std::vector<std::vector<std::int32_t>> earned { }; ///< A list a month, of the last six: the kinds of award given that month
        std::vector<std::int32_t> prize_money { };         ///< Per award slot; negative for the penalty slots
        std::optional<std::int32_t> dangerous_coaster_events { };
        std::optional<std::int32_t> dangerous_pool_events { };
    };

    // ---------------------------------------------------------------------------------------------------------
    // Objectives
    // ---------------------------------------------------------------------------------------------------------

    struct Objective
    {
        std::int32_t difficulty = 0;   ///< 0, 1, 2 for the three levels
        std::int32_t objective_id = 0; ///< 0 is the main one; other values are kept raw
        std::int32_t prize_id = 0;     ///< Raw index into the game's prize list; 0..9 are valid, which kind is open
        bool success = false;

        [[nodiscard]] bool prize_in_range() const noexcept
        {
            return prize_id >= 0 && prize_id <= 9;
        }
    };

    /// Condition families the loader recognises by class. Everything else is Other and keeps its class name.
    enum class ConditionKind : std::uint8_t
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
    enum class ConditionFlag : std::uint32_t
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
        float value = 0; ///< The figure a ride must reach
        /// The low four bits pick the figure: excitement, intensity, nausea, length, most negative and most
        /// positive vertical G, most lateral G, top speed, height span, air time, inversions, highest drop.
        /// kAtMost asks for at most the value rather than at least; kEveryRide that every ride of the group
        /// reaches it; kPoolSlides counts the pools' slides rather than the coasters.
        std::uint32_t flags = 0;
        std::int32_t rides = 0; ///< How many rides must reach it

        static constexpr std::uint32_t kFigure = 0xf;
        static constexpr std::uint32_t kAtMost = 1u << 4;
        static constexpr std::uint32_t kEveryRide = 1u << 5;
        static constexpr std::uint32_t kPoolSlides = 1u << 6;
    };

    struct Condition
    {
        ConditionKind kind = ConditionKind::Other;
        std::string class_name { }; ///< The file's class, always set
        std::uint64_t id = 0;       ///< Object id
        std::int32_t difficulty = 0;
        std::int32_t objective_id = 0;
        std::int32_t slot = 0; ///< 0..7 within the objective
        bool completed = false;
        std::int32_t months_to_hold = 0; ///< How long the condition must stay true
        float start_time = 0.0f;         ///< Game time units
        float next_check_time = 0.0f;    ///< Game time units; the deadline when HasDeadline is set
        std::uint32_t consistency_flags = 0;
        std::optional<std::int64_t> value { };    ///< Threshold, for the families that have one
        std::optional<bool> greater_than { };     ///< Comparison direction, newer generations only
        std::optional<CoasterTarget> coaster { }; ///< Coaster conditions only

        [[nodiscard]] bool has(ConditionFlag f) const noexcept
        {
            return (consistency_flags & static_cast<std::uint32_t>(f)) != 0;
        }
    };

    /// A scripted side goal of the newer generations.
    struct Challenge
    {
        std::string name_key { }, description_key { }, acceptance_key { }, completion_key { }; ///< Text keys
        std::int32_t challenge_id = 0;
        std::int32_t state = 0; ///< Raw; labels unknown
        std::int32_t prize_cash = 0;
        std::int32_t prize_publicity = 0;
        std::int32_t prize_research_boost = 0;
        float time_limit = 0.0f;
        float time_remaining = 0.0f;
        std::int32_t times_postponed = 0;
    };

    struct ObjectiveSet
    {
        std::vector<Objective> objectives { };
        std::vector<Condition> conditions { };
        std::vector<Challenge> challenges { };

        /// Conditions that belong to the given objective, in slot order.
        [[nodiscard]] std::vector<const Condition*> conditions_for(std::int32_t difficulty, std::int32_t objective_id) const;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Research
    // ---------------------------------------------------------------------------------------------------------

    struct ResearchCategory
    {
        std::string name { };
        float budget = 0.0f;
        float current_level = 0.0f;
        float develop_cost = 0.0f;
        bool available = false;
        bool hidden = false;
        std::optional<bool> invented { }; ///< Missing in some older builds
    };

    struct ResearchState
    {
        std::vector<ResearchCategory> categories { };
        bool has_simple_research = false;
        std::optional<std::uint64_t> current_item { }; ///< Object id of the item being researched
        std::optional<std::uint64_t> previous_item { };
        std::int32_t money_spent_on_current = 0;
        std::int32_t monthly_budget = 0;
        std::vector<float> priorities { }; ///< Per category weight, order as in the file
        float time_since_last_spend = 0.0f;
    };

    // ---------------------------------------------------------------------------------------------------------
    // Messages
    // ---------------------------------------------------------------------------------------------------------

    struct Message
    {
        std::uint64_t id = 0;
        std::string class_name { };
        std::string text_key { };
        float time = 0.0f;                       ///< Game time units
        std::optional<std::uint64_t> object { }; ///< Subject object, when the entry kind has one
        std::optional<std::int32_t> panel { };
        std::string object_name { };           ///< The subject's name when the message was posted; may be empty
        std::optional<std::int32_t> award { }; ///< Award messages: which award
    };

    /// Indices 0..count-1 of a ring buffer visited oldest to newest, given the index the next write will use.
    /// A `next_write` outside the ring is treated as 0.
    [[nodiscard]] std::vector<std::size_t> ring_order(std::size_t count, std::int64_t next_write);

    struct MessageLog
    {
        std::size_t ring_size = 0;        ///< Slots in the ring as stored
        std::int32_t next_write = 0;      ///< As stored
        std::vector<Message> ordered { }; ///< Oldest first; empty and dangling slots are skipped
    };

    // ---------------------------------------------------------------------------------------------------------
    // Required content
    // ---------------------------------------------------------------------------------------------------------

    enum class AssetKind : std::uint8_t
    {
        Other,
        Scenery,       ///< Scenery pieces and track sections
        Attraction,    ///< Rides, stalls, facilities
        AnimalSpecies, ///< Zoo animals; these record only whether the park has unlocked them
    };

    /// One database entry of the save: an installed overlay and a symbol within it, plus the research flags.
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
        std::uint64_t id = 0;
    };

    struct RequiredContent
    {
        std::vector<AssetRequirement> entries { }; ///< Entries that name an overlay
        std::size_t entries_without_overlay = 0;   ///< Database entries that carry only a symbol or a type name
        [[nodiscard]] std::size_t invented_count() const noexcept;
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
        /// Entries of `dir` matching `name` ignoring ASCII case, the exact spelling first; the flag says exact.
        std::vector<std::pair<std::string, bool>> candidates(const std::filesystem::path& dir, std::string_view name);
        std::optional<OverlayFiles> descend(const std::filesystem::path& dir, std::span<const std::string_view> parts, bool exact);

        std::filesystem::path root_;
        std::unordered_map<std::string, Listing> cache_;
    };

    /// Convenience: one-shot resolution as a single path (the common half), without caching.
    [[nodiscard]] std::optional<std::filesystem::path> resolve_overlay(const std::filesystem::path& install_root, std::string_view stem);

    struct ContentReport
    {
        std::size_t entries = 0;
        std::size_t resolved = 0;                   ///< Common half found
        std::size_t exact_case = 0;                 ///< Of those, spelled exactly
        std::size_t with_unique = 0;                ///< Of those, unique half found too
        std::size_t missing = 0;                    ///< entries - resolved
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
        std::array<float, 8> weights { };
        float single_parent = 0; ///< Chance a family comes with one parent
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
        std::size_t particle_objects = 0;
    };

    /// Builds the model. Fails only when an object's framing cannot be decoded, which read_save already rules
    /// out for objects it returned. A save without a park or economy object loads with the has_* flags false.
    [[nodiscard]] std::expected<ParkModel, Error> load_park(const SaveFile& file, const ObjectIndex& index);

    /// Checks invariants of a loaded model and returns one line per violation (empty when sound): rating inside
    /// 0..1000, series no longer than their capacity, opening and closing inside a day, objective prizes in
    /// range, loan within the maximum, finite clock.
    [[nodiscard]] std::vector<std::string> validate(const ParkModel& model);

} // namespace rct3::save
