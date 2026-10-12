// SPDX-License-Identifier: GPL-3.0-or-later
#include "save/model/people.hpp"

#include <algorithm>
#include <bit>
#include <cmath>
#include <unordered_set>

namespace rct3::save
{

    namespace
    {

        // ---- Format identifiers the loader keys on. Class names first, then attribute names. ----
        namespace cls
        {
            constexpr std::string_view guest = "Guest";
            constexpr std::string_view handyman = "Handyman";
            constexpr std::string_view mechanic = "Mechanic";
            constexpr std::string_view security_guard = "SecurityGuard";
            constexpr std::string_view entertainer = "Entertainer";
            constexpr std::string_view vendor = "Vendor";
            constexpr std::string_view animal_keeper = "AnimalKeeper";
            constexpr std::string_view life_guard = "LifeGuard";
            constexpr std::string_view park_inspector = "ParkInspector";
            constexpr std::string_view duck = "Duck";
            constexpr std::string_view seagull = "Seagull";
            constexpr std::string_view fish = "Fish";
            constexpr std::string_view wild_animal = "WildAnimal";
            constexpr std::string_view group = "GuestGroup";
            constexpr std::string_view holder = "GuestHolders";
            constexpr std::string_view carried = "CarriedItemInstance";
            constexpr std::string_view shop_item = "ShopItemInstance";
            constexpr std::string_view shop_extra = "ShopExtraInstance";
            constexpr std::string_view litter = "Litter";
        } // namespace cls

        namespace at
        {
            // person
            constexpr std::string_view world_matrix = "WorldMatrix";
            constexpr std::string_view path_offset = "PathOffset";
            constexpr std::string_view facing_angle = "FacingAngle";
            constexpr std::string_view anim_type = "AnimType";
            constexpr std::string_view current_walk_anim = "CurrentWalkAnim";
            constexpr std::string_view idle_anim = "IdleAnim";
            constexpr std::string_view alternate_walk_anim = "AlternateWalkingAnim";
            constexpr std::string_view anim_time = "AnimTime";
            constexpr std::string_view anim_speed = "AnimSpeed";
            constexpr std::string_view move_speed = "MoveSpeed";
            constexpr std::string_view state = "State";
            constexpr std::string_view sub_state = "SubState";
            constexpr std::string_view state_target = "StateTarget";
            constexpr std::string_view decide_time = "DecideTime";
            constexpr std::string_view route_controller = "RouteController";
            constexpr std::string_view name = "NameS";
            constexpr std::string_view start_date = "StartDate";
            constexpr std::string_view head_group = "HeadPartGroup";
            constexpr std::string_view body_group = "BodyPartGroup";
            constexpr std::string_view legs_group = "LegsPartGroup";
            constexpr std::string_view swim_body_group = "SwimSuitBodyPartGroup";
            constexpr std::string_view swim_legs_group = "SwimSuitLegsPartGroup";
            constexpr std::string_view skin_palette = "SkinPalette";
            constexpr std::string_view upper_palette = "UpperClothingPalette";
            constexpr std::string_view lower_palette = "LowerClothingPalette";
            constexpr std::string_view upper_swim_palette = "UpperSwimSuitPalette";
            constexpr std::string_view lower_swim_palette = "LowerSwimSuitPalette";
            constexpr std::string_view head_optionals = "HeadOptionals";
            constexpr std::string_view body_optionals = "BodyOptionals";
            constexpr std::string_view legs_optionals = "LegsOptionals";
            constexpr std::string_view person_type = "PersonType";
            constexpr std::string_view sub_type = "SubType";
            constexpr std::string_view level_of_detail = "LevelOfDetail";
            constexpr std::string_view use_swim_suit = "UseSwimSuit";
            constexpr std::string_view recent_thoughts = "RecentThoughts";
            constexpr std::string_view thought_about = "About";
            constexpr std::string_view thought_person = "GuestReference";
            constexpr std::string_view thought_id = "Thought";
            constexpr std::string_view thought_time = "Time";
            constexpr std::string_view thought_variant = "Variant";
            constexpr std::string_view attractions_visited = "AttractionsVisited";
            constexpr std::string_view attraction_info = "AttractionPersonInfo";
            constexpr std::string_view look_type = "LookAtType";
            constexpr std::string_view look_target = "LookAtTarget";
            constexpr std::string_view look_extra = "LookAtExtra";
            constexpr std::string_view look_h = "LookAtH";
            constexpr std::string_view look_v = "LookAtV";
            constexpr std::string_view look_dist = "LookAtDist";
            constexpr std::string_view holders = "MyHolders";
            constexpr std::string_view rendering = "Rendering";
            constexpr std::string_view time_to_next_think = "TimeToNextThink";
            constexpr std::string_view thinks_to_big = "ThinksToNextBigThink";
            // guest
            constexpr std::string_view cash = "CashInfo";
            constexpr std::string_view atm_cash = "ATMCash";
            constexpr std::string_view current_cash = "CurrentCash";
            constexpr std::string_view spent_drink = "SpentOnDrink";
            constexpr std::string_view spent_food = "SpentOnFood";
            constexpr std::string_view spent_entry = "SpentOnParkEntry";
            constexpr std::string_view spent_rides = "SpentOnRides";
            constexpr std::string_view spent_souvenirs = "SpentOnSouvenirs";
            constexpr std::string_view total_spent = "TotalSpent";
            constexpr std::string_view withdrawn = "WithdrawnCash";
            constexpr std::string_view consumption = "Consumption";
            constexpr std::string_view food_f = "FoodF";
            constexpr std::string_view drink_f = "DrinkF";
            constexpr std::string_view toilet_f = "ToiletF";
            constexpr std::string_view current_health = "CurrentHealth";
            constexpr std::string_view target_health = "TargetHealth";
            constexpr std::string_view energy_f = "EnergyF";
            constexpr std::string_view happiness_f = "HappinessF";
            constexpr std::string_view nausea_f = "NauseaF";
            constexpr std::string_view sunburn_f = "SunBurnF";
            constexpr std::string_view favourite_ride = "FavouriteRide";
            constexpr std::string_view favourite_rating = "FavouriteRideRating";
            constexpr std::string_view intensity = "PreferedRideIntensity";
            constexpr std::string_view intensity_min = "PreferedIntensityMin";
            constexpr std::string_view intensity_max = "PreferedIntensityMax";
            constexpr std::string_view nausea_tolerance = "NauseaTolerance";
            constexpr std::string_view rides_done = "NumRidesDone";
            constexpr std::string_view drinks_bought = "NumDrinksBought";
            constexpr std::string_view food_bought = "NumFoodItemsBought";
            constexpr std::string_view souvenirs_bought = "NumSouvenirsBought";
            constexpr std::string_view photos_taken = "PhotosTaken";
            constexpr std::string_view time_in_park_int = "TimeInPark";
            constexpr std::string_view man_lost = "ManLost";
            constexpr std::string_view is_in_park = "IsInPark";
            constexpr std::string_view is_avatar = "IsAvatar";
            constexpr std::string_view is_facing_photo = "IsFacingPhoto";
            constexpr std::string_view guest_group = "GuestGroup";
            constexpr std::string_view assess_scenery = "AssessSceneryTimer";
            constexpr std::string_view bad_entertainer = "BadEntertainer";
            constexpr std::string_view consuming_amount = "ConsumingAmount";
            constexpr std::string_view enjoyed_last_ride = "EnjoyedLastRide";
            constexpr std::string_view last_ride_favourite = "LastRideWasFavourite";
            constexpr std::string_view got_gift = "GotGift";
            constexpr std::string_view needs_cash = "NeedsCash";
            constexpr std::string_view want_leave = "WantLeavePark";
            constexpr std::string_view leaving_closed = "LeavingDueToClosedPark";
            constexpr std::string_view pool_need = "PoolNeed";
            constexpr std::string_view pool_preference = "PoolPreference";
            constexpr std::string_view price_modifier = "PriceModifier";
            // staff
            constexpr std::string_view staff_type_id = "StaffTypeId";
            constexpr std::string_view wage_level = "WageLevel";
            constexpr std::string_view warnings = "Warnings";
            constexpr std::string_view training = "Training";
            constexpr std::string_view think_timer = "ThinkTimer";
            constexpr std::string_view last_task_type = "LastTaskType";
            constexpr std::string_view thinks_last_task = "ThinksSinceLastTask";
            constexpr std::string_view thinks_different_task = "ThinksSinceDifferentTask";
            constexpr std::string_view thinks_no_task = "ThinksSinceNoTask";
            constexpr std::string_view thinks_happy = "ThinksSinceHappy";
            constexpr std::string_view laziness = "Laziness";
            constexpr std::string_view happiness = "Happiness";
            constexpr std::string_view grid_x = "GridSizeX";
            constexpr std::string_view grid_y = "GridSizeY";
            constexpr std::string_view start_col = "StartCol";
            constexpr std::string_view start_row = "StartRow";
            constexpr std::string_view start_state = "StartState";
            constexpr std::string_view task_flags = "TaskFlags";
            constexpr std::string_view bins_emptied = "BinsEmptied";
            constexpr std::string_view gardens_watered = "GardensWatered";
            constexpr std::string_view grass_mown = "GrassMown";
            constexpr std::string_view paths_swept = "PathsSwept";
            constexpr std::string_view duty_waypoint = "DutyWayPoint";
            constexpr std::string_view mechanic_info = "ObjectMechanicInfo";
            constexpr std::string_view objects_fixed = "ObjectsFixed";
            constexpr std::string_view objects_inspected = "ObjectsInspected";
            constexpr std::string_view state_timer = "StateTimer";
            constexpr std::string_view anim_count = "AnimCount";
            constexpr std::string_view bad = "Bad";
            constexpr std::string_view facing = "Facing";
            constexpr std::string_view guests_ok = "NumGuestsOk";
            constexpr std::string_view show_piece = "ShowPieceIndex";
            constexpr std::string_view time_till_entertain = "TimeTillEntertain";
            constexpr std::string_view too_few_many = "TooFewManyGuestsTime";
            constexpr std::string_view attraction_instance = "AttractionInstance";
            constexpr std::string_view customer = "Customer";
            constexpr std::string_view scenery_item = "SceneryItem";
            constexpr std::string_view shop_item = "ShopItemInstance";
            constexpr std::string_view shelters_to_fill = "SheltersToFill";
            constexpr std::string_view enclosure_id = "EnclosureID";
            constexpr std::string_view dung_cleared = "DungCleared";
            constexpr std::string_view troughs_filled = "TroughsFilled";
            constexpr std::string_view life_guard_chair = "LifeGuardChair";
            constexpr std::string_view chair_been_set = "ChairBeenSet";
            // animals
            constexpr std::string_view palette = "Palette";
            constexpr std::string_view state_timer_a = "StateTimer";
            constexpr std::string_view time_in_park_f = "TimeInPark";
            constexpr std::string_view target_pos = "TargetPos";
            constexpr std::string_view leader = "Leader";
            constexpr std::string_view left_duck = "LeftDuck";
            constexpr std::string_view right_duck = "RightDuck";
            constexpr std::string_view left_fish = "LeftFish";
            constexpr std::string_view right_fish = "RightFish";
            constexpr std::string_view flight_route = "FlightRoute";
            constexpr std::string_view victim = "Victim";
            constexpr std::string_view litter_pecked = "LitterPecked";
            constexpr std::string_view guests_attacked = "GuestsAttacked";
            constexpr std::string_view fish_type = "FishType";
            constexpr std::string_view swim_radius = "SwimRadius";
            constexpr std::string_view swim_speed_step = "SwimSpeedPerStep";
            constexpr std::string_view swim_turn_step = "SwimTurnSpeedPerStep";
            constexpr std::string_view normal_swim_speed = "NormalSwimSpeed";
            // zoo animal
            constexpr std::string_view wa_name = "Name";
            constexpr std::string_view wa_first_names = "FirstAndMiddleName";
            constexpr std::string_view wa_suffix = "GenerationSuffix";
            constexpr std::string_view wa_is_male = "IsMale";
            constexpr std::string_view wa_is_adult = "IsAdult";
            constexpr std::string_view wa_birth = "DateOfBirth";
            constexpr std::string_view wa_health = "Health";
            constexpr std::string_view wa_food = "Food";
            constexpr std::string_view wa_drink = "Drink";
            constexpr std::string_view wa_tiredness = "Tiredness";
            constexpr std::string_view wa_social = "Social";
            constexpr std::string_view wa_dung = "Dung";
            constexpr std::string_view wa_sleeping = "IsSleeping";
            constexpr std::string_view wa_tranquilised = "IsTranquilised";
            constexpr std::string_view wa_pickup = "CanBePickedUp";
            constexpr std::string_view wa_crate = "InCrate";
            constexpr std::string_view wa_paid = "BeenPaidFor";
            constexpr std::string_view wa_fee = "AdoptionFee";
            constexpr std::string_view wa_hunger = "SSHunger";
            constexpr std::string_view wa_life = "SSLifeExpectancy";
            constexpr std::string_view wa_enjoyment = "SSPeepEnjoyment";
            constexpr std::string_view wa_species_social = "SSSocial";
            constexpr std::string_view wa_water = "SSWaterReq";
            constexpr std::string_view wa_type = "Type";
            constexpr std::string_view wa_entry = "Entry";
            constexpr std::string_view wa_family = "FamilyMembership";
            constexpr std::string_view wa_visual = "Visual";
            constexpr std::string_view wa_pregnancy = "PregnancyTimer";
            constexpr std::string_view wa_babies = "NumBabiesInWomb";
            constexpr std::string_view wa_enclosure_score = "EnclosureHealthScore";
            constexpr std::string_view wa_thoughts = "ThoughtsAbout";
            // groups
            constexpr std::string_view g_type = "GuestGroupType";
            constexpr std::string_view g_num_members = "NumMembers";
            constexpr std::string_view g_leader = "Leader";
            constexpr std::string_view g_members = "Members";
            constexpr std::string_view g_avatar = "Avatar";
            constexpr std::string_view g_flock_state = "FlockState";
            constexpr std::string_view g_flock_sub = "FlockSubState";
            constexpr std::string_view g_flock_focus = "FlockFocus";
            constexpr std::string_view g_flock_target = "FlockTarget";
            constexpr std::string_view g_flock_index = "FlockIndex";
            constexpr std::string_view g_flock_progress = "FlockProgress";
            constexpr std::string_view g_flock_decay = "FlockDecay";
            constexpr std::string_view g_avg_energy = "AvgEnergy";
            constexpr std::string_view g_avg_happiness = "AvgHappiness";
            constexpr std::string_view g_avg_nausea = "AvgNausea";
            constexpr std::string_view g_avg_sunburn = "AvgSunBurn";
            constexpr std::string_view g_min_food = "MinFood";
            constexpr std::string_view g_min_drink = "MinDrink";
            constexpr std::string_view g_max_toilet = "MaxToilet";
            constexpr std::string_view g_min_intensity = "MinIntensity";
            constexpr std::string_view g_max_intensity = "MaxIntensity";
            constexpr std::string_view g_sp_type = "SetPieceType";
            constexpr std::string_view g_sp_state = "SetPieceState";
            constexpr std::string_view g_sp_focus = "SetPieceFocus";
            constexpr std::string_view g_sp_path = "SetPiecePath";
            constexpr std::string_view g_sp_actor0 = "SetPieceActor0";
            constexpr std::string_view g_sp_actor1 = "SetPieceActor1";
            constexpr std::string_view g_sp_pos = "SetPiecePos";
            constexpr std::string_view g_sp_photo = "SetPieceTakePhoto";
            constexpr std::string_view g_hold_hands = "HoldHandsRouteController";
            // holders
            constexpr std::string_view h_owner = "Person";
            constexpr std::string_view h_consuming = "ConsumingItem";
            constexpr std::string_view h_head = "HeadHolder";
            constexpr std::string_view h_lhand = "LHandHolder";
            constexpr std::string_view h_rhand = "RHandHolder";
            constexpr std::string_view h_lpocket = "LPocketHolder";
            constexpr std::string_view h_rpocket = "RPocketHolder";
            constexpr std::string_view h_umbrella = "Umbrella";
            constexpr std::string_view h_sunglasses = "Sunglasses";
            constexpr std::string_view h_swimsuit = "Swimsuit";
            constexpr std::string_view h_slotless = "SlotlessHolders";
            constexpr std::string_view h_hidden = "HiddenBits";
            // items
            constexpr std::string_view i_definition = "CIDEntry";
            constexpr std::string_view i_name = "Name";
            constexpr std::string_view i_food = "Food";
            constexpr std::string_view i_drink = "Drink";
            constexpr std::string_view i_nausea = "Nausea";
            constexpr std::string_view i_bad = "Bad";
            constexpr std::string_view i_becomes = "Becomes";
            constexpr std::string_view i_voucher = "VoucherTarget";
            constexpr std::string_view i_colour0 = "FlexiColour0";
            constexpr std::string_view i_colour1 = "FlexiColour1";
            constexpr std::string_view i_colour2 = "FlexiColour2";
            constexpr std::string_view i_inventory = "InventoryChoice";
            constexpr std::string_view i_price = "Price";
            constexpr std::string_view i_total_sold = "TotalSold";
            constexpr std::string_view i_extras = "Extras";
            constexpr std::string_view i_sale_item = "SaleItem";
            constexpr std::string_view i_stall = "Stall";
            constexpr std::string_view i_setting = "Setting";
            // litter
            constexpr std::string_view l_position = "Position";
            constexpr std::string_view l_orientation = "Orientation";
            constexpr std::string_view l_type = "LitterType";
            constexpr std::string_view l_state = "LitterState";
            constexpr std::string_view l_create = "CreateTime";
            constexpr std::string_view l_tile = "PathTile";
        } // namespace at

        // ---- tolerant scalar readers over possibly-null attribute values ----

        constexpr std::uint32_t kFillBits = 0xCDCDCDCDu;
        constexpr std::size_t kMaxVisited = 4096; // upper bound on list lengths kept in the model

        const Value* sub(const Value* v, std::string_view n) noexcept
        {
            return v ? v->field(n) : nullptr;
        }

        /// The 32 bits of a numeric slot, whichever way it was declared.
        std::optional<std::uint32_t> bits32(const Value* v) noexcept
        {
            if (!v)
                return std::nullopt;
            switch (v->kind())
            {
                case ValueKind::Float:
                    return std::bit_cast<std::uint32_t>(v->as_float());
                case ValueKind::Integer:
                    return static_cast<std::uint32_t>(v->as_integer());
                case ValueKind::Bool:
                    return v->as_bool() ? 1u : 0u;
                default:
                    return std::nullopt;
            }
        }

        std::optional<float> opt_f32(const Value* v) noexcept
        {
            const auto b = bits32(v);
            if (!b || *b == kFillBits)
                return std::nullopt;
            const auto f = std::bit_cast<float>(*b);
            if (!std::isfinite(f))
                return std::nullopt;
            return f;
        }
        float f32(const Value* v, float def = 0) noexcept
        {
            return opt_f32(v).value_or(def);
        }

        std::optional<std::int32_t> opt_i32(const Value* v) noexcept
        {
            const auto b = bits32(v);
            if (!b || *b == kFillBits)
                return std::nullopt;
            return static_cast<std::int32_t>(*b);
        }
        std::int32_t i32(const Value* v, std::int32_t def = 0) noexcept
        {
            return opt_i32(v).value_or(def);
        }

        std::uint8_t u8(const Value* v, std::uint8_t def = 0) noexcept
        {
            const auto b = bits32(v);
            if (!b || *b == kFillBits)
                return def;
            return static_cast<std::uint8_t>(*b);
        }

        bool flag(const Value* v, bool def = false) noexcept
        {
            const auto b = bits32(v);
            if (!b || *b == kFillBits)
                return def;
            return *b != 0;
        }

        std::string str(const Value* v)
        {
            if (v && v->kind() == ValueKind::String)
                return std::string(v->text());
            return { };
        }

        PeopleRef ref(const Value* v, const ObjectIndex& index) noexcept
        {
            if (!v)
                return { };
            std::uint64_t id = 0;
            if (v->kind() == ValueKind::Reference)
                id = v->as_reference();
            else if (v->kind() == ValueKind::Integer && v->as_integer() > 0)
                id = static_cast<std::uint64_t>(v->as_integer());
            if (id == 0 || index.find(id) == nullptr)
                return { };
            return { id };
        }

        float vec_component(float f) noexcept
        {
            if (!std::isfinite(f) || std::bit_cast<std::uint32_t>(f) == kFillBits)
                return 0;
            return f;
        }

        PeopleVec3 vec3(const Value* v) noexcept
        {
            if (!v)
                return { };
            const auto f = v->floats();
            if (f.size() < 3)
                return { };
            return { vec_component(f[0]), vec_component(f[1]), vec_component(f[2]) };
        }

        /// Translation row of a world matrix.
        PeopleVec3 matrix_position(const Value* v) noexcept
        {
            if (!v)
                return { };
            const auto f = v->floats();
            if (f.size() != 16)
                return { };
            return { vec_component(f[12]), vec_component(f[13]), vec_component(f[14]) };
        }

        std::vector<PeopleRef> ref_list(const Value* v, const ObjectIndex& index, std::size_t cap)
        {
            std::vector<PeopleRef> out;
            if (!v || v->kind() != ValueKind::List)
                return out;
            const auto items = v->items();
            out.reserve(std::min(items.size(), cap));
            for (std::size_t i = 0; i < items.size() && i < cap; ++i)
                out.push_back(ref(&items[i], index));
            return out;
        }

        std::array<Thought, kThoughtSlots> thoughts(const Value* v, const ObjectIndex& index)
        {
            std::array<Thought, kThoughtSlots> out;
            if (!v || v->kind() != ValueKind::List)
                return out;
            const auto items = v->items();
            for (std::size_t i = 0; i < items.size() && i < kThoughtSlots; ++i)
            {
                const Value* t = &items[i];
                out[i].about = ref(sub(t, at::thought_about), index);
                out[i].other = ref(sub(t, at::thought_person), index);
                out[i].id = i32(sub(t, at::thought_id));
                out[i].time = f32(sub(t, at::thought_time), -1);
                out[i].variant = i32(sub(t, at::thought_variant));
            }
            return out;
        }

        Mood mood(const Value* v) noexcept
        {
            Mood m;
            m.energy = f32(sub(v, at::energy_f), 0.5f);
            m.happiness = f32(sub(v, at::happiness_f), 0.5f);
            m.nausea = f32(sub(v, at::nausea_f), 0);
            m.sunburn = opt_f32(sub(v, at::sunburn_f));
            return m;
        }

        Person build_person(const DecodedObject& o, const ObjectIndex& index, PersonType default_type)
        {
            const auto get = [&](std::string_view n) { return o.find(n); };
            Person p;
            p.id = o.id;
            p.type = static_cast<PersonType>(u8(get(at::person_type), static_cast<std::uint8_t>(default_type)));
            p.sub_type = u8(get(at::sub_type));
            p.name = str(get(at::name));
            p.position = matrix_position(get(at::world_matrix));
            p.heading = f32(get(at::facing_angle));
            p.lane_offset = vec3(get(at::path_offset));
            p.state = static_cast<PersonState>(i32(get(at::state)));
            p.sub_state = i32(get(at::sub_state));
            p.state_target = ref(get(at::state_target), index);
            p.decide_time = f32(get(at::decide_time));
            p.start_time = f32(get(at::start_date));
            p.animation.anim_type = i32(get(at::anim_type), -1);
            p.animation.walk = i32(get(at::current_walk_anim), -1);
            p.animation.idle = i32(get(at::idle_anim), -1);
            p.animation.alternate_walk = i32(get(at::alternate_walk_anim), -1);
            p.animation.time = f32(get(at::anim_time));
            p.animation.speed = f32(get(at::anim_speed));
            p.animation.move_speed = f32(get(at::move_speed));
            auto& a = p.appearance;
            a.head = ref(get(at::head_group), index);
            a.body = ref(get(at::body_group), index);
            a.legs = ref(get(at::legs_group), index);
            a.swim_body = ref(get(at::swim_body_group), index);
            a.swim_legs = ref(get(at::swim_legs_group), index);
            a.palettes = { u8(get(at::skin_palette), 255), u8(get(at::upper_palette), 255), u8(get(at::lower_palette), 255), u8(get(at::upper_swim_palette), 255), u8(get(at::lower_swim_palette), 255) };
            const auto mask = [&](std::string_view n) { return bits32(get(n)).value_or(0); };
            a.optionals = { mask(at::head_optionals), mask(at::body_optionals), mask(at::legs_optionals) };
            a.wearing_swimsuit = flag(get(at::use_swim_suit));
            a.level_of_detail = u8(get(at::level_of_detail));
            a.visible = flag(get(at::rendering), true);
            p.look_at.type = i32(get(at::look_type));
            p.look_at.target = ref(get(at::look_target), index);
            p.look_at.horizontal = f32(get(at::look_h));
            p.look_at.vertical = f32(get(at::look_v));
            p.look_at.distance = f32(get(at::look_dist), 1.5f);
            p.look_at.extra_bits = bits32(get(at::look_extra)).value_or(0);
            p.thoughts = thoughts(get(at::recent_thoughts), index);
            p.attractions_visited = ref_list(get(at::attractions_visited), index, kMaxVisited);
            p.attraction_info = ref(get(at::attraction_info), index);
            p.route_controller = ref(get(at::route_controller), index);
            p.holder = ref(get(at::holders), index);
            p.time_to_next_think = f32(get(at::time_to_next_think));
            p.thinks_to_big_think = i32(get(at::thinks_to_big));
            return p;
        }

        struct StaffClass
        {
            std::string_view name;
            StaffKind kind;
            PersonType type;
        };
        constexpr StaffClass kStaffClasses[] = {
            { cls::handyman, StaffKind::Handyman, PersonType::Handyman },     { cls::mechanic, StaffKind::Mechanic, PersonType::Mechanic },           { cls::security_guard, StaffKind::SecurityGuard, PersonType::SecurityGuard }, { cls::entertainer, StaffKind::Entertainer, PersonType::Entertainer }, { cls::vendor, StaffKind::Vendor, PersonType::Vendor }, { cls::animal_keeper, StaffKind::AnimalKeeper, PersonType::AnimalKeeper },
            { cls::life_guard, StaffKind::LifeGuard, PersonType::LifeGuard }, { cls::park_inspector, StaffKind::ParkInspector, PersonType::Special },
        };

        const StaffClass* staff_class(std::string_view name) noexcept
        {
            for (const auto& c : kStaffClasses)
                if (c.name == name)
                    return &c;
            return nullptr;
        }

        constexpr std::pair<std::string_view, AnimalKind> kAnimalClasses[] = { { cls::duck, AnimalKind::Duck }, { cls::seagull, AnimalKind::Seagull }, { cls::fish, AnimalKind::Fish } };

    } // namespace

    // ---- public small helpers ----

    const Object* resolve(const PeopleRef& r, const ObjectIndex& index) noexcept
    {
        return r.id == 0 ? nullptr : index.find(r.id);
    }

    const char* to_string(PersonType v) noexcept
    {
        switch (v)
        {
            case PersonType::Child:
                return "child";
            case PersonType::Teen:
                return "teen";
            case PersonType::Adult:
                return "adult";
            case PersonType::Special:
                return "special";
            case PersonType::Handyman:
                return "handyman";
            case PersonType::Mechanic:
                return "mechanic";
            case PersonType::SecurityGuard:
                return "security-guard";
            case PersonType::Entertainer:
                return "entertainer";
            case PersonType::Vendor:
                return "vendor";
            case PersonType::AnimalKeeper:
                return "animal-keeper";
            case PersonType::LifeGuard:
                return "life-guard";
        }
        return "other";
    }

    const char* to_string(PersonState v) noexcept
    {
        switch (v)
        {
            case PersonState::Decision:
                return "decision";
            case PersonState::Dangling:
                return "dangling";
            case PersonState::FallToGround:
                return "fall-to-ground";
            case PersonState::Drowning:
                return "drowning";
            case PersonState::Wandering:
                return "wandering";
            case PersonState::SelectingPosition:
                return "selecting-position";
            case PersonState::Queueing:
                return "queueing";
            case PersonState::OnRide:
                return "on-ride";
            case PersonState::ExitingRide:
                return "exiting-ride";
            case PersonState::AtAttraction:
                return "at-attraction";
            case PersonState::HeadingFor:
                return "heading-for";
            case PersonState::Lost:
                return "lost";
            case PersonState::Rescued:
                return "rescued";
            case PersonState::Swimming:
                return "swimming";
            case PersonState::HighDive:
                return "high-dive";
            case PersonState::Jacuzzi:
                return "jacuzzi";
            case PersonState::Fleeing:
                return "fleeing";
            case PersonState::Passport:
                return "passport";
        }
        return "other";
    }

    const char* to_string(StaffKind v) noexcept
    {
        switch (v)
        {
            case StaffKind::Handyman:
                return "handyman";
            case StaffKind::Mechanic:
                return "mechanic";
            case StaffKind::SecurityGuard:
                return "security-guard";
            case StaffKind::Entertainer:
                return "entertainer";
            case StaffKind::Vendor:
                return "vendor";
            case StaffKind::AnimalKeeper:
                return "animal-keeper";
            case StaffKind::LifeGuard:
                return "life-guard";
            case StaffKind::ParkInspector:
                return "park-inspector";
        }
        return "other";
    }

    const char* to_string(AnimalKind v) noexcept
    {
        switch (v)
        {
            case AnimalKind::Duck:
                return "duck";
            case AnimalKind::Seagull:
                return "seagull";
            case AnimalKind::Fish:
                return "fish";
        }
        return "other";
    }

    const char* to_string(IssueKind k) noexcept
    {
        switch (k)
        {
            case IssueKind::PersonWithoutHolder:
                return "person-without-holder";
            case IssueKind::HolderOwnerNotPerson:
                return "holder-owner-not-person";
            case IssueKind::GuestWithoutGroup:
                return "guest-without-group";
            case IssueKind::GroupMemberNotGuest:
                return "group-member-not-guest";
            case IssueKind::GroupMemberCountMismatch:
                return "group-member-count-mismatch";
            case IssueKind::StatOutOfRange:
                return "stat-out-of-range";
            case IssueKind::SpendingBelowCategories:
                return "spending-below-categories";
        }
        return "unknown";
    }

    std::optional<GuestState> Person::guest_state() const noexcept
    {
        const auto v = static_cast<std::int32_t>(state);
        if (v < 12 || v > 20)
            return std::nullopt;
        return static_cast<GuestState>(v);
    }

    std::size_t GuestGroup::live_members() const noexcept
    {
        return static_cast<std::size_t>(std::count_if(members.begin(), members.end(), [](const PeopleRef& r) { return !r.is_none(); }));
    }

    // ---- builders ----

    std::optional<Guest> make_guest(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::guest)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        Guest g;
        g.person = build_person(o, index, PersonType::Adult);
        const auto* cash = get(at::cash);
        g.cash.arrival = i32(sub(cash, at::atm_cash));
        g.cash.current = i32(sub(cash, at::current_cash));
        g.cash.withdrawn = i32(sub(cash, at::withdrawn));
        g.cash.spent_drink = i32(sub(cash, at::spent_drink));
        g.cash.spent_food = i32(sub(cash, at::spent_food));
        g.cash.spent_park_entry = i32(sub(cash, at::spent_entry));
        g.cash.spent_rides = i32(sub(cash, at::spent_rides));
        g.cash.spent_souvenirs = i32(sub(cash, at::spent_souvenirs));
        g.cash.total_spent = i32(sub(cash, at::total_spent));
        const auto* need = get(at::consumption);
        g.needs.food = f32(sub(need, at::food_f), 1);
        g.needs.drink = f32(sub(need, at::drink_f), 1);
        g.needs.toilet = f32(sub(need, at::toilet_f), 0);
        g.mood_now = mood(get(at::current_health));
        g.mood_target = get(at::target_health) ? mood(get(at::target_health)) : g.mood_now;
        g.ride_preference.intensity = static_cast<RideIntensity>(u8(get(at::intensity), 1));
        g.ride_preference.min = f32(get(at::intensity_min));
        g.ride_preference.max = f32(get(at::intensity_max));
        g.nausea_tolerance = static_cast<NauseaTolerance>(u8(get(at::nausea_tolerance), 1));
        g.favourite_ride = ref(get(at::favourite_ride), index);
        g.favourite_ride_rating = f32(get(at::favourite_rating));
        g.group = ref(get(at::guest_group), index);
        g.counters.rides = i32(get(at::rides_done));
        g.counters.drinks = i32(get(at::drinks_bought));
        g.counters.food_items = i32(get(at::food_bought));
        g.counters.souvenirs = i32(get(at::souvenirs_bought));
        g.counters.photos = i32(get(at::photos_taken));
        g.counters.times_lost = i32(get(at::man_lost));
        g.counters.minutes_in_park = i32(get(at::time_in_park_int));
        g.scenery_assess_timer = i32(get(at::assess_scenery));
        g.consuming_amount = f32(get(at::consuming_amount));
        g.in_park = flag(get(at::is_in_park), true);
        g.is_avatar = flag(get(at::is_avatar));
        g.facing_photo = flag(get(at::is_facing_photo));
        g.bad_entertainer = flag(get(at::bad_entertainer));
        g.enjoyed_last_ride = flag(get(at::enjoyed_last_ride));
        g.last_ride_was_favourite = flag(get(at::last_ride_favourite));
        g.got_gift = flag(get(at::got_gift));
        g.needs_cash = flag(get(at::needs_cash));
        g.wants_leave_park = flag(get(at::want_leave));
        g.leaving_due_to_closed_park = flag(get(at::leaving_closed));
        g.pool_need = opt_f32(get(at::pool_need));
        g.pool_preference = opt_f32(get(at::pool_preference));
        g.price_modifier = opt_f32(get(at::price_modifier));
        return g;
    }

    std::optional<Staff> make_staff(const DecodedObject& o, const ObjectIndex& index)
    {
        const auto* c = staff_class(o.class_name);
        if (!c)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        Staff s;
        s.kind = c->kind;
        s.person = build_person(o, index, c->type);
        s.type_id = i32(get(at::staff_type_id));
        s.wage_level = i32(get(at::wage_level), 1);
        s.warnings = i32(get(at::warnings));
        s.training = i32(get(at::training));
        s.laziness = f32(get(at::laziness));
        s.happiness = f32(get(at::happiness), 0.5f);
        s.think_timer = f32(get(at::think_timer));
        s.last_task_type = i32(get(at::last_task_type));
        s.thinks_since_last_task = i32(get(at::thinks_last_task), -1);
        s.thinks_since_different_task = i32(get(at::thinks_different_task), -1);
        s.thinks_since_no_task = i32(get(at::thinks_no_task), -1);
        s.thinks_since_happy = i32(get(at::thinks_happy), -1);
        s.patrol = { u8(get(at::start_col)), u8(get(at::start_row)), u8(get(at::grid_x)), u8(get(at::grid_y)), flag(get(at::start_state)) };
        s.task_flags = i32(get(at::task_flags));
        switch (c->kind)
        {
            case StaffKind::Handyman:
            {
                HandymanWork w;
                w.bins_emptied = i32(get(at::bins_emptied));
                w.gardens_watered = i32(get(at::gardens_watered));
                w.grass_mown = i32(get(at::grass_mown));
                w.paths_swept = i32(get(at::paths_swept));
                w.duty_waypoint = vec3(get(at::duty_waypoint));
                s.work = w;
                break;
            }
            case StaffKind::Mechanic:
            {
                MechanicWork w;
                w.info = ref(get(at::mechanic_info), index);
                w.objects_fixed = i32(get(at::objects_fixed));
                w.objects_inspected = i32(get(at::objects_inspected));
                w.state_timer = f32(get(at::state_timer));
                s.work = w;
                break;
            }
            case StaffKind::Entertainer:
            {
                EntertainerWork w;
                w.anim_count = i32(get(at::anim_count));
                w.bad = flag(get(at::bad));
                w.facing = flag(get(at::facing));
                w.guests_ok = flag(get(at::guests_ok));
                w.show_piece_index = i32(get(at::show_piece));
                w.time_till_entertain = f32(get(at::time_till_entertain));
                w.too_few_many_guests_time = f32(get(at::too_few_many));
                s.work = w;
                break;
            }
            case StaffKind::Vendor:
            {
                VendorWork w;
                w.attraction_instance = ref(get(at::attraction_instance), index);
                w.customer = ref(get(at::customer), index);
                w.scenery_item = ref(get(at::scenery_item), index);
                w.shop_item = ref(get(at::shop_item), index);
                s.work = w;
                break;
            }
            case StaffKind::AnimalKeeper:
            {
                KeeperWork w;
                w.shelters_to_fill = ref_list(get(at::shelters_to_fill), index, kMaxVisited);
                w.enclosure_id = i32(get(at::enclosure_id));
                w.dung_cleared = i32(get(at::dung_cleared));
                w.troughs_filled = i32(get(at::troughs_filled));
                s.work = std::move(w);
                break;
            }
            case StaffKind::LifeGuard:
            {
                LifeGuardWork w;
                w.chair = ref(get(at::life_guard_chair), index);
                w.chair_been_set = flag(get(at::chair_been_set));
                s.work = w;
                break;
            }
            case StaffKind::SecurityGuard:
            case StaffKind::ParkInspector:
                break;
        }
        return s;
    }

    std::optional<Animal> make_animal(const DecodedObject& o, const ObjectIndex& index)
    {
        std::optional<AnimalKind> kind;
        for (const auto& [n, k] : kAnimalClasses)
            if (n == o.class_name)
                kind = k;
        if (!kind)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        Animal a;
        a.id = o.id;
        a.kind = *kind;
        a.position = matrix_position(get(at::world_matrix));
        a.anim_type = i32(get(at::anim_type));
        a.anim_time = f32(get(at::anim_time));
        a.anim_speed = f32(get(at::anim_speed));
        a.state = i32(get(at::state));
        a.sub_state = i32(get(at::sub_state));
        a.state_timer = f32(get(at::state_timer_a));
        a.palette = u8(get(at::palette));
        a.time_in_park = f32(get(at::time_in_park_f));
        a.target = vec3(get(at::target_pos));
        a.leader = flag(get(at::leader));
        if (*kind == AnimalKind::Fish)
        {
            a.left = ref(get(at::left_fish), index);
            a.right = ref(get(at::right_fish), index);
            a.fish_type = i32(get(at::fish_type));
            a.swim_radius = f32(get(at::swim_radius));
            a.swim_speed_per_step = f32(get(at::swim_speed_step));
            a.swim_turn_speed_per_step = f32(get(at::swim_turn_step));
            a.normal_swim_speed = f32(get(at::normal_swim_speed));
        }
        else
        {
            a.left = ref(get(at::left_duck), index);
            a.right = ref(get(at::right_duck), index);
            a.flight_route = ref(get(at::flight_route), index);
        }
        if (*kind == AnimalKind::Seagull)
        {
            a.victim = ref(get(at::victim), index);
            a.litter_pecked = i32(get(at::litter_pecked));
            a.guests_attacked = i32(get(at::guests_attacked));
        }
        return a;
    }

    std::optional<ZooAnimal> make_zoo_animal(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::wild_animal)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        ZooAnimal z;
        z.id = o.id;
        z.name_template = str(get(at::wa_name));
        z.first_names = str(get(at::wa_first_names));
        z.generation_suffix = i32(get(at::wa_suffix));
        z.is_male = flag(get(at::wa_is_male));
        z.is_adult = flag(get(at::wa_is_adult));
        z.date_of_birth = f32(get(at::wa_birth));
        z.health = f32(get(at::wa_health));
        z.food = f32(get(at::wa_food));
        z.drink = f32(get(at::wa_drink));
        z.tiredness = f32(get(at::wa_tiredness));
        z.social = f32(get(at::wa_social));
        z.dung = f32(get(at::wa_dung));
        z.sleeping = flag(get(at::wa_sleeping));
        z.tranquilised = flag(get(at::wa_tranquilised));
        z.can_be_picked_up = flag(get(at::wa_pickup));
        z.in_crate = flag(get(at::wa_crate));
        z.been_paid_for = flag(get(at::wa_paid));
        z.adoption_fee = i32(get(at::wa_fee));
        z.species_hunger = i32(get(at::wa_hunger));
        z.species_life_expectancy = i32(get(at::wa_life));
        z.species_peep_enjoyment = i32(get(at::wa_enjoyment));
        z.species_social = i32(get(at::wa_species_social));
        z.species_water_req = i32(get(at::wa_water));
        z.type = i32(get(at::wa_type));
        z.entry = ref(get(at::wa_entry), index);
        z.family_membership = ref(get(at::wa_family), index);
        z.visual = ref(get(at::wa_visual), index);
        z.pregnancy_timer = f32(get(at::wa_pregnancy), -1);
        z.babies_in_womb = i32(get(at::wa_babies));
        z.enclosure_health_score = f32(get(at::wa_enclosure_score));
        z.thoughts = thoughts(get(at::wa_thoughts), index);
        return z;
    }

    std::optional<GuestGroup> make_guest_group(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::group)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        GuestGroup g;
        g.id = o.id;
        g.group_type = i32(get(at::g_type));
        g.num_members = i32(get(at::g_num_members));
        g.leader = ref(get(at::g_leader), index);
        g.avatar = ref(get(at::g_avatar), index);
        g.members = ref_list(get(at::g_members), index, kMaxGroupMembers);
        g.flock_state = i32(get(at::g_flock_state));
        g.flock_sub_state = i32(get(at::g_flock_sub));
        g.flock_focus = ref(get(at::g_flock_focus), index);
        g.flock_target = ref(get(at::g_flock_target), index);
        g.flock_index = i32(get(at::g_flock_index));
        g.flock_progress = f32(get(at::g_flock_progress));
        g.flock_decay = f32(get(at::g_flock_decay));
        g.average_mood.energy = f32(get(at::g_avg_energy));
        g.average_mood.happiness = f32(get(at::g_avg_happiness));
        g.average_mood.nausea = f32(get(at::g_avg_nausea));
        g.average_mood.sunburn = opt_f32(get(at::g_avg_sunburn));
        g.min_food = f32(get(at::g_min_food));
        g.min_drink = f32(get(at::g_min_drink));
        g.max_toilet = f32(get(at::g_max_toilet));
        g.min_intensity = f32(get(at::g_min_intensity));
        g.max_intensity = f32(get(at::g_max_intensity));
        g.set_piece_type = i32(get(at::g_sp_type));
        g.set_piece_state = i32(get(at::g_sp_state));
        g.set_piece_focus = vec3(get(at::g_sp_focus));
        g.set_piece_path = ref(get(at::g_sp_path), index);
        g.set_piece_actor0 = ref(get(at::g_sp_actor0), index);
        g.set_piece_actor1 = ref(get(at::g_sp_actor1), index);
        if (const auto* pos = get(at::g_sp_pos); pos && pos->kind() == ValueKind::List)
        {
            const auto items = pos->items();
            for (std::size_t i = 0; i < items.size() && i < kMaxGroupMembers; ++i)
                g.set_piece_positions.push_back(vec3(&items[i]));
        }
        g.set_piece_take_photo = flag(get(at::g_sp_photo));
        g.hold_hands_route = ref(get(at::g_hold_hands), index);
        return g;
    }

    std::optional<ItemHolder> make_item_holder(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::holder)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        ItemHolder h;
        h.id = o.id;
        h.owner = ref(get(at::h_owner), index);
        h.consuming_slot = i32(get(at::h_consuming), -1);
        h.head = ref(get(at::h_head), index);
        h.left_hand = ref(get(at::h_lhand), index);
        h.right_hand = ref(get(at::h_rhand), index);
        h.left_pocket = ref(get(at::h_lpocket), index);
        h.right_pocket = ref(get(at::h_rpocket), index);
        h.umbrella = ref(get(at::h_umbrella), index);
        h.sunglasses = ref(get(at::h_sunglasses), index);
        h.swimsuit = ref(get(at::h_swimsuit), index);
        h.slotless = ref_list(get(at::h_slotless), index, kMaxVisited);
        if (const auto b = bits32(get(at::h_hidden)); b && *b != kFillBits)
            h.hidden_parts = *b;
        return h;
    }

    std::optional<CarriedItem> make_carried_item(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::carried)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        CarriedItem c;
        c.id = o.id;
        c.definition = ref(get(at::i_definition), index);
        c.name = str(get(at::i_name));
        c.food = f32(get(at::i_food));
        c.drink = f32(get(at::i_drink));
        c.nausea = f32(get(at::i_nausea));
        c.bad = flag(get(at::i_bad));
        c.becomes = ref(get(at::i_becomes), index);
        c.voucher_target = ref(get(at::i_voucher), index);
        c.colours = { u8(get(at::i_colour0)), u8(get(at::i_colour1)), u8(get(at::i_colour2)) };
        c.inventory_choice = i32(get(at::i_inventory), -1);
        return c;
    }

    std::optional<ShopItem> make_shop_item(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::shop_item)
            return std::nullopt;
        const auto get = [&](std::string_view n) { return o.find(n); };
        ShopItem s;
        s.id = o.id;
        s.definition = ref(get(at::i_definition), index);
        s.name = str(get(at::i_name));
        s.price = i32(get(at::i_price));
        s.total_sold = i32(get(at::i_total_sold));
        s.extras = i32(get(at::i_extras));
        s.colours = { u8(get(at::i_colour0)), u8(get(at::i_colour1)), u8(get(at::i_colour2)) };
        s.sale_item = ref(get(at::i_sale_item), index);
        s.stall = ref(get(at::i_stall), index);
        return s;
    }

    std::optional<ShopExtra> make_shop_extra(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::shop_extra)
            return std::nullopt;
        ShopExtra s;
        s.id = o.id;
        s.definition = ref(o.find(at::i_definition), index);
        s.setting = i32(o.find(at::i_setting));
        return s;
    }

    std::optional<Litter> make_litter(const DecodedObject& o, const ObjectIndex& index)
    {
        if (o.class_name != cls::litter)
            return std::nullopt;
        Litter l;
        l.id = o.id;
        l.position = vec3(o.find(at::l_position));
        l.orientation = vec3(o.find(at::l_orientation));
        l.type = u8(o.find(at::l_type));
        l.state = static_cast<LitterState>(u8(o.find(at::l_state)));
        l.create_time = f32(o.find(at::l_create));
        l.path_tile = ref(o.find(at::l_tile), index);
        return l;
    }

    // ---- loading ----

    std::expected<PeopleModel, Error> load_people(const SaveFile& file, const ObjectIndex& index)
    {
        PeopleModel m;
        // Decide once per class which builder applies, so unrelated objects are never decoded.
        enum class Slot : std::uint8_t
        {
            None,
            Guest,
            Staff,
            Animal,
            Zoo,
            Group,
            Holder,
            Carried,
            ShopItem,
            ShopExtra,
            Litter
        };
        std::vector<Slot> slot(file.classes.size(), Slot::None);
        for (std::size_t i = 0; i < file.classes.size(); ++i)
        {
            const auto n = file.classes[i].name;
            if (n == cls::guest)
                slot[i] = Slot::Guest;
            else if (staff_class(n))
                slot[i] = Slot::Staff;
            else if (n == cls::duck || n == cls::seagull || n == cls::fish)
                slot[i] = Slot::Animal;
            else if (n == cls::wild_animal)
                slot[i] = Slot::Zoo;
            else if (n == cls::group)
                slot[i] = Slot::Group;
            else if (n == cls::holder)
                slot[i] = Slot::Holder;
            else if (n == cls::carried)
                slot[i] = Slot::Carried;
            else if (n == cls::shop_item)
                slot[i] = Slot::ShopItem;
            else if (n == cls::shop_extra)
                slot[i] = Slot::ShopExtra;
            else if (n == cls::litter)
                slot[i] = Slot::Litter;
        }
        for (const auto& obj : file.objects)
        {
            if (obj.class_index >= slot.size() || slot[obj.class_index] == Slot::None)
                continue;
            const auto decoded = decode_object(file, obj);
            if (!decoded)
                return std::unexpected(decoded.error());
            const auto& d = *decoded;
            switch (slot[obj.class_index])
            {
                case Slot::Guest:
                    if (auto g = make_guest(d, index))
                        m.guests.push_back(std::move(*g));
                    break;
                case Slot::Staff:
                    if (auto s = make_staff(d, index))
                        m.staff.push_back(std::move(*s));
                    break;
                case Slot::Animal:
                    if (auto a = make_animal(d, index))
                        m.animals.push_back(*a);
                    break;
                case Slot::Zoo:
                    if (auto z = make_zoo_animal(d, index))
                        m.zoo_animals.push_back(std::move(*z));
                    break;
                case Slot::Group:
                    if (auto g = make_guest_group(d, index))
                        m.groups.push_back(std::move(*g));
                    break;
                case Slot::Holder:
                    if (auto h = make_item_holder(d, index))
                        m.holders.push_back(std::move(*h));
                    break;
                case Slot::Carried:
                    if (auto c = make_carried_item(d, index))
                        m.carried_items.push_back(std::move(*c));
                    break;
                case Slot::ShopItem:
                    if (auto s = make_shop_item(d, index))
                        m.shop_items.push_back(std::move(*s));
                    break;
                case Slot::ShopExtra:
                    if (auto s = make_shop_extra(d, index))
                        m.shop_extras.push_back(*s);
                    break;
                case Slot::Litter:
                    if (auto l = make_litter(d, index))
                        m.litter.push_back(*l);
                    break;
                case Slot::None:
                    break;
            }
        }
        return m;
    }

    // ---- validation ----

    std::vector<Issue> validate(const PeopleModel& m)
    {
        std::vector<Issue> out;
        std::unordered_set<PeopleObjectId> guest_ids, person_ids, group_ids, owners;
        for (const auto& g : m.guests)
        {
            guest_ids.insert(g.person.id);
            person_ids.insert(g.person.id);
        }
        for (const auto& s : m.staff)
            person_ids.insert(s.person.id);
        for (const auto& g : m.groups)
            group_ids.insert(g.id);
        for (const auto& h : m.holders)
        {
            if (h.owner.is_none() || !person_ids.contains(h.owner.id))
                out.push_back({ IssueKind::HolderOwnerNotPerson, h.id });
            else
                owners.insert(h.owner.id);
        }
        const auto level = [&](float v, PeopleObjectId id) {
            if (!std::isfinite(v) || v < -0.001f || v > 1.001f)
                out.push_back({ IssueKind::StatOutOfRange, id });
        };
        for (const auto& g : m.guests)
        {
            const auto id = g.person.id;
            if (!owners.contains(id))
                out.push_back({ IssueKind::PersonWithoutHolder, id });
            if (g.group.is_none() || !group_ids.contains(g.group.id))
                out.push_back({ IssueKind::GuestWithoutGroup, id });
            level(g.needs.food, id);
            level(g.needs.drink, id);
            level(g.needs.toilet, id);
            for (const Mood* mood : { &g.mood_now, &g.mood_target })
            {
                level(mood->energy, id);
                level(mood->happiness, id);
                level(mood->nausea, id);
                if (mood->sunburn)
                    level(*mood->sunburn, id);
            }
            level(g.consuming_amount, id);
            const auto& c = g.cash;
            const std::int64_t sum = std::int64_t { c.spent_drink } + c.spent_food + c.spent_park_entry + c.spent_rides + c.spent_souvenirs;
            if (c.total_spent < sum)
                out.push_back({ IssueKind::SpendingBelowCategories, id });
        }
        for (const auto& s : m.staff)
        {
            if (!owners.contains(s.person.id))
                out.push_back({ IssueKind::PersonWithoutHolder, s.person.id });
            level(s.happiness, s.person.id);
            level(s.laziness, s.person.id);
        }
        for (const auto& g : m.groups)
        {
            for (const auto& member : g.members)
                if (!member.is_none() && !guest_ids.contains(member.id))
                    out.push_back({ IssueKind::GroupMemberNotGuest, g.id });
            if (g.num_members < 0 || static_cast<std::size_t>(g.num_members) != g.live_members())
                out.push_back({ IssueKind::GroupMemberCountMismatch, g.id });
        }
        for (const auto& z : m.zoo_animals)
        {
            level(z.health, z.id);
            level(z.food, z.id);
            level(z.drink, z.id);
        }
        return out;
    }

} // namespace rct3::save
