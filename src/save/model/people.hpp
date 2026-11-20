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

#include "core/types.hpp"
#include "save/save_file.hpp"
#include "save/value.hpp"
#include "units/units.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <optional>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace rct3::save::model
{

    using PeopleObjectId = u64;

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

    /// The object a reference names; none for the null reference.
    [[nodiscard]] std::optional<const Object&> resolve(const PeopleRef& ref, const ObjectIndex& index) noexcept;

    struct PeopleVec3
    {
        f32 x = 0;
        f32 y = 0;
        f32 z = 0;
        friend bool operator==(const PeopleVec3&, const PeopleVec3&) = default;
    };

    // ----- enumerations (explicit underlying types; values outside the list are representable) -----

    /// What a person is. Guests are 0..2, everything else is a staff role.
    enum class PersonType : u8
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

    enum class Sex : u8
    {
        Male = 0,
        Female = 1
    };

    /// States shared by all persons. Guests and staff reuse numbers from 12 upward for role-specific states
    /// (see GuestState); the number alone is not enough to name those. Names are the game's own text keys.
    /// Numbers are matched to states by what the person is doing in game and what their state target is.
    enum class PersonState : i32
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
    enum class GuestState : i32
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

    enum class RideIntensity : u8
    {
        Low = 0,
        Medium = 1,
        High = 2,
        Extreme = 3
    };
    enum class NauseaTolerance : u8
    {
        Low = 0,
        Medium = 1,
        High = 2,
        VeryHigh = 3
    };

    enum class StaffKind : u8
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

    enum class AnimalKind : u8
    {
        Duck,
        Seagull,
        Fish
    };

    enum class LitterState : u8
    {
        OnGround = 0,
        Carried = 1
    };

    [[nodiscard]] const char* to_string(PersonType v) noexcept;
    [[nodiscard]] const char* to_string(PersonState v) noexcept; ///< "other" for values not listed
    [[nodiscard]] const char* to_string(StaffKind v) noexcept;
    [[nodiscard]] const char* to_string(AnimalKind v) noexcept;

    [[nodiscard]] constexpr bool is_guest_type(PersonType t) noexcept
    {
        return static_cast<u8>(t) <= 2;
    }

    // ----- shared person data -----

    struct Thought
    {
        PeopleRef about { };
        PeopleRef other { };         ///< A second person involved (a guest, for staff thoughts)
        i32 id = 0;                  ///< Thought identifier, 0 = empty slot. Label list not modelled.
        std::optional<f32> time { }; ///< Game clock when it was had; none for an empty slot
        i32 variant = 0;             ///< Seed choosing a text variant
        [[nodiscard]] bool empty() const noexcept
        {
            return id == 0;
        }
    };

    inline constexpr usize kThoughtSlots = 6;

    struct Appearance
    {
        PeopleRef head { }, body { }, legs { }, swim_body { }, swim_legs { }; ///< Part group database entries
        /// skin, upper clothes, lower clothes, upper swimsuit, lower swimsuit. 255 means "pick at random".
        std::array<u8, 5> palettes { 255, 255, 255, 255, 255 };
        std::array<u32, 3> optionals { }; ///< Optional mesh part masks: head, body, legs
        bool wearing_swimsuit = false;
        u8 level_of_detail = 0; ///< 0..3
        bool visible = true;
    };

    struct Animation
    {
        std::optional<i32> anim_type { }; ///< Index into the person's animation set
        std::optional<i32> walk { };
        std::optional<i32> idle { };
        std::optional<i32> alternate_walk { };
        f32 playback_time = 0;
        f32 playback_rate = 0;
        f32 walk_speed = 0;
    };

    struct LookAt
    {
        i32 type = 0; ///< 0 none; 1, 2, 6 seen
        PeopleRef target { };
        units::Radians horizontal { };
        units::Radians vertical { };
        units::Distance distance { 1.5f };
        /// Union keyed by `type` (float bits or an id); kept as the stored bit pattern.
        u32 extra_bits = 0;
    };

    /// Everything every person has, whatever its role.
    struct Person
    {
        PeopleObjectId id = 0;
        PersonType type = PersonType::Adult;
        u8 sub_type = 0;         ///< Sex for guests (see Sex), costume variant for entertainers
        std::string name { };    ///< Guests: a real name. Staff: a placeholder to be localised.
        PeopleVec3 position { }; ///< World position
        /// Model to world as stored: four rows, the model's x, y and z axes then its position.
        std::array<f32, 16> world { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        units::Radians heading { }; ///< 0..2pi
        PeopleVec3 lane_offset { }; ///< Offset from the path centre line
        PersonState state = PersonState::Decision;
        i32 sub_state = 0;
        PeopleRef state_target { };
        f32 decide_time = 0; ///< Countdown to the next decision
        f32 start_time = 0;  ///< Game clock when the person entered the park
        Animation animation { };
        Appearance appearance { };
        LookAt look_at { };
        std::array<Thought, kThoughtSlots> thoughts { };
        std::vector<PeopleRef> attractions_visited { };
        PeopleRef attraction_info { }; ///< Per-attraction session object
        PeopleRef route_controller { };
        PeopleRef holder { }; ///< The person's item holder object (MyHolders)
        units::Seconds time_to_next_think { };
        i32 thinks_to_big_think = 0;
        // The player's switches on their window, kept in rct3++'s own saves.
        bool thoughts_to_console = false; ///< Their new thoughts are posted as messages
        bool highlight = false;           ///< Their name and what they are doing float above them, while the park shows highlights
        [[nodiscard]] std::optional<GuestState> guest_state() const noexcept;
    };

    // ----- guests -----

    struct Cash
    {
        i32 bank = 0; ///< What cash machines can still give them, minor units
        i32 current = 0;
        i32 withdrawn = 0;
        i32 spent_drink = 0;
        i32 spent_food = 0;
        i32 spent_park_entry = 0;
        i32 spent_rides = 0;
        i32 spent_souvenirs = 0;
        i32 total_spent = 0;
    };

    /// Needs, each 0..1. High food and drink mean satisfied, a high toilet value means urgent.
    struct Needs
    {
        f32 food = 1;
        f32 drink = 1;
        f32 toilet = 0;
    };

    struct Mood
    {
        f32 energy = 0.5f;
        f32 happiness = 0.5f;
        f32 nausea = 0;
        std::optional<f32> sunburn { }; ///< Only newer saves store it
    };

    struct RidePreference
    {
        RideIntensity intensity = RideIntensity::Medium;
        f32 min = 0; ///< Bounds in intensity units
        f32 max = 0;
    };

    struct GuestCounters
    {
        i32 rides = 0;
        i32 drinks = 0;
        i32 food_items = 0;
        i32 souvenirs = 0;
        i32 photos = 0;
        i32 times_lost = 0;
        i32 minutes_in_park = 0;
    };

    /// A stamp in a guest's passport, from a passport post.
    struct PassportStamp
    {
        i32 colour = 0;     ///< The post's stamp colour, as the post keeps it
        PeopleRef post { }; ///< The post, as a use point (WorldModel::use_points)
        f32 x = 0;          ///< Where on the page it sits, 0..1 across
        f32 y = 0;          ///< And 0..1 down
    };

    struct Guest
    {
        Person person { };
        Cash cash { };
        Needs needs { };
        Mood mood_now { };
        Mood mood_target { }; ///< The current mood eases towards this
        RidePreference ride_preference { };
        NauseaTolerance nausea_tolerance = NauseaTolerance::Medium;
        PeopleRef favourite_ride { };
        f32 favourite_ride_rating = 0; ///< Unit not confirmed
        PeopleRef group { };
        GuestCounters counters { };
        i32 scenery_assess_timer = 0;
        f32 consuming_amount = 0; ///< 0..1 progress of the item being eaten or drunk
        bool in_park = true;
        bool is_avatar = false; ///< Player-named guest
        bool facing_photo = false;
        bool bad_entertainer = false;
        // Newer saves only; false or nullopt when absent.
        bool enjoyed_last_ride = false;
        bool last_ride_was_favourite = false;
        bool got_gift = false; ///< Given the park's passport gift
        std::vector<PassportStamp> passport_stamps { };
        bool needs_cash = false;
        bool wants_leave_park = false;
        bool leaving_due_to_closed_park = false;
        std::optional<f32> pool_need { };
        std::optional<f32> pool_preference { };
        std::optional<units::Seconds> time_in_pool { }; ///< Still to swim, while in a pool
        std::optional<f32> price_modifier { };          ///< Willingness-to-pay multiplier
    };

    // ----- staff -----

    /// A staff member's patrol area, kept as a box of tiles and, inside it, which tiles belong: `runs` holds how
    /// many cells in a row are in or out along a walk over the box (see patrol_tiles), the first run being in
    /// when `start_state` is set, each next run the other way. No runs: no area, the whole park.
    struct PatrolArea
    {
        u8 start_col = 0; ///< The box's first column
        u8 start_row = 0; ///< ... and row
        u8 size_x = 0;    ///< Rows of the box
        u8 size_y = 0;    ///< Columns of the box
        bool start_state = false;
        std::vector<i32> runs { };
    };

    struct PatrolTile
    {
        i32 col = 0;
        i32 row = 0;
        bool operator==(const PatrolTile&) const = default;
    };

    enum class PatrolError
    {
        EmptyBox,    ///< Runs given for a box with no tiles
        BadRun,      ///< A run of no tiles or fewer
        RunsShort,   ///< The runs end before the walk does
        RunsTooLong, ///< The runs go on past the box's far corner
    };

    [[nodiscard]] const char* to_string(PatrolError e) noexcept;

    /// The tiles `area` takes in, in walk order; none when it has no runs. The walk goes over a grid size_x
    /// wide and size_y tall from one corner to the other, diagonal by diagonal, turning at the edges. Each
    /// cell's place in that grid, counted row by row, is a tile of the box counted row by row too, but from
    /// the box's last row back to its first.
    [[nodiscard]] std::expected<std::vector<PatrolTile>, PatrolError> patrol_tiles(const PatrolArea& area);

    struct HandymanWork
    {
        i32 bins_emptied = 0;
        i32 gardens_watered = 0;
        i32 grass_mown = 0;
        i32 paths_swept = 0;
        PeopleVec3 duty_waypoint { };
    };

    /// A ride, stall or changing room a mechanic has on their list.
    struct MechanicJob
    {
        PeopleRef attraction { };
        i32 tasks = 0; ///< What to do there, as the staff's task flags number it: inspect, fix
    };

    struct MechanicWork
    {
        PeopleRef info { }; ///< Per-object mechanic bookkeeping
        i32 objects_fixed = 0;
        i32 objects_inspected = 0;
        f32 state_timer = 0;
        std::vector<MechanicJob> jobs { }; ///< In turn: the first the one they are on, the rest waiting for them
    };

    struct EntertainerWork
    {
        i32 anim_count = 0;
        bool bad = false;
        bool facing = false;
        bool guests_ok = false;
        i32 show_piece_index = 0;
        f32 time_till_entertain = 0;
        f32 too_few_many_guests_time = 0;
    };

    struct VendorWork
    {
        PeopleRef attraction_instance { }; ///< Stall or ride instance, or none
        PeopleRef customer { };
        PeopleRef scenery_item { }; ///< The stall prop
        PeopleRef shop_item { };
    };

    struct KeeperWork
    {
        std::vector<PeopleRef> shelters_to_fill { };
        i32 enclosure_id = 0;
        i32 dung_cleared = 0;
        i32 troughs_filled = 0;
    };

    struct LifeGuardWork
    {
        PeopleRef chair { };
        bool chair_been_set = false;
    };

    struct Staff
    {
        Person person { };
        StaffKind kind = StaffKind::Handyman;
        i32 type_id = 0; ///< Per-class variant; for vendors a per-park vendor id
        i32 wage_level = 1;
        i32 warnings = 0;
        i32 training = 0;     ///< 0 untrained .. 3 fully trained
        f32 laziness = 0;     ///< 0..1
        f32 happiness = 0.5f; ///< 0..1
        units::Seconds think_timer { };
        i32 last_task_type = 0;
        // Counters of thinks since an event; none when it never happened.
        std::optional<i32> thinks_since_last_task { };
        std::optional<i32> thinks_since_different_task { };
        std::optional<i32> thinks_since_no_task { };
        std::optional<i32> thinks_since_happy { };
        PatrolArea patrol { };
        i32 task_flags = 0;
        std::optional<units::Seconds> time_in_pool { }; ///< Still to swim, while in a pool (an inspector looking one over)
        std::variant<std::monostate, HandymanWork, MechanicWork, EntertainerWork, VendorWork, KeeperWork, LifeGuardWork> work { };
    };

    // ----- animals -----

    struct Animal
    {
        PeopleObjectId id = 0;
        AnimalKind kind = AnimalKind::Duck;
        PeopleVec3 position { };
        i32 anim_type = 0;
        f32 anim_time = 0;
        f32 anim_speed = 0;
        i32 state = 0;
        i32 sub_state = 0;
        f32 state_timer = 0;
        u8 palette = 0; ///< Colour variant, 0..3
        f32 time_in_park = 0;
        PeopleVec3 target { };
        bool leader = false; ///< Flock or school leader (ducks, fish)
        PeopleRef left { };  ///< Neighbour in the formation (ducks, fish)
        PeopleRef right { };
        PeopleRef flight_route { }; ///< Ducks and gulls
        // Gulls
        PeopleRef victim { };
        i32 litter_pecked = 0;
        i32 guests_attacked = 0;
        // Fish
        i32 fish_type = 0;
        f32 swim_radius = 0;
        f32 swim_speed_per_step = 0;
        f32 swim_turn_speed_per_step = 0;
        f32 normal_swim_speed = 0;
    };

    /// A zoo animal. Its species is not stored as a field; it follows from the referenced entry and visual.
    struct ZooAnimal
    {
        PeopleObjectId id = 0;
        std::string name_template { }; ///< Name pattern with placeholders to localise
        std::string first_names { };
        i32 generation_suffix = 0;
        bool is_male = false;
        bool is_adult = false;
        f32 date_of_birth = 0;                                                    ///< Game clock, negative for born before the start
        f32 health = 0, food = 0, drink = 0, tiredness = 0, social = 0, dung = 0; ///< 0..1
        bool sleeping = false;
        bool tranquilised = false;
        bool can_be_picked_up = false;
        bool in_crate = false;
        bool been_paid_for = false;
        i32 adoption_fee = 0;
        i32 species_hunger = 0;
        i32 species_life_expectancy = 0;
        i32 species_peep_enjoyment = 0;
        i32 species_social = 0;
        i32 species_water_req = 0;
        i32 type = 0;
        PeopleRef entry { };
        PeopleRef family_membership { };
        PeopleRef visual { };
        std::string species { }; ///< Symbol of its species ("was" item), from the database entry `entry` names
        /// Model to world from the visual `visual` names, stored as Person::world is; identity without one.
        std::array<f32, 16> world { 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1, 0, 0, 0, 0, 1 };
        std::optional<f32> pregnancy_timer { }; ///< None when not pregnant
        i32 babies_in_womb = 0;
        f32 enclosure_health_score = 0;
        std::array<Thought, kThoughtSlots> thoughts { };
    };

    // ----- groups, holders, items, litter -----

    inline constexpr usize kMaxGroupMembers = 64; ///< Members beyond this are dropped

    struct GuestGroup
    {
        PeopleObjectId id = 0;
        i32 group_type = 0;  ///< 0..10, names not modelled
        i32 num_members = 0; ///< As stored; see live_members()
        PeopleRef leader { };
        PeopleRef avatar { };
        /// Member slots in file order; unused or dangling slots are none.
        std::vector<PeopleRef> members { };
        i32 flock_state = 0;
        i32 flock_sub_state = 0;
        PeopleRef flock_focus { };
        PeopleRef flock_target { };
        i32 flock_index = 0;
        f32 flock_progress = 0;
        f32 flock_decay = 0;
        Mood average_mood { };
        f32 min_food = 0, min_drink = 0, max_toilet = 0;
        f32 min_intensity = 0, max_intensity = 0;
        i32 set_piece_type = 0;
        i32 set_piece_state = 0;
        PeopleVec3 set_piece_focus { };
        PeopleRef set_piece_path { };
        PeopleRef set_piece_actor0 { }, set_piece_actor1 { };
        std::vector<PeopleVec3> set_piece_positions { };
        bool set_piece_take_photo = false;
        PeopleRef hold_hands_route { };
        [[nodiscard]] usize live_members() const noexcept;
    };

    /// The items one person carries.
    struct ItemHolder
    {
        PeopleObjectId id = 0;
        PeopleRef owner { };
        std::optional<i32> consuming_slot { };
        PeopleRef head { }, left_hand { }, right_hand { }, left_pocket { }, right_pocket { };
        PeopleRef umbrella { }, sunglasses { }, swimsuit { };
        std::vector<PeopleRef> slotless { };
        std::optional<u32> hidden_parts { }; ///< Mask of hidden body parts, when stored
    };

    struct CarriedItem
    {
        PeopleObjectId id = 0;
        PeopleRef definition { };    ///< The item's database entry
        std::string kind_symbol { }; ///< What kind of item it is, by the database entry's symbol, as `Burger:cid`
        std::string name { };
        f32 food = 0;
        f32 drink = 0; ///< May be negative
        f32 nausea = 0;
        bool bad = false;
        PeopleRef becomes { }; ///< Item this turns into when used up
        PeopleRef voucher_target { };
        std::array<u8, 3> colours { };
        std::optional<i32> inventory_choice { };
    };

    struct ShopItem
    {
        PeopleObjectId id = 0;
        PeopleRef definition { };
        std::string name { };
        i32 price = 0; ///< Minor units
        i32 total_sold = 0;
        i32 extras = 0;
        std::array<u8, 3> colours { };
        PeopleRef sale_item { };
        PeopleRef stall { };
    };

    struct ShopExtra
    {
        PeopleObjectId id = 0;
        PeopleRef definition { };
        i32 setting = 0;
    };

    /// The first Litter::type that is dung an animal left, lying off the paths in its enclosure.
    inline constexpr u8 kFirstDungType = 3;

    struct Litter
    {
        PeopleObjectId id = 0;
        PeopleVec3 position { };
        PeopleVec3 orientation { }; ///< Euler angles in radians; y is the heading
        u8 type = 0;                ///< 0..5: litter and sick below kFirstDungType, animal dung from it up, smallest first
        LitterState state = LitterState::OnGround;
        f32 create_time = 0;
        PeopleRef path_tile { };
        /// Newer saves only. Bit 5 keeps the piece out of the park's rating; the other bits are not known.
        std::optional<u32> flags { };
    };

    /// One of the sets of meshes people's heads, bodies and legs are picked from, named by the symbol the
    /// game's character files give it (without its tag).
    struct PartGroup
    {
        PeopleObjectId id = 0;
        std::string symbol { };
        u8 section = 0; ///< 0 head, 1 body, 2 legs
        u8 sex = 0;
        u8 type = 0; ///< Same numbers as PersonType
    };

    /// What the park gives a guest whose passport has enough stamps.
    struct PassportRules
    {
        PeopleRef gift { }; ///< The carried item given, among PeopleModel::carried_items
        i32 stamps_for_gift = 0;
    };

    /// An animal house's care, kept by an original park beside the house it stands for.
    struct AnimalHouseCare
    {
        PeopleRef house { }; ///< The house as placed, among the park's placed objects
        u8 feeding = 1;      ///< 0 infrequent, 1 normal, 2 often
    };

    struct PeopleModel
    {
        std::vector<PartGroup> part_groups { };
        std::vector<Guest> guests { };
        std::vector<Staff> staff { };
        std::vector<Animal> animals { };
        std::vector<ZooAnimal> zoo_animals { };
        std::vector<GuestGroup> groups { };
        std::vector<ItemHolder> holders { };
        std::vector<CarriedItem> carried_items { };
        std::vector<ShopItem> shop_items { };
        std::vector<ShopExtra> shop_extras { };
        std::vector<Litter> litter { };
        std::vector<AnimalHouseCare> animal_houses { };
        std::optional<PassportRules> passport { }; ///< None when the file keeps none
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
    [[nodiscard]] std::optional<AnimalHouseCare> make_animal_house_care(const DecodedObject& o, const ObjectIndex& index);
    [[nodiscard]] std::optional<PartGroup> make_part_group(const DecodedObject& o);

    /// Builds the model from every object of a people-related class. Fails only if an object cannot be decoded.
    [[nodiscard]] std::expected<PeopleModel, Error> load_people(const SaveFile& file, const ObjectIndex& index);

    // ----- validation -----

    enum class IssueKind : u8
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
        IssueKind kind { };
        PeopleObjectId id { }; ///< Object the issue concerns
    };

    /// Checks the invariants that hold for every consistent save. The model is never modified.
    [[nodiscard]] std::vector<Issue> validate(const PeopleModel& model);

} // namespace rct3::save::model
