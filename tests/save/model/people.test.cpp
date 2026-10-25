// SPDX-License-Identifier: GPL-3.0-or-later
//
// Synthetic saves only: class and attribute identifiers are the format's own, every number is invented.
#include "save/model/people.hpp"

#include "save/save_file.hpp"
#include "save/value.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <doctest/doctest.h>
#include <limits>
#include <map>
#include <memory>
#include <string>
#include <vector>

using namespace rct3::save;

namespace
{

    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& u8(std::uint8_t x)
        {
            v.push_back(static_cast<std::byte>(x));
            return *this;
        }
        Bytes& u16(std::uint16_t x)
        {
            return put(&x, 2);
        }
        Bytes& u32(std::uint32_t x)
        {
            return put(&x, 4);
        }
        Bytes& u64(std::uint64_t x)
        {
            return put(&x, 8);
        }
        Bytes& f32(float x)
        {
            return put(&x, 4);
        }
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& cat(const Bytes& o)
        {
            v.insert(v.end(), o.v.begin(), o.v.end());
            return *this;
        }
        Bytes& name(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            for (const char c : s)
                u8(static_cast<std::uint8_t>(c));
            return *this;
        }
        Bytes& blob(const Bytes& payload)
        {
            u32(static_cast<std::uint32_t>(payload.v.size()));
            return cat(payload);
        }
    };

    constexpr std::uint32_t kFill = 0xCDCDCDCDu;

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children;
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
        for (const auto& c : a.children)
            write_attr(b, c);
    }

    /// One synthetic object: its class name, id and attributes in declaration order.
    struct Obj
    {
        std::string cls;
        std::uint64_t id;
        std::vector<Attr> attrs;
        Bytes payload;

        Obj(std::string c, std::uint64_t i)
            : cls(std::move(c))
            , id(i)
        {
        }

        Obj& i32(const std::string& n, std::int32_t x)
        {
            attrs.push_back({ n, "int32", 4, { } });
            payload.u32(static_cast<std::uint32_t>(x));
            return *this;
        }
        Obj& u8(const std::string& n, std::uint8_t x)
        {
            attrs.push_back({ n, "uint8", 1, { } });
            payload.u8(x);
            return *this;
        }
        Obj& f32(const std::string& n, float x)
        {
            attrs.push_back({ n, "float32", 4, { } });
            payload.f32(x);
            return *this;
        }
        Obj& boolean(const std::string& n, bool x)
        {
            attrs.push_back({ n, "bool", 1, { } });
            payload.u8(x ? 1 : 0);
            return *this;
        }
        Obj& ref(const std::string& n, std::uint64_t x)
        {
            attrs.push_back({ n, "reference", 8, { } });
            payload.u64(x);
            return *this;
        }
        Obj& vec3(const std::string& n, float x, float y, float z)
        {
            attrs.push_back({ n, "vector3", 12, { } });
            payload.f32(x).f32(y).f32(z);
            return *this;
        }
        Obj& matrix(const std::string& n, float x, float y, float z)
        {
            attrs.push_back({ n, "matrix44", 64, { } });
            for (int i = 0; i < 12; ++i)
                payload.f32(i % 5 == 0 ? 1.0f : 0.0f);
            payload.f32(x).f32(y).f32(z).f32(1);
            return *this;
        }
        Obj& str(const std::string& n, const std::u16string& s)
        {
            attrs.push_back({ n, "string", 0, { } });
            Bytes p;
            p.u32(0xEFEFEFEFu);
            for (const auto c : s)
                p.u16(c);
            payload.blob(p);
            return *this;
        }
        Obj& refs(const std::string& n, const std::vector<std::uint64_t>& ids)
        {
            attrs.push_back({ n, "list", 0, { { "r", "reference", 8, { } } } });
            Bytes p;
            p.u32(static_cast<std::uint32_t>(ids.size()));
            for (const auto id_ : ids)
                p.u64(id_);
            payload.blob(p);
            return *this;
        }
        /// A list that claims `count` elements but carries no data.
        Obj& hostile_refs(const std::string& n, std::uint32_t count)
        {
            attrs.push_back({ n, "list", 0, { { "r", "reference", 8, { } } } });
            Bytes p;
            p.u32(count);
            payload.blob(p);
            return *this;
        }
        Obj& vec3s(const std::string& n, const std::vector<PeopleVec3>& pts)
        {
            attrs.push_back({ n, "list", 0, { { "p", "vector3", 12, { } } } });
            Bytes p;
            p.u32(static_cast<std::uint32_t>(pts.size()));
            for (const auto& q : pts)
                p.f32(q.x).f32(q.y).f32(q.z);
            payload.blob(p);
            return *this;
        }
        struct T
        {
            std::uint64_t about = 0, other = 0;
            std::int32_t id = 0;
            float time = -1;
            std::int32_t variant = 0;
        };
        Obj& thoughts(const std::string& n, const std::vector<T>& ts)
        {
            attrs.push_back({ n, "list", 0, { { "About", "reference", 8, { } }, { "GuestReference", "reference", 8, { } }, { "Thought", "int32", 4, { } }, { "Time", "float32", 4, { } }, { "Variant", "int32", 4, { } } } });
            Bytes p;
            p.u32(static_cast<std::uint32_t>(ts.size()));
            for (const auto& t : ts)
                p.u64(t.about).u64(t.other).u32(static_cast<std::uint32_t>(t.id)).f32(t.time).u32(static_cast<std::uint32_t>(t.variant));
            payload.blob(p);
            return *this;
        }
        /// Fixed-size struct of int32 and float32 children, written in order.
        struct Field
        {
            std::string name;
            bool is_float;
            std::uint32_t bits;
        };
        static Field fi(const std::string& n, std::int32_t v)
        {
            return { n, false, static_cast<std::uint32_t>(v) };
        }
        static Field ff(const std::string& n, float v)
        {
            return { n, true, std::bit_cast<std::uint32_t>(v) };
        }
        static Field ffill(const std::string& n)
        {
            return { n, true, kFill };
        }
        Obj& group(const std::string& n, const std::vector<Field>& fs)
        {
            Attr a { n, "struct", static_cast<std::uint32_t>(fs.size() * 4), { } };
            for (const auto& f : fs)
            {
                a.children.push_back({ f.name, f.is_float ? "float32" : "int32", 4, { } });
                payload.u32(f.bits);
            }
            attrs.push_back(std::move(a));
            return *this;
        }
    };

    struct World
    {
        std::vector<std::byte> bytes;
        SaveFile file;
        std::unique_ptr<ObjectIndex> index;
    };

    std::unique_ptr<World> make_world(const std::vector<Obj>& objs)
    {
        // One class per distinct class name, taking the attribute set of its first object.
        std::vector<std::string> names;
        for (const auto& o : objs)
            if (!std::ranges::contains(names, o.cls))
                names.push_back(o.cls);
        Bytes b;
        b.u32(static_cast<std::uint32_t>(names.size()));
        for (const auto& n : names)
        {
            const auto first = std::find_if(objs.begin(), objs.end(), [&](const Obj& o) { return o.cls == n; });
            b.name(n).u32(static_cast<std::uint32_t>(first->attrs.size()));
            for (const auto& a : first->attrs)
                write_attr(b, a);
        }
        b.u32(static_cast<std::uint32_t>(objs.size()));
        for (const auto& o : objs)
        {
            const auto idx = std::find(names.begin(), names.end(), o.cls) - names.begin();
            b.u32(static_cast<std::uint32_t>(idx)).u64(o.id).cat(o.payload);
        }
        auto w = std::make_unique<World>();
        w->bytes = std::move(b.v);
        auto f = read_save(w->bytes);
        REQUIRE(f);
        w->file = std::move(*f);
        w->index = std::make_unique<ObjectIndex>(w->file);
        return w;
    }

    /// Decodes the object with this id and runs `builder` on it.
    template<class F> auto build(const World& w, std::uint64_t id, F builder)
    {
        const auto o = w.index->find(id);
        REQUIRE(o);
        const auto d = decode_object(w.file, *o);
        REQUIRE(d);
        return builder(*d, *w.index);
    }

    Obj blank(const std::string& cls, std::uint64_t id)
    {
        return Obj(cls, id);
    }

} // namespace

TEST_CASE("a minimal guest decodes into typed fields")
{
    Obj g("Guest", 10);
    g.i32("PersonType", 1).u8("SubType", 1).i32("State", 16).i32("SubState", 2).f32("FacingAngle", 1.5f);
    g.matrix("WorldMatrix", 3, 4, 5).vec3("PathOffset", 0.25f, 0, -0.5f).str("NameS", u"Ann B");
    g.ref("StateTarget", 11).ref("GuestGroup", 12).ref("MyHolders", 13).ref("FavouriteRide", 11);
    g.group("CashInfo", { Obj::fi("ATMCash", 9000), Obj::fi("CurrentCash", 800), Obj::fi("SpentOnDrink", 10), Obj::fi("SpentOnFood", 20), Obj::fi("SpentOnParkEntry", 30), Obj::fi("SpentOnRides", 40), Obj::fi("SpentOnSouvenirs", 50), Obj::fi("TotalSpent", 150), Obj::fi("WithdrawnCash", 5) });
    g.group("Consumption", { Obj::ff("FoodF", 0.75f), Obj::ff("DrinkF", 0.5f), Obj::ff("ToiletF", 0.125f) });
    g.group("CurrentHealth", { Obj::ff("EnergyF", 0.25f), Obj::ff("HappinessF", 0.5f), Obj::ff("NauseaF", 0.0f), Obj::ff("SunBurnF", 0.0625f) });
    g.group("TargetHealth", { Obj::ff("EnergyF", 0.75f), Obj::ff("HappinessF", 1.0f), Obj::ff("NauseaF", 0.5f) });
    g.u8("PreferedRideIntensity", 2).f32("PreferedIntensityMin", -1).f32("PreferedIntensityMax", 7.5f);
    g.u8("NauseaTolerance", 3).i32("NumRidesDone", 4).i32("TimeInPark", 90).boolean("IsInPark", true);
    g.thoughts("RecentThoughts", { { 11, 0, 5, 12.5f, 777 }, { } });
    g.refs("AttractionsVisited", { 11, 99999 });
    g.u8("SkinPalette", 3).u8("UpperClothingPalette", 255).i32("HeadOptionals", 6).ref("HeadPartGroup", 11);
    const auto w = make_world({ g, blank("GuestGroup", 12), blank("GuestHolders", 13), blank("Ride", 11) });

    const auto guest = build(*w, 10, make_guest);
    REQUIRE(guest);
    const auto& p = guest->person;
    CHECK(p.id == 10);
    CHECK(p.type == PersonType::Teen);
    CHECK(p.sub_type == static_cast<std::uint8_t>(Sex::Female));
    CHECK(p.name == "Ann B");
    CHECK(p.state == static_cast<PersonState>(16));
    CHECK(p.guest_state() == GuestState::FollowingGroup);
    CHECK(p.sub_state == 2);
    CHECK(p.heading == 1.5f);
    CHECK(p.position == PeopleVec3 { 3, 4, 5 });
    CHECK(p.world[0] == 1.0f);
    CHECK(p.world[1] == 0.0f);
    CHECK(p.world[12] == 3.0f);
    CHECK(p.world[14] == 5.0f);
    CHECK(p.lane_offset == PeopleVec3 { 0.25f, 0, -0.5f });
    CHECK(p.state_target.id == 11);
    CHECK(p.holder.id == 13);
    CHECK(guest->group.id == 12);
    CHECK(guest->favourite_ride.id == 11);
    CHECK(guest->cash.bank == 9000);
    CHECK(guest->cash.current == 800);
    CHECK(guest->cash.total_spent == 150);
    CHECK(guest->needs.food == 0.75f);
    CHECK(guest->needs.toilet == 0.125f);
    CHECK(guest->mood_now.sunburn == 0.0625f);
    CHECK(guest->mood_target.energy == 0.75f);
    CHECK_FALSE(guest->mood_target.sunburn.has_value());
    CHECK(guest->ride_preference.intensity == RideIntensity::High);
    CHECK(guest->ride_preference.min == -1.0f);
    CHECK(guest->ride_preference.max == 7.5f);
    CHECK(guest->nausea_tolerance == NauseaTolerance::VeryHigh);
    CHECK(guest->counters.rides == 4);
    CHECK(guest->counters.minutes_in_park == 90);
    CHECK(guest->in_park);
    CHECK(p.thoughts[0].id == 5);
    CHECK(p.thoughts[0].about.id == 11);
    CHECK(p.thoughts[0].time == 12.5f);
    CHECK(p.thoughts[0].variant == 777);
    CHECK(p.thoughts[1].empty());
    CHECK(p.thoughts[5].empty());
    REQUIRE(p.attractions_visited.size() == 2);
    CHECK(p.attractions_visited[0].id == 11);
    CHECK(p.attractions_visited[1].is_none()); // dangling
    CHECK(p.appearance.palettes[0] == 3);
    CHECK(p.appearance.palettes[1] == 255);
    CHECK(p.appearance.optionals[0] == 6);
    CHECK(p.appearance.head.id == 11);
}

TEST_CASE("missing attributes take defaults")
{
    Obj g("Guest", 1);
    g.i32("State", 4);
    const auto w = make_world({ g });
    const auto guest = build(*w, 1, make_guest);
    REQUIRE(guest);
    CHECK(guest->person.state == PersonState::Wandering);
    CHECK(guest->person.name.empty());
    CHECK(guest->person.appearance.visible);
    CHECK(guest->person.look_at.distance == 1.5f);
    CHECK(guest->person.animation.anim_type == -1);
    CHECK(guest->needs.food == 1.0f);
    CHECK(guest->needs.toilet == 0.0f);
    CHECK(guest->ride_preference.intensity == RideIntensity::Medium);
    CHECK(guest->in_park);
    CHECK_FALSE(guest->is_avatar);
    CHECK_FALSE(guest->pool_need.has_value());
    CHECK_FALSE(guest->price_modifier.has_value());
    CHECK(guest->group.is_none());
    CHECK(guest->cash.bank == 0);
}

TEST_CASE("uninitialised fill becomes the default or none")
{
    Obj g("Guest", 1);
    g.f32("FacingAngle", std::bit_cast<float>(kFill)).i32("NumRidesDone", static_cast<std::int32_t>(kFill));
    g.group("Consumption", { Obj::ffill("FoodF"), Obj::ff("DrinkF", 0.25f), Obj::ffill("ToiletF") });
    g.f32("PoolNeed", std::bit_cast<float>(kFill)).f32("PriceModifier", 1.5f);
    g.matrix("WorldMatrix", std::bit_cast<float>(kFill), 2, 3);
    g.f32("MoveSpeed", std::numeric_limits<float>::quiet_NaN());
    const auto w = make_world({ g });
    const auto guest = build(*w, 1, make_guest);
    REQUIRE(guest);
    CHECK(guest->person.heading == 0.0f);
    CHECK(guest->counters.rides == 0);
    CHECK(guest->needs.food == 1.0f);
    CHECK(guest->needs.drink == 0.25f);
    CHECK(guest->needs.toilet == 0.0f);
    CHECK_FALSE(guest->pool_need.has_value());
    CHECK(guest->price_modifier == 1.5f);
    CHECK(guest->person.position == PeopleVec3 { 0, 2, 3 });
    CHECK(guest->person.animation.move_speed == 0.0f);
}

TEST_CASE("dangling references are none, live ones kept")
{
    Obj g("Guest", 1);
    g.ref("GuestGroup", 424242).ref("StateTarget", 2).ref("RouteController", 0).ref("LookAtTarget", 3);
    g.i32("FavouriteRide", 2); // a reference stored under an integer type still resolves
    const auto w = make_world({ g, blank("Bench", 2) });
    const auto guest = build(*w, 1, make_guest);
    REQUIRE(guest);
    CHECK(guest->group.is_none());
    CHECK(guest->person.state_target.id == 2);
    CHECK(resolve(guest->person.state_target, *w->index));
    CHECK(!resolve(guest->group, *w->index));
    CHECK(guest->person.route_controller.is_none());
    CHECK(guest->person.look_at.target.is_none());
    CHECK(guest->favourite_ride.id == 2);
}

TEST_CASE("values stored under the wrong declared type are reinterpreted")
{
    Obj g("Guest", 1);
    g.i32("TimeToNextThink", std::bit_cast<std::int32_t>(2.5f));             // float seconds in an int slot
    g.f32("ThinksToNextBigThink", std::bit_cast<float>(std::int32_t { 7 })); // small count in a float slot
    g.i32("IsInPark", 0).i32("PersonType", 2).i32("LookAtExtra", std::bit_cast<std::int32_t>(0.5f));
    g.i32("NameS", 5).f32("StateTarget", 1.0f).str("PhotosTaken", u"x");
    const auto w = make_world({ g });
    const auto guest = build(*w, 1, make_guest);
    REQUIRE(guest);
    CHECK(guest->person.time_to_next_think == 2.5f);
    CHECK(guest->person.thinks_to_big_think == 7);
    CHECK_FALSE(guest->in_park);
    CHECK(guest->person.type == PersonType::Adult);
    CHECK(guest->person.look_at.extra_bits == std::bit_cast<std::uint32_t>(0.5f));
    CHECK(guest->person.name.empty());           // not a string: ignored
    CHECK(guest->person.state_target.is_none()); // not a reference: ignored
    CHECK(guest->counters.photos == 0);
}

TEST_CASE("unknown enumeration values are kept")
{
    Obj g("Guest", 1);
    g.i32("State", 9999).i32("SubState", -3).i32("PersonType", 77).u8("PreferedRideIntensity", 9).u8("NauseaTolerance", 200);
    const auto w = make_world({ g });
    const auto guest = build(*w, 1, make_guest);
    REQUIRE(guest);
    CHECK(static_cast<std::int32_t>(guest->person.state) == 9999);
    CHECK(guest->person.sub_state == -3);
    CHECK_FALSE(guest->person.guest_state().has_value());
    CHECK(static_cast<std::uint8_t>(guest->person.type) == 77);
    CHECK(static_cast<std::uint8_t>(guest->ride_preference.intensity) == 9);
    CHECK(static_cast<std::uint8_t>(guest->nausea_tolerance) == 200);
    CHECK(std::string(to_string(guest->person.state)) == "other");
    CHECK(std::string(to_string(PersonState::Lost)) == "lost");
}

TEST_CASE("builders reject objects of another class")
{
    const auto w = make_world({ blank("Ride", 1) });
    CHECK_FALSE(build(*w, 1, make_guest).has_value());
    CHECK_FALSE(build(*w, 1, make_staff).has_value());
    CHECK_FALSE(build(*w, 1, make_animal).has_value());
    CHECK_FALSE(build(*w, 1, make_guest_group).has_value());
    CHECK_FALSE(build(*w, 1, make_litter).has_value());
}

TEST_CASE("staff roles carry their own data")
{
    Obj h("Handyman", 1);
    h.i32("PersonType", 4).i32("State", 14).i32("StaffTypeId", 2).i32("WageLevel", 2).i32("Training", 3);
    h.f32("Laziness", 0.5f).f32("Happiness", 0.75f).f32("ThinkTimer", 3.5f).i32("ThinksSinceHappy", -1);
    h.u8("StartCol", 10).u8("StartRow", 20).u8("GridSizeX", 5).u8("GridSizeY", 6).boolean("StartState", true);
    h.i32("BinsEmptied", 3).i32("PathsSwept", 8).vec3("DutyWayPoint", 1, 2, 3);
    Obj v("Vendor", 2);
    v.i32("PersonType", 8).ref("SceneryItem", 1).ref("AttractionInstance", 555).ref("Customer", 0);
    Obj m("Mechanic", 3);
    m.i32("ObjectsFixed", 4).f32("StateTimer", 1.5f).ref("ObjectMechanicInfo", 1);
    Obj k("AnimalKeeper", 4);
    k.i32("PersonType", 9).refs("SheltersToFill", { 1, 1, 777 }).i32("EnclosureID", 3);
    Obj l("LifeGuard", 5);
    l.ref("LifeGuardChair", 1).boolean("ChairBeenSet", true);
    Obj i("ParkInspector", 6);
    i.i32("PersonType", 3).i32("StaffTypeId", -1);
    Obj e("Entertainer", 7);
    e.u8("SubType", 5).i32("ShowPieceIndex", 2).boolean("Bad", true);
    Obj s("SecurityGuard", 8);
    const auto w = make_world({ h, v, m, k, l, i, e, s });

    const auto hs = build(*w, 1, make_staff);
    REQUIRE(hs);
    CHECK(hs->kind == StaffKind::Handyman);
    CHECK(hs->person.type == PersonType::Handyman);
    CHECK(hs->type_id == 2);
    CHECK(hs->wage_level == 2);
    CHECK(hs->training == 3);
    CHECK(hs->laziness == 0.5f);
    CHECK(hs->happiness == 0.75f);
    CHECK(hs->thinks_since_happy == -1);
    CHECK(hs->patrol.start_col == 10);
    CHECK(hs->patrol.size_y == 6);
    CHECK(hs->patrol.start_state);
    const auto* hw = std::get_if<HandymanWork>(&hs->work);
    REQUIRE(hw != nullptr);
    CHECK(hw->bins_emptied == 3);
    CHECK(hw->paths_swept == 8);
    CHECK(hw->duty_waypoint == PeopleVec3 { 1, 2, 3 });

    const auto vs = build(*w, 2, make_staff);
    REQUIRE(vs);
    const auto* vw = std::get_if<VendorWork>(&vs->work);
    REQUIRE(vw != nullptr);
    CHECK(vw->scenery_item.id == 1);
    CHECK(vw->attraction_instance.is_none());
    CHECK(vs->wage_level == 1);   // default
    CHECK(vs->happiness == 0.5f); // default

    const auto ms = build(*w, 3, make_staff);
    REQUIRE(ms);
    CHECK(ms->person.type == PersonType::Mechanic); // from the class when the attribute is absent
    const auto* mw = std::get_if<MechanicWork>(&ms->work);
    REQUIRE(mw != nullptr);
    CHECK(mw->objects_fixed == 4);
    CHECK(mw->state_timer == 1.5f);
    CHECK(mw->info.id == 1);

    const auto ks = build(*w, 4, make_staff);
    REQUIRE(ks);
    const auto* kw = std::get_if<KeeperWork>(&ks->work);
    REQUIRE(kw != nullptr);
    REQUIRE(kw->shelters_to_fill.size() == 3);
    CHECK(kw->shelters_to_fill[2].is_none());
    CHECK(kw->enclosure_id == 3);

    const auto ls = build(*w, 5, make_staff);
    REQUIRE(ls);
    CHECK(ls->kind == StaffKind::LifeGuard);
    CHECK(std::get<LifeGuardWork>(ls->work).chair_been_set);

    const auto is = build(*w, 6, make_staff);
    REQUIRE(is);
    CHECK(is->kind == StaffKind::ParkInspector);
    CHECK(is->person.type == PersonType::Special);
    CHECK(std::holds_alternative<std::monostate>(is->work));

    const auto es = build(*w, 7, make_staff);
    REQUIRE(es);
    CHECK(es->person.sub_type == 5);
    CHECK(std::get<EntertainerWork>(es->work).bad);

    const auto ss = build(*w, 8, make_staff);
    REQUIRE(ss);
    CHECK(ss->kind == StaffKind::SecurityGuard);
    CHECK(std::string(to_string(ss->kind)) == "security-guard");
}

TEST_CASE("animals of each kind")
{
    Obj d("Duck", 1);
    d.matrix("WorldMatrix", 7, 8, 9).i32("AnimType", 2).i32("State", 512).i32("SubState", 1).u8("Palette", 3);
    d.boolean("Leader", true).ref("LeftDuck", 2).ref("RightDuck", 888).f32("StateTimer", 4.5f).f32("TimeInPark", 100);
    d.vec3("TargetPos", 1, 2, 3);
    Obj f("Fish", 2);
    f.i32("FishType", 2).f32("SwimRadius", 0.5f).ref("LeftFish", 1).f32("SwimSpeedPerStep", 0.25f).f32("NormalSwimSpeed", 3);
    f.f32("SwimSlowAtTarget", std::bit_cast<float>(kFill));
    Obj g("Seagull", 3);
    g.ref("Victim", 1).i32("LitterPecked", 4).i32("GuestsAttacked", 1);
    const auto w = make_world({ d, f, g });

    const auto da = build(*w, 1, make_animal);
    REQUIRE(da);
    CHECK(da->kind == AnimalKind::Duck);
    CHECK(da->position == PeopleVec3 { 7, 8, 9 });
    CHECK(da->state == 512);
    CHECK(da->palette == 3);
    CHECK(da->leader);
    CHECK(da->left.id == 2);
    CHECK(da->right.is_none());
    CHECK(da->state_timer == 4.5f);
    CHECK(da->target == PeopleVec3 { 1, 2, 3 });

    const auto fa = build(*w, 2, make_animal);
    REQUIRE(fa);
    CHECK(fa->kind == AnimalKind::Fish);
    CHECK(fa->fish_type == 2);
    CHECK(fa->left.id == 1);
    CHECK(fa->swim_speed_per_step == 0.25f);
    CHECK(fa->normal_swim_speed == 3.0f);

    const auto ga = build(*w, 3, make_animal);
    REQUIRE(ga);
    CHECK(ga->kind == AnimalKind::Seagull);
    CHECK(ga->victim.id == 1);
    CHECK(ga->litter_pecked == 4);
}

TEST_CASE("zoo animal")
{
    Obj z("WildAnimal", 1);
    z.str("FirstAndMiddleName", u"Rex the").boolean("IsMale", true).boolean("IsAdult", false).f32("DateOfBirth", -50);
    z.f32("Health", 0.5f).i32("AdoptionFee", 100).i32("SSHunger", 12).f32("PregnancyTimer", -1);
    z.ref("Entry", 2).ref("FamilyMembership", 31337).thoughts("ThoughtsAbout", { { 0, 0, 250, 3, 9 } });
    const auto w = make_world({ z, blank("Entry", 2) });
    const auto a = build(*w, 1, make_zoo_animal);
    REQUIRE(a);
    CHECK(a->first_names == "Rex the");
    CHECK(a->is_male);
    CHECK_FALSE(a->is_adult);
    CHECK(a->date_of_birth == -50.0f);
    CHECK(a->health == 0.5f);
    CHECK(a->adoption_fee == 100);
    CHECK(a->species_hunger == 12);
    CHECK(a->pregnancy_timer == -1.0f);
    CHECK(a->entry.id == 2);
    CHECK(a->family_membership.is_none());
    CHECK(a->thoughts[0].id == 250);
}

TEST_CASE("a zoo animal takes its species from its entry and its place from its visual")
{
    Obj z("WildAnimal", 1);
    z.ref("Entry", 2).ref("Visual", 3);
    Obj e("WASDatabaseEntry", 2);
    e.str("SYMBOLNAME", u"wombat");
    Obj v("WildAnimalVisual", 3);
    v.matrix("WorldMatrix", 4, 5, 6);
    const auto w = make_world({ z, e, v });
    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    REQUIRE(model->zoo_animals.size() == 1);
    const auto& a = model->zoo_animals[0];
    CHECK(a.species == "wombat");
    CHECK(a.world[0] == 1.0f);
    CHECK(a.world[12] == 4.0f);
    CHECK(a.world[14] == 6.0f);
}

TEST_CASE("guest groups, holders, items and litter")
{
    Obj g("GuestGroup", 1);
    g.i32("GuestGroupType", 4).i32("NumMembers", 3).ref("Leader", 10).refs("Members", { 10, 11, 9999, 0 });
    g.i32("FlockState", 16).f32("MinFood", 0.5f).f32("AvgSunBurn", 0.25f).i32("SetPieceType", 2);
    g.vec3s("SetPiecePos", { { 1, 2, 3 }, { 4, 5, 6 } }).boolean("SetPieceTakePhoto", true);
    Obj h("GuestHolders", 2);
    h.ref("Person", 10).i32("ConsumingItem", -1).ref("LHandHolder", 3).ref("RHandHolder", 4040).i32("HiddenBits", 88);
    Obj c("CarriedItemInstance", 3);
    c.ref("CIDEntry", 10).str("Name", u"Thing").f32("Drink", -0.5f).boolean("Bad", true).u8("FlexiColour1", 4);
    Obj s("ShopItemInstance", 4);
    s.str("Name", u"Item").i32("Price", 150).i32("TotalSold", 12).ref("Stall", 10);
    Obj x("ShopExtraInstance", 5);
    x.i32("Setting", 3);
    Obj t("Litter", 6);
    t.vec3("Position", 1, 0, 2).vec3("Orientation", 0, 1.5f, 0).u8("LitterType", 3).u8("LitterState", 1).f32("CreateTime", 9).ref("PathTile", 10);
    const auto w = make_world({ g, h, c, s, x, t, blank("Guest", 10), blank("Guest", 11) });

    const auto grp = build(*w, 1, make_guest_group);
    REQUIRE(grp);
    CHECK(grp->group_type == 4);
    CHECK(grp->num_members == 3);
    CHECK(grp->leader.id == 10);
    REQUIRE(grp->members.size() == 4);
    CHECK(grp->members[2].is_none());
    CHECK(grp->live_members() == 2);
    CHECK(grp->flock_state == 16);
    CHECK(grp->average_mood.sunburn == 0.25f);
    CHECK(grp->set_piece_positions.size() == 2);
    CHECK(grp->set_piece_positions[1] == PeopleVec3 { 4, 5, 6 });
    CHECK(grp->set_piece_take_photo);

    const auto hold = build(*w, 2, make_item_holder);
    REQUIRE(hold);
    CHECK(hold->owner.id == 10);
    CHECK(hold->consuming_slot == -1);
    CHECK(hold->left_hand.id == 3);
    CHECK(hold->right_hand.is_none());
    CHECK(hold->hidden_parts == 88u);

    const auto item = build(*w, 3, make_carried_item);
    REQUIRE(item);
    CHECK(item->definition.id == 10);
    CHECK(item->name == "Thing");
    CHECK(item->drink == -0.5f);
    CHECK(item->bad);
    CHECK(item->colours[1] == 4);
    CHECK(item->inventory_choice == -1);

    const auto shop = build(*w, 4, make_shop_item);
    REQUIRE(shop);
    CHECK(shop->price == 150);
    CHECK(shop->total_sold == 12);
    CHECK(shop->stall.id == 10);

    const auto extra = build(*w, 5, make_shop_extra);
    REQUIRE(extra);
    CHECK(extra->setting == 3);

    const auto lit = build(*w, 6, make_litter);
    REQUIRE(lit);
    CHECK(lit->position == PeopleVec3 { 1, 0, 2 });
    CHECK(lit->orientation.y == 1.5f);
    CHECK(lit->type == 3);
    CHECK(lit->state == LitterState::Carried);
    CHECK(lit->create_time == 9.0f);
    CHECK(lit->path_tile.id == 10);
}

TEST_CASE("hostile list counts do not allocate or fail")
{
    Obj g("GuestGroup", 1);
    g.hostile_refs("Members", 0xFFFFFFFFu).i32("NumMembers", -5);
    Obj p("Guest", 2);
    p.hostile_refs("AttractionsVisited", 0x7FFFFFFFu).hostile_refs("RecentThoughts", 0xFFFFFFF0u);
    Obj k("AnimalKeeper", 3);
    k.hostile_refs("SheltersToFill", 0x10000000u);
    const auto w = make_world({ g, p, k });
    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    REQUIRE(model->groups.size() == 1);
    CHECK(model->groups[0].live_members() == 0);
    CHECK(model->groups[0].num_members == -5);
    REQUIRE(model->guests.size() == 1);
    CHECK(model->guests[0].person.attractions_visited.empty());
    CHECK(model->guests[0].person.thoughts[0].empty());
    CHECK(model->staff.size() == 1);
    // A negative stored count is reported, not trusted.
    const auto issues = validate(*model);
    CHECK(std::any_of(issues.begin(), issues.end(), [](const Issue& i) { return i.kind == IssueKind::GroupMemberCountMismatch; }));
}

TEST_CASE("oversized member lists are truncated")
{
    std::vector<std::uint64_t> many(kMaxGroupMembers + 20, 2);
    Obj g("GuestGroup", 1);
    g.refs("Members", many);
    const auto w = make_world({ g, blank("Guest", 2) });
    const auto grp = build(*w, 1, make_guest_group);
    REQUIRE(grp);
    CHECK(grp->members.size() == kMaxGroupMembers);
}

namespace
{
    Obj consistent_guest(std::uint64_t id, std::uint64_t group, std::uint64_t holder, float food = 0.5f, std::int32_t drink_spent = 10, std::int32_t total = 10)
    {
        Obj g("Guest", id);
        g.i32("PersonType", 2).ref("GuestGroup", group).ref("MyHolders", holder);
        g.group("CashInfo", { Obj::fi("ATMCash", 100), Obj::fi("CurrentCash", 50), Obj::fi("SpentOnDrink", drink_spent), Obj::fi("SpentOnFood", 0), Obj::fi("SpentOnParkEntry", 0), Obj::fi("SpentOnRides", 0), Obj::fi("SpentOnSouvenirs", 0), Obj::fi("TotalSpent", total), Obj::fi("WithdrawnCash", 0) });
        g.group("Consumption", { Obj::ff("FoodF", food), Obj::ff("DrinkF", 0.5f), Obj::ff("ToiletF", 0.5f) });
        return g;
    }
} // namespace

TEST_CASE("a consistent population validates cleanly")
{
    Obj grp("GuestGroup", 20);
    grp.i32("NumMembers", 2).refs("Members", { 1, 2, 0 });
    Obj h1("GuestHolders", 30), h2("GuestHolders", 31), hs("GuestHolders", 32);
    h1.ref("Person", 1);
    h2.ref("Person", 2);
    hs.ref("Person", 40);
    Obj st("Handyman", 40);
    st.i32("PersonType", 4).ref("MyHolders", 32);
    const auto w = make_world({ consistent_guest(1, 20, 30), consistent_guest(2, 20, 31), grp, h1, h2, hs, st });
    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    CHECK(model->guests.size() == 2);
    CHECK(model->staff.size() == 1);
    CHECK(model->holders.size() == 3);
    CHECK(model->groups.size() == 1);
    CHECK(validate(*model).empty());
}

TEST_CASE("validation reports broken invariants")
{
    auto bad = consistent_guest(1, 777, 30); // group does not exist
    const auto out_of_range = consistent_guest(2, 20, 99, 1.5f, 50, 10);
    Obj grp("GuestGroup", 20);
    grp.i32("NumMembers", 1).refs("Members", { 1, 40 }); // member 40 is not a guest, count wrong
    Obj h("GuestHolders", 30);
    h.ref("Person", 1);
    Obj orphan("GuestHolders", 31);
    orphan.ref("Person", 40);
    Obj notperson("Ride", 40);
    const auto w = make_world({ bad, out_of_range, grp, h, orphan, notperson });
    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    const auto issues = validate(*model);
    const auto has = [&](IssueKind k, std::uint64_t id) { return std::any_of(issues.begin(), issues.end(), [&](const Issue& i) { return i.kind == k && i.id == id; }); };
    CHECK(has(IssueKind::GuestWithoutGroup, 1));
    CHECK(has(IssueKind::PersonWithoutHolder, 2));
    CHECK(has(IssueKind::StatOutOfRange, 2));
    CHECK(has(IssueKind::SpendingBelowCategories, 2));
    CHECK(has(IssueKind::GroupMemberNotGuest, 20));
    CHECK(has(IssueKind::GroupMemberCountMismatch, 20));
    CHECK(has(IssueKind::HolderOwnerNotPerson, 31));
    CHECK(std::string(to_string(IssueKind::StatOutOfRange)) == "stat-out-of-range");
}

TEST_CASE("part groups name the sets people are dressed from")
{
    Obj g("PPGDatabaseEntry", 7);
    g.str("SymbolName", u"XY01_BODY").u8("PartGroup", 1).u8("PeopleSex", 1).u8("PeopleType", 2);
    const auto w = make_world({ g, blank("Ride", 1) });
    CHECK_FALSE(build(*w, 1, [](const auto& d, const auto&) { return make_part_group(d); }).has_value());
    const auto group = build(*w, 7, [](const auto& d, const auto&) { return make_part_group(d); });
    REQUIRE(group);
    CHECK(group->id == 7);
    CHECK(group->symbol == "XY01_BODY");
    CHECK(group->section == 1);
    CHECK(group->sex == 1);
    CHECK(group->type == 2);

    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    REQUIRE(model->part_groups.size() == 1);
    CHECK(model->part_groups[0].symbol == "XY01_BODY");
}

TEST_CASE("an empty save gives an empty model")
{
    const auto w = make_world({ blank("Ride", 1) });
    const auto model = load_people(w->file, *w->index);
    REQUIRE(model);
    CHECK(model->guests.empty());
    CHECK(model->staff.empty());
    CHECK(model->animals.empty());
    CHECK(validate(*model).empty());
}
