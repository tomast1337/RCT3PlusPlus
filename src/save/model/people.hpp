// SPDX-License-Identifier: GPL-3.0-or-later
//
// In-memory model of the people and animals in a park save: guests, staff, free-roaming animals, zoo
// animals, guest groups, carried and sold items and litter.
//
// The model is built from decoded objects (see save/value.hpp) by free functions. Rules shared by every
// builder:
//  - Attributes the file lacks (older or newer generations) take the defaults documented on the field.
//  - A reference that does not name an object of the save is "none" (id 0). Dangling ids are never kept.
//  - Slots holding the uninitialised filler pattern (all bytes 0xCD) count as unset: scalars with a sensible
//    default take it, optional ones become std::nullopt.
//  - A slot stored under the wrong declared type is reinterpreted by the meaning of the field (a float in an
//    integer slot and the reverse).
//  - Unknown enumeration values are kept as they are. Nothing fails because of a value.
// Floats that are meant to be levels are not clamped; use validate() to find out-of-range values.
#pragma once

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rct3::save
{

    using PeopleObjectId = std::uint64_t;

    /// Reference to another object of the same save. id 0 means none (also used for dangling references).
    struct PeopleRef
    {
        PeopleObjectId id = 0;
        [[nodiscard]] bool is_none() const noexcept
        {
            return id == 0;
        }
        [[nodiscard]] explicit operator bool() const noexcept
        {
            return id != 0;
        }
        friend bool operator==(const PeopleRef&, const PeopleRef&) = default;
    };

    /// The object a reference names, or nullptr for none.
    [[nodiscard]] const Object* resolve(const PeopleRef& ref, const ObjectIndex& index) noexcept;

    struct PeopleVec3
    {
        float x = 0;
        float y = 0;
        float z = 0;
        friend bool operator==(const PeopleVec3&, const PeopleVec3&) = default;
    };

    // ----- enumerations (explicit underlying types; values outside the list are representable) -----

    /// What a person is. Guests are 0..2, everything else is a staff role.
    enum class PersonType : std::uint8_t
    {
        Child = 0,
        Teen = 1,
        Adult = 2,
        Special = 3, ///< Park inspector and other non-guest, non-staff persons
        Handyman = 4,
        Mechanic = 5,
        SecurityGuard = 6,
        Entertainer = 7,
        Vendor = 8,
        AnimalKeeper = 9,
        LifeGuard = 12,
    };

    enum class Sex : std::uint8_t
    {
        Male = 0,
        Female = 1
    };

    /// States shared by all persons. Guests and staff reuse numbers from 12 upward for role-specific states
    /// (see GuestState); the number alone is not enough to name those.
    enum class PersonState : std::int32_t
    {
        Decision = 0,
        Dangling = 1,
        FallToGround = 2,
        Drowning = 3,
        Wandering = 4,
        SelectingPosition = 5,
        Queueing = 7,
        OnRide = 8,
        ExitingRide = 9,
        AtAttraction = 10,
        HeadingFor = 11,
        Lost = 256,
        Rescued = 257,
        Swimming = 259,
        HighDive = 260,
        Jacuzzi = 261,
        Fleeing = 262,
        Passport = 512,
    };

    /// Guest-only states, meaningful for a Guest when its state is in 12..20.
    enum class GuestState : std::int32_t
    {
        HeadingForBench = 12,
        WalkingToPark = 13,
        WalkingHome = 14,
        HeadingForBin = 15,
        FollowingGroup = 16,
        AdmiringView = 17,
        GettingAutograph = 18,
        WatchingFireworks = 19,
        WatchingEntertainer = 20,
    };

    /// Sub-states of PersonState::Queueing.
    enum class QueueingPhase : std::int32_t
    {
        Start = 0,
        PushingIn = 1,
        Queueing = 2,
        Waiting = 3,
        Entering = 4,
        WaitingToPay = 5,
        Paying = 6,
        Leaving = 7,
    };

    /// Sub-states of PersonState::Swimming.
    enum class SwimmingPhase : std::int32_t
    {
        Start = 0,
        Idle = 1,
        Travel = 2,
        Queue = 3,
        Shower = 4,
        Diving = 5,
        SunLounger = 6,
        Splashing = 7,
    };

    /// Sub-states of a guest in GuestState::FollowingGroup.
    enum class FollowPhase : std::int32_t
    {
        Wait = 0,
        FollowLeader = 1,
        Flock = 2,
        SetPieceStart = 3,
        SetPieceTravel = 4,
        SetPieceGather = 5,
        SetPiecePlay = 6,
    };

    enum class RideIntensity : std::uint8_t
    {
        Low = 0,
        Medium = 1,
        High = 2,
        Extreme = 3
    };
    enum class NauseaTolerance : std::uint8_t
    {
        Low = 0,
        Medium = 1,
        High = 2,
        VeryHigh = 3
    };

    enum class StaffKind : std::uint8_t
    {
        Handyman,
        Mechanic,
        SecurityGuard,
        Entertainer,
        Vendor,
        AnimalKeeper,
        LifeGuard,
        ParkInspector,
    };

    enum class AnimalKind : std::uint8_t
    {
        Duck,
        Seagull,
        Fish
    };

    enum class LitterState : std::uint8_t
    {
        OnGround = 0,
        Carried = 1
    };

    [[nodiscard]] const char* to_string(PersonType v) noexcept;
    [[nodiscard]] const char* to_string(PersonState v) noexcept; ///< "other" for values not listed
    [[nodiscard]] const char* to_string(StaffKind v) noexcept;
    [[nodiscard]] const char* to_string(AnimalKind v) noexcept;

    /// True for the guest person types.
    [[nodiscard]] constexpr bool is_guest_type(PersonType t) noexcept
    {
        return static_cast<std::uint8_t>(t) <= 2;
    }

    // ----- shared person data -----

    struct Thought
    {
        PeopleRef about;          ///< Object the thought is about
        PeopleRef other;          ///< A second person involved (a guest, for staff thoughts)
        std::int32_t id = 0;      ///< Thought identifier, 0 = empty slot. Label list not modelled.
        float time = -1;          ///< Game clock when it was had, -1 for an empty slot
        std::int32_t variant = 0; ///< Seed choosing a text variant
        [[nodiscard]] bool empty() const noexcept
        {
            return id == 0;
        }
    };

    inline constexpr std::size_t kThoughtSlots = 6;

    struct Appearance
    {
        PeopleRef head, body, legs, swim_body, swim_legs; ///< Part group database entries
        /// skin, upper clothes, lower clothes, upper swimsuit, lower swimsuit. 255 means "pick at random".
        std::array<std::uint8_t, 5> palettes { 255, 255, 255, 255, 255 };
        std::array<std::uint32_t, 3> optionals { }; ///< Optional mesh part masks: head, body, legs
        bool wearing_swimsuit = false;
        std::uint8_t level_of_detail = 0; ///< 0..3
        bool visible = true;
    };

    struct Animation
    {
        std::int32_t anim_type = -1; ///< Index into the person's animation set, -1 none
        std::int32_t walk = -1;
        std::int32_t idle = -1;
        std::int32_t alternate_walk = -1;
        float time = 0;       ///< Playback position
        float speed = 0;      ///< Playback rate
        float move_speed = 0; ///< Walking speed
    };

    struct LookAt
    {
        std::int32_t type = 0; ///< 0 none; 1, 2, 6 seen
        PeopleRef target;
        float horizontal = 0;  ///< radians
        float vertical = 0;    ///< radians
        float distance = 1.5f; ///< metres
        /// Union keyed by `type` (float bits or an id); kept as the stored bit pattern.
        std::uint32_t extra_bits = 0;
    };

    /// Everything every person has, whatever its role.
    struct Person
    {
        PeopleObjectId id = 0;
        PersonType type = PersonType::Adult;
        std::uint8_t sub_type = 0; ///< Sex for guests (see Sex), costume variant for entertainers
        std::string name;          ///< Guests: a real name. Staff: a placeholder to be localised.
        PeopleVec3 position;       ///< World position
        float heading = 0;         ///< Radians, 0..2pi
        PeopleVec3 lane_offset;    ///< Offset from the path centre line
        PersonState state = PersonState::Decision;
        std::int32_t sub_state = 0;
        PeopleRef state_target;
        float decide_time = 0; ///< Countdown to the next decision
        float start_time = 0;  ///< Game clock when the person entered the park
        Animation animation;
        Appearance appearance;
        LookAt look_at;
        std::array<Thought, kThoughtSlots> thoughts;
        std::vector<PeopleRef> attractions_visited;
        PeopleRef attraction_info; ///< Per-attraction session object
        PeopleRef route_controller;
        PeopleRef holder;             ///< The person's item holder object (MyHolders)
        float time_to_next_think = 0; ///< Seconds
        std::int32_t thinks_to_big_think = 0;
        [[nodiscard]] std::optional<GuestState> guest_state() const noexcept;
    };

    // ----- guests -----

    struct Cash
    {
        std::int32_t arrival = 0; ///< Cash brought into the park, minor units
        std::int32_t current = 0;
        std::int32_t withdrawn = 0;
        std::int32_t spent_drink = 0;
        std::int32_t spent_food = 0;
        std::int32_t spent_park_entry = 0;
        std::int32_t spent_rides = 0;
        std::int32_t spent_souvenirs = 0;
        std::int32_t total_spent = 0;
    };

    /// Needs, each 0..1. High food and drink mean satisfied, a high toilet value means urgent.
    struct Needs
    {
        float food = 1;
        float drink = 1;
        float toilet = 0;
    };

    struct Mood
    {
        float energy = 0.5f;
        float happiness = 0.5f;
        float nausea = 0;
        std::optional<float> sunburn; ///< Only newer saves store it
    };

    struct RidePreference
    {
        RideIntensity intensity = RideIntensity::Medium;
        float min = 0; ///< Bounds in intensity units
        float max = 0;
    };

    struct GuestCounters
    {
        std::int32_t rides = 0;
        std::int32_t drinks = 0;
        std::int32_t food_items = 0;
        std::int32_t souvenirs = 0;
        std::int32_t photos = 0;
        std::int32_t times_lost = 0;
        std::int32_t minutes_in_park = 0;
    };

    struct Guest
    {
        Person person;
        Cash cash;
        Needs needs;
        Mood mood_now;
        Mood mood_target; ///< The current mood eases towards this
        RidePreference ride_preference;
        NauseaTolerance nausea_tolerance = NauseaTolerance::Medium;
        PeopleRef favourite_ride;
        float favourite_ride_rating = 0; ///< Unit not confirmed
        PeopleRef group;                 ///< The group the guest belongs to
        GuestCounters counters;
        std::int32_t scenery_assess_timer = 0;
        float consuming_amount = 0; ///< 0..1 progress of the item being eaten or drunk
        bool in_park = true;
        bool is_avatar = false; ///< Player-named guest
        bool facing_photo = false;
        bool bad_entertainer = false;
        // Newer saves only; false or nullopt when absent.
        bool enjoyed_last_ride = false;
        bool last_ride_was_favourite = false;
        bool got_gift = false;
        bool needs_cash = false;
        bool wants_leave_park = false;
        bool leaving_due_to_closed_park = false;
        std::optional<float> pool_need;
        std::optional<float> pool_preference;
        std::optional<float> price_modifier; ///< Willingness-to-pay multiplier
    };

    // ----- staff -----

    struct PatrolArea
    {
        std::uint8_t start_col = 0;
        std::uint8_t start_row = 0;
        std::uint8_t size_x = 0;
        std::uint8_t size_y = 0;
        bool start_state = false;
    };

    struct HandymanWork
    {
        std::int32_t bins_emptied = 0;
        std::int32_t gardens_watered = 0;
        std::int32_t grass_mown = 0;
        std::int32_t paths_swept = 0;
        PeopleVec3 duty_waypoint;
    };

    struct MechanicWork
    {
        PeopleRef info; ///< Per-object mechanic bookkeeping
        std::int32_t objects_fixed = 0;
        std::int32_t objects_inspected = 0;
        float state_timer = 0;
    };

    struct EntertainerWork
    {
        std::int32_t anim_count = 0;
        bool bad = false;
        bool facing = false;
        bool guests_ok = false;
        std::int32_t show_piece_index = 0;
        float time_till_entertain = 0;
        float too_few_many_guests_time = 0;
    };

    struct VendorWork
    {
        PeopleRef attraction_instance; ///< Stall or ride instance, or none
        PeopleRef customer;
        PeopleRef scenery_item; ///< The stall prop
        PeopleRef shop_item;
    };

    struct KeeperWork
    {
        std::vector<PeopleRef> shelters_to_fill;
        std::int32_t enclosure_id = 0;
        std::int32_t dung_cleared = 0;
        std::int32_t troughs_filled = 0;
    };

    struct LifeGuardWork
    {
        PeopleRef chair;
        bool chair_been_set = false;
    };

    struct Staff
    {
        Person person;
        StaffKind kind = StaffKind::Handyman;
        std::int32_t type_id = 0; ///< Per-class variant; for vendors a per-park vendor id
        std::int32_t wage_level = 1;
        std::int32_t warnings = 0;
        std::int32_t training = 0; ///< 0 untrained .. 3 fully trained
        float laziness = 0;        ///< 0..1
        float happiness = 0.5f;    ///< 0..1
        float think_timer = 0;     ///< Seconds
        std::int32_t last_task_type = 0;
        // Counters of thinks since an event; -1 means never.
        std::int32_t thinks_since_last_task = -1;
        std::int32_t thinks_since_different_task = -1;
        std::int32_t thinks_since_no_task = -1;
        std::int32_t thinks_since_happy = -1;
        PatrolArea patrol;
        std::int32_t task_flags = 0;
        std::variant<std::monostate, HandymanWork, MechanicWork, EntertainerWork, VendorWork, KeeperWork, LifeGuardWork> work;
    };

    // ----- animals -----

    struct Animal
    {
        PeopleObjectId id = 0;
        AnimalKind kind = AnimalKind::Duck;
        PeopleVec3 position;
        std::int32_t anim_type = 0;
        float anim_time = 0;
        float anim_speed = 0;
        std::int32_t state = 0;
        std::int32_t sub_state = 0;
        float state_timer = 0;
        std::uint8_t palette = 0; ///< Colour variant, 0..3
        float time_in_park = 0;
        PeopleVec3 target;
        bool leader = false; ///< Flock or school leader (ducks, fish)
        PeopleRef left;      ///< Neighbour in the formation (ducks, fish)
        PeopleRef right;
        PeopleRef flight_route; ///< Ducks and gulls
        // Gulls
        PeopleRef victim;
        std::int32_t litter_pecked = 0;
        std::int32_t guests_attacked = 0;
        // Fish
        std::int32_t fish_type = 0;
        float swim_radius = 0;
        float swim_speed_per_step = 0;
        float swim_turn_speed_per_step = 0;
        float normal_swim_speed = 0;
    };

    /// A zoo animal. Its species is not stored as a field; it follows from the referenced entry and visual.
    struct ZooAnimal
    {
        PeopleObjectId id = 0;
        std::string name_template; ///< Name pattern with placeholders to localise
        std::string first_names;
        std::int32_t generation_suffix = 0;
        bool is_male = false;
        bool is_adult = false;
        float date_of_birth = 0;                                                    ///< Game clock, negative for born before the start
        float health = 0, food = 0, drink = 0, tiredness = 0, social = 0, dung = 0; ///< 0..1
        bool sleeping = false;
        bool tranquilised = false;
        bool can_be_picked_up = false;
        bool in_crate = false;
        bool been_paid_for = false;
        std::int32_t adoption_fee = 0;
        std::int32_t species_hunger = 0;
        std::int32_t species_life_expectancy = 0;
        std::int32_t species_peep_enjoyment = 0;
        std::int32_t species_social = 0;
        std::int32_t species_water_req = 0;
        std::int32_t type = 0;
        PeopleRef entry;
        PeopleRef family_membership;
        PeopleRef visual;
        float pregnancy_timer = -1; ///< -1 when not pregnant
        std::int32_t babies_in_womb = 0;
        float enclosure_health_score = 0;
        std::array<Thought, kThoughtSlots> thoughts;
    };

    // ----- groups, holders, items, litter -----

    inline constexpr std::size_t kMaxGroupMembers = 64; ///< Members beyond this are dropped

    struct GuestGroup
    {
        PeopleObjectId id = 0;
        std::int32_t group_type = 0;  ///< 0..10, names not modelled
        std::int32_t num_members = 0; ///< As stored; see live_members()
        PeopleRef leader;
        PeopleRef avatar;
        /// Member slots in file order; unused or dangling slots are none.
        std::vector<PeopleRef> members;
        std::int32_t flock_state = 0;
        std::int32_t flock_sub_state = 0;
        PeopleRef flock_focus;
        PeopleRef flock_target;
        std::int32_t flock_index = 0;
        float flock_progress = 0;
        float flock_decay = 0;
        Mood average_mood;
        float min_food = 0, min_drink = 0, max_toilet = 0;
        float min_intensity = 0, max_intensity = 0;
        std::int32_t set_piece_type = 0;
        std::int32_t set_piece_state = 0;
        PeopleVec3 set_piece_focus;
        PeopleRef set_piece_path;
        PeopleRef set_piece_actor0, set_piece_actor1;
        std::vector<PeopleVec3> set_piece_positions;
        bool set_piece_take_photo = false;
        PeopleRef hold_hands_route;
        [[nodiscard]] std::size_t live_members() const noexcept;
    };

    /// The items one person carries.
    struct ItemHolder
    {
        PeopleObjectId id = 0;
        PeopleRef owner;
        std::int32_t consuming_slot = -1; ///< -1 none
        PeopleRef head, left_hand, right_hand, left_pocket, right_pocket;
        PeopleRef umbrella, sunglasses, swimsuit;
        std::vector<PeopleRef> slotless;
        std::optional<std::uint32_t> hidden_parts; ///< Mask of hidden body parts, when stored
    };

    struct CarriedItem
    {
        PeopleObjectId id = 0;
        PeopleRef definition; ///< The item's database entry
        std::string name;
        float food = 0;
        float drink = 0; ///< May be negative
        float nausea = 0;
        bool bad = false;
        PeopleRef becomes; ///< Item this turns into when used up
        PeopleRef voucher_target;
        std::array<std::uint8_t, 3> colours { };
        std::int32_t inventory_choice = -1; ///< -1 none
    };

    struct ShopItem
    {
        PeopleObjectId id = 0;
        PeopleRef definition;
        std::string name;
        std::int32_t price = 0; ///< Minor units
        std::int32_t total_sold = 0;
        std::int32_t extras = 0;
        std::array<std::uint8_t, 3> colours { };
        PeopleRef sale_item;
        PeopleRef stall;
    };

    struct ShopExtra
    {
        PeopleObjectId id = 0;
        PeopleRef definition;
        std::int32_t setting = 0;
    };

    struct Litter
    {
        PeopleObjectId id = 0;
        PeopleVec3 position;
        PeopleVec3 orientation; ///< Euler angles in radians; y is the heading
        std::uint8_t type = 0;  ///< 0..5, names not modelled
        LitterState state = LitterState::OnGround;
        float create_time = 0;
        PeopleRef path_tile;
        /// Newer saves only. Bit 5 keeps the piece out of the park's rating; the other bits are not known.
        std::optional<std::uint32_t> flags;
    };

    struct PeopleModel
    {
        std::vector<Guest> guests;
        std::vector<Staff> staff;
        std::vector<Animal> animals;
        std::vector<ZooAnimal> zoo_animals;
        std::vector<GuestGroup> groups;
        std::vector<ItemHolder> holders;
        std::vector<CarriedItem> carried_items;
        std::vector<ShopItem> shop_items;
        std::vector<ShopExtra> shop_extras;
        std::vector<Litter> litter;
    };

    // ----- builders: nullopt only when the object is of another class -----

    [[nodiscard]] std::optional<Guest> make_guest(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<Staff> make_staff(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<Animal> make_animal(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<ZooAnimal> make_zoo_animal(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<GuestGroup> make_guest_group(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<ItemHolder> make_item_holder(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<CarriedItem> make_carried_item(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<ShopItem> make_shop_item(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<ShopExtra> make_shop_extra(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<Litter> make_litter(const DecodedObject& o, const ObjectIndex& index);

    /// Builds the model from every object of a people-related class. Fails only if an object cannot be decoded.
    [[nodiscard]] std::expected<PeopleModel, Error> load_people(const SaveFile& file, const ObjectIndex& index);

    // ----- validation -----

    enum class IssueKind : std::uint8_t
    {
        PersonWithoutHolder,      ///< No holder names the person as its owner
        HolderOwnerNotPerson,     ///< A holder's owner is none or not a guest or staff member
        GuestWithoutGroup,        ///< Group reference is none or not a group of the model
        GroupMemberNotGuest,      ///< A member slot names an object that is not a guest of the model
        GroupMemberCountMismatch, ///< Stored member count differs from the number of live slots
        StatOutOfRange,           ///< A 0..1 level lies outside 0..1 or is not finite
        SpendingBelowCategories,  ///< Total spent is smaller than the sum of the categories (it may legitimately be larger)
    };

    [[nodiscard]] const char* to_string(IssueKind k) noexcept;

    struct Issue
    {
        IssueKind kind;
        PeopleObjectId id; ///< Object the issue concerns
    };

    /// Checks the invariants that hold for every consistent save. The model is never modified.
    [[nodiscard]] std::vector<Issue> validate(const PeopleModel& model);

} // namespace rct3::save
