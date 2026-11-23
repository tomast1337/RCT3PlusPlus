// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/show_effect.hpp"

#include "core/types.hpp"
#include "particles/display.hpp"
#include "save/save_file.hpp"

#include <cmath>
#include <cstdint>
#include <doctest/doctest.h>
#include <numbers>
#include <string>
#include <utility>
#include <vector>

using namespace rct3::particles;

namespace
{

    // A container without its optional header: classes, then objects, each field as the class lays it out.
    struct Bytes
    {
        std::vector<std::byte> v;
        Bytes& put(const void* p, usize n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& put_u8(u8 x)
        {
            return put(&x, 1);
        }
        Bytes& put_u16(u16 x)
        {
            return put(&x, 2);
        }
        Bytes& put_u32(u32 x)
        {
            return put(&x, 4);
        }
        Bytes& put_u64(u64 x)
        {
            return put(&x, 8);
        }
        Bytes& put_f32(f32 x)
        {
            return put(&x, 4);
        }
        Bytes& name(const std::string& s)
        {
            put_u16(static_cast<u16>(s.size()));
            return put(s.data(), s.size());
        }
        /// A variable-size field: its length, then its bytes.
        Bytes& field(const Bytes& b)
        {
            put_u32(static_cast<u32>(b.v.size()));
            v.insert(v.end(), b.v.begin(), b.v.end());
            return *this;
        }
    };

    struct Attr
    {
        std::string name, type;
        u32 size = 0;
        std::vector<Attr> children { };
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).put_u32(a.size).put_u32(static_cast<u32>(a.children.size()));
        for (const Attr& c : a.children)
            write_attr(b, c);
    }

    using Classes = std::vector<std::pair<std::string, std::vector<Attr>>>;

    Bytes class_table(const Classes& classes)
    {
        Bytes b;
        b.put_u32(static_cast<u32>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).put_u32(static_cast<u32>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        return b;
    }

    Bytes text(const std::string& s)
    {
        Bytes b;
        return b.put(s.data(), s.size());
    }

    // A timeline: up to its count of control points, each an element and a time, stored side by side.
    const std::pair<std::string, std::vector<Attr>> kTimeline { "Blender", { { "FixedPts", "array", 0, { { "BlenderElement", "managedobjectptr", 8 }, { "PointTime", "float32", 4 } } }, { "MaxTime", "float32", 4 }, { "NumPts", "int32", 4 } } };
    const std::pair<std::string, std::vector<Attr>> kNumber { "FloatBlenderElement", { { "Float", "float32", 4 } } };
    const std::pair<std::string, std::vector<Attr>> kColour { "ColourBlenderElement", { { "Colour", "int32", 4 } } };

    struct TimelinePoint
    {
        u64 element = 0;
        f32 seconds = 0;
    };

    /// A timeline's fields over `longest` seconds, using the first `used` of `points`.
    Bytes timeline(const std::vector<TimelinePoint>& points, u32 used, f32 longest)
    {
        Bytes stored;
        stored.put_u32(static_cast<u32>(points.size()));
        for (const TimelinePoint& p : points)
            stored.put_u64(p.element).put_f32(p.seconds);
        Bytes b;
        b.field(stored).put_f32(longest).put_u32(used);
        return b;
    }

    /// A laser (id 1) of 8 seconds: four beams of the plane look, red-orange at half opacity, an elevation
    /// timeline (id 2) whose points are stored latest first with a leftover past its count, and one drawing (id 7)
    /// of two points, a jump and a lit line, with a third stored past its count.
    std::vector<std::byte> invented_laser()
    {
        Bytes b = class_table(
            {
                { "LaserDesc", { { "ColourBlender", "managedobjectptr", 8 }, { "Duration", "float32", 4 }, { "ElevationBlender", "managedobjectptr", 8 }, { "LaserWord", "managedobjectptr", 8 }, { "Name", "string", 0 }, { "NumBeams", "int32", 4 }, { "Power", "float32", 4 }, { "RenderType", "int32", 4 } } },
                kTimeline,
                kNumber,
                kColour,
                { "LaserWritingWord", { { "Letters", "array", 0, { { "Ltr", "managedobjectptr", 8 } } } } },
                { "LaserWritingLetter", { { "DragPoints", "array", 0, { { "X", "float32", 4 }, { "Y", "float32", 4 } } }, { "Duration", "float32", 4 }, { "NumPts", "int32", 4 }, { "SolidConnection", "array", 0, { { "S", "bool", 1 } } } } },
            });
        Bytes letters;
        letters.put_u32(1).put_u64(7);
        Bytes drag;
        drag.put_u32(3).put_f32(4).put_f32(5).put_f32(10).put_f32(5).put_f32(99).put_f32(99);
        Bytes lit;
        lit.put_u32(3).put_u8(0).put_u8(1).put_u8(1);
        b.put_u32(8);
        b.put_u32(0).put_u64(1).put_u64(3).put_f32(8).put_u64(2).put_u64(6).field(text("Sweep")).put_u32(4).put_f32(150).put_u32(1);
        Bytes elevation = timeline({ { 4, 6 }, { 5, 2 }, { 4, 0 } }, 2, 8);
        b.put_u32(1).put_u64(2);
        b.v.insert(b.v.end(), elevation.v.begin(), elevation.v.end());
        Bytes colour = timeline({ { 8, 4 } }, 1, 8);
        b.put_u32(1).put_u64(3);
        b.v.insert(b.v.end(), colour.v.begin(), colour.v.end());
        b.put_u32(2).put_u64(4).put_f32(0.25f);
        b.put_u32(2).put_u64(5).put_f32(1.0f);
        b.put_u32(4).put_u64(6).field(letters);
        b.put_u32(5).put_u64(7).field(drag).put_f32(1.5f).put_u32(2).field(lit);
        b.put_u32(3).put_u64(8).put_u32((0x80u << 24) | (0xffu << 16) | (0x40u << 8));
        return b.v;
    }

    /// A water jet (id 1) of 10 seconds whose pressure rises from 12 to 16 meters a second.
    std::vector<std::byte> invented_water_jet()
    {
        Bytes b = class_table(
            {
                { "WaterJetBaseDesc", { { "Duration", "float32", 4 }, { "Name", "string", 0 }, { "PressureBlender", "managedobjectptr", 8 } } },
                kTimeline,
                kNumber,
            });
        b.put_u32(4);
        b.put_u32(0).put_u64(1).put_f32(10).field(text("Spout")).put_u64(2);
        Bytes pressure = timeline({ { 3, 0 }, { 4, 10 } }, 2, 10);
        b.put_u32(1).put_u64(2);
        b.v.insert(b.v.end(), pressure.v.begin(), pressure.v.end());
        b.put_u32(2).put_u64(3).put_f32(12);
        b.put_u32(2).put_u64(4).put_f32(16);
        return b.v;
    }

    /// A park's laser display: one laser shot at 3 seconds whose source (id 2) names a laser of 8 seconds (id 3).
    std::vector<std::byte> invented_laser_show()
    {
        const Attr shot { "EntriesElement", "struct", 0, { { "EffectType", "int32", 4 }, { "Name", "string", 0 }, { "ParticleSourceRef", "reference", 8 }, { "SceneryItemRef", "reference", 8 }, { "Time", "float32", 4 } } };
        Bytes b = class_table(
            {
                { "FireworkDisplay", { { "Name", "string", 0 }, { "Entries", "array", 0, { shot } } } },
                { "LaserSource", { { "LaserBaseDesc", "reference", 8 } } },
                { "LaserBaseDesc", { { "Duration", "float32", 4 }, { "Name", "string", 0 } } },
            });
        Bytes one;
        one.put_u32(1).field(text("Lasers\\Sweep.las")).put_u64(2).put_u64(40).put_f32(3);
        Bytes shots;
        shots.put_u32(1).field(one);
        b.put_u32(3);
        b.put_u32(0).put_u64(1).field(text("Beams")).field(shots);
        b.put_u32(1).put_u64(2).put_u64(3);
        b.put_u32(2).put_u64(3).put_f32(8).field(text("Sweep"));
        return b.v;
    }

    /// A park's firework display (id 1) of two shots, from placed launchers 40 and 41. Launcher 40 keeps an aim of
    /// 0.2 up and 0.5 round; 41 keeps none. The display's keys (timeline id 5, of 10 seconds) are stored latest first,
    /// with a leftover past their count: at 6 seconds (key 7) launcher 40 aims 1.5 up and 3 round, beside an item no
    /// shot fires from; at 2 seconds (key 6) 0.5 up and 1 round, with a leftover slot naming 41 past the key's count.
    std::vector<std::byte> invented_aimed_show()
    {
        const Attr shot { "EntriesElement", "struct", 0, { { "EffectType", "int32", 4 }, { "Name", "string", 0 }, { "ParticleSourceRef", "reference", 8 }, { "SceneryItemRef", "reference", 8 }, { "Time", "float32", 4 } } };
        const Attr aim { "FireworkSlotTransform", "struct", 0, { { "Angle", "float32", 4 }, { "Elevation", "float32", 4 } } };
        const Attr slot { "SlotInfoElement", "struct", 0, { { "Angle", "float32", 4 }, { "Elevation", "float32", 4 }, { "SceneryItem", "managedobjectptr", 8 } } };
        Bytes b = class_table(
            {
                { "FireworkDisplay", { { "Name", "string", 0 }, { "Entries", "array", 0, { shot } }, { "SlotKeyFrames", "managedobjectptr", 8 } } },
                { "SceneryItem", { aim } },
                kTimeline,
                { "FireworkSlotKeyFrame", { { "NumCachedItems", "int32", 4 }, { "SlotInfo", "array", 0, { slot } } } },
            });
        const auto entry = [](u64 item, f32 seconds) {
            Bytes one;
            one.put_u32(0).field(text("Fireworks\\Peony.frw")).put_u64(0).put_u64(item).put_f32(seconds);
            return one;
        };
        Bytes shots;
        shots.put_u32(2).field(entry(40, 1)).field(entry(41, 3));
        const auto aimed = [](f32 angle, f32 elevation) {
            Bytes one;
            one.put_f32(angle).put_f32(elevation);
            return one;
        };
        const auto slot_of = [](f32 angle, f32 elevation, u64 item) {
            Bytes one;
            one.put_f32(angle).put_f32(elevation).put_u64(item);
            return one;
        };
        b.put_u32(6);
        b.put_u32(0).put_u64(1).field(text("Aimed")).field(shots).put_u64(5);
        b.put_u32(1).put_u64(40).field(aimed(0.5f, 0.2f));
        b.put_u32(1).put_u64(41).field(aimed(0, 0));
        Bytes keys = timeline({ { 7, 6 }, { 6, 2 }, { 6, 99 } }, 2, 10);
        b.put_u32(2).put_u64(5);
        b.v.insert(b.v.end(), keys.v.begin(), keys.v.end());
        Bytes early;
        early.put_u32(2).field(slot_of(1, 0.5f, 40)).field(slot_of(9, 9, 41));
        b.put_u32(3).put_u64(6).put_u32(1).field(early);
        Bytes late;
        late.put_u32(2).field(slot_of(3, 1.5f, 40)).field(slot_of(-1, 0.25f, 99));
        b.put_u32(3).put_u64(7).put_u32(2).field(late);
        return b.v;
    }

} // namespace

TEST_CASE("a laser file reads its settings, its timelines in time order and its drawings")
{
    const auto laser = read_laser(invented_laser());
    REQUIRE(laser.has_value());
    CHECK(laser->name == "Sweep");
    CHECK(laser->seconds == doctest::Approx(8));
    CHECK(laser->look == LaserLook::Plane);
    CHECK(laser->beams == 4);
    CHECK(laser->power == doctest::Approx(150));
    REQUIRE(laser->elevation.times.size() == 2);
    CHECK(laser->elevation.times[0] == doctest::Approx(0.25));
    CHECK(laser->elevation.at(0.0f) == doctest::Approx(1));
    CHECK(laser->elevation.at(1.0f) == doctest::Approx(0.25));
    CHECK(laser->colour[0].at(0.5f) == doctest::Approx(1));
    CHECK(laser->colour[1].at(0.5f) == doctest::Approx(64.0 / 255));
    CHECK(laser->colour[2].at(0.5f) == doctest::Approx(0));
    CHECK(laser->colour[3].at(0.5f) == doctest::Approx(128.0 / 255));
    CHECK(laser->spin.values.empty());
    REQUIRE(laser->drawings.size() == 1);
    const LaserDrawing& d = laser->drawings.front();
    CHECK(d.seconds == doctest::Approx(1.5));
    REQUIRE(d.points.size() == 2);
    CHECK_FALSE(d.points[0].lit);
    CHECK(d.points[1].lit);
    CHECK(d.points[1].at.x == doctest::Approx(10));
}

TEST_CASE("a water jet file reads its pattern")
{
    const auto jet = read_water_jet(invented_water_jet());
    REQUIRE(jet.has_value());
    CHECK(jet->name == "Spout");
    CHECK(jet->seconds == doctest::Approx(10));
    CHECK(jet->pressure.at(0.5f) == doctest::Approx(14));
    CHECK(jet->elevation.values.empty());
}

TEST_CASE("a show effect file of another kind is told apart")
{
    CHECK(read_water_jet(invented_laser()).error() == ShowEffectError::NoEffect);
    CHECK(read_laser(invented_water_jet()).error() == ShowEffectError::NoEffect);
    CHECK(read_laser_drawing(invented_water_jet()).error() == ShowEffectError::NoEffect);
    const std::vector<std::byte> laser = invented_laser();
    const std::vector<std::byte> cut(laser.begin(), laser.begin() + 40);
    CHECK(read_laser(cut).error() == ShowEffectError::Container);
}

TEST_CASE("a laser drawing file reads its points")
{
    const auto drawing = read_laser_drawing(invented_laser());
    REQUIRE(drawing.has_value());
    CHECK(drawing->points.size() == 2);
}

TEST_CASE("a park's laser shot carries its laser, and the display runs until the laser ends")
{
    const std::vector<std::byte> park = invented_laser_show();
    const auto file = rct3::save::read_save(park, { .verify_checksum = false });
    REQUIRE(file.has_value());
    const auto displays = read_park_displays(*file);
    REQUIRE(displays.has_value());
    REQUIRE(displays->size() == 1);
    const Shot& shot = displays->front().shots.front();
    CHECK(shot.kind == ShotKind::Laser);
    REQUIRE(shot.laser.has_value());
    CHECK(shot.laser->name == "Sweep");
    CHECK(display_seconds(displays->front()) == doctest::Approx(11));
}

TEST_CASE("a laser turns by its spin summed over its run")
{
    Laser laser { .seconds = 4 };
    SUBCASE("without spin it stays")
    {
        CHECK(laser_turn(laser, 3) == 0);
    }
    SUBCASE("a steady spin")
    {
        laser.spin = { .times = { 0.5f }, .values = { 2 } };
        CHECK(laser_turn(laser, 3) == doctest::Approx(6));
    }
    SUBCASE("a spin speeding up from still, held after its last point")
    {
        laser.spin = { .times = { 0, 0.5f }, .values = { 0, 2 } };
        CHECK(laser_turn(laser, 2) == doctest::Approx(2));
        CHECK(laser_turn(laser, 4) == doctest::Approx(6));
        CHECK(laser_turn(laser, -1) == 0);
    }
}

TEST_CASE("a laser's beams point round a cone or across a fan")
{
    std::vector<glm::vec3> ways;
    const auto near = [](glm::vec3 a, glm::vec3 b) { return glm::distance(a, b) < 1e-5f; };
    SUBCASE("a level cone of four, a quarter turn apart round the tube")
    {
        laser_beams({ .seconds = 1, .look = LaserLook::Cone, .beams = 4 }, 0, ways);
        REQUIRE(ways.size() == 4);
        CHECK(near(ways[0], { 0, 0, 1 }));
        CHECK(near(ways[1], { 1, 0, 0 }));
        CHECK(near(ways[2], { 0, 0, -1 }));
        CHECK(near(ways[3], { -1, 0, 0 }));
    }
    SUBCASE("a cone raised and turned")
    {
        laser_beams({ .seconds = 2, .look = LaserLook::Cone, .beams = 2, .elevation = { .times = { 0 }, .values = { 0.5f } }, .spin = { .times = { 0 }, .values = { 0.25f } } }, 2, ways);
        REQUIRE(ways.size() == 2);
        CHECK(near(ways[0], { std::cos(0.5f) * std::sin(0.5f), std::sin(0.5f), std::cos(0.5f) * std::cos(0.5f) }));
        CHECK(near(ways[1], { -ways[0].x, ways[0].y, -ways[0].z }));
    }
    SUBCASE("a level fan of three: front, up the tube, back")
    {
        laser_beams({ .seconds = 1, .look = LaserLook::Plane, .beams = 3 }, 0, ways);
        REQUIRE(ways.size() == 3);
        CHECK(near(ways[0], { 0, 0, 1 }));
        CHECK(near(ways[1], { 0, 1, 0 }));
        CHECK(near(ways[2], { 0, 0, -1 }));
    }
    SUBCASE("a drawing laser without drawings shines nothing")
    {
        ways.push_back({ });
        laser_beams({ .seconds = 1, .look = LaserLook::Drawing, .beams = 1 }, 0, ways);
        CHECK(ways.empty());
    }
}

namespace
{

    // A square drawn in four lit strokes after a dark jump to its corner, a second for its five points.
    LaserDrawing invented_square()
    {
        return { .seconds = 1, .points = { { .at = { 4, 4 } }, { .at = { 12, 4 }, .lit = true }, { .at = { 12, 12 }, .lit = true }, { .at = { 4, 12 }, .lit = false }, { .at = { 4, 4 }, .lit = true } } };
    }

} // namespace

TEST_CASE("a drawing laser's pen runs along its drawings")
{
    const auto near = [](glm::vec2 a, glm::vec2 b) { return glm::distance(a, b) < 1e-4f; };
    Laser laser { .seconds = 10, .look = LaserLook::Drawing, .drawings = { invented_square() } };
    SUBCASE("each point takes the same share of the drawing's time, measured from its first point")
    {
        CHECK(near(laser_pen(laser, 0).at, { 0, 0 }));
        CHECK(near(laser_pen(laser, 0.3f).at, { 8, 4 }));
        CHECK(laser_pen(laser, 0.3f).lit);
    }
    SUBCASE("a stroke the drawing does not light is dark all the way")
    {
        CHECK_FALSE(laser_pen(laser, 0.5f).lit);
        CHECK(near(laser_pen(laser, 0.5f).at, { 4, 8 }));
        CHECK(laser_pen(laser, 0.7f).lit);
    }
    SUBCASE("once traced it stays at the end, dark")
    {
        CHECK_FALSE(laser_pen(laser, 0.9f).lit);
        CHECK_FALSE(laser_pen(laser, 5).lit);
        CHECK(near(laser_pen(laser, 5).at, { 0, 0 }));
        CHECK_FALSE(laser_pen(laser, -1).lit);
    }
    SUBCASE("a second drawing starts where the first ended")
    {
        laser.drawings.push_back({ .seconds = 2, .points = { { .at = { 20, 20 } }, { .at = { 30, 20 }, .lit = true } } });
        CHECK(near(laser_pen(laser, 1.5f).at, { 5, 0 }));
        CHECK(laser_pen(laser, 1.5f).lit);
    }
    SUBCASE("centred, it is measured from the middle of all its drawings")
    {
        laser.centred = true;
        CHECK(near(laser_pen(laser, 0).at, { -4, -4 }));
        CHECK(near(laser_pen(laser, 0.3f).at, { 4, 0 }));
    }
}

TEST_CASE("a drawing laser's beam turns toward its pen by the drawing's scale")
{
    std::vector<glm::vec3> ways;
    const auto near = [](glm::vec3 a, glm::vec3 b) { return glm::distance(a, b) < 1e-5f; };
    // A stroke straight across, over the pad's width of 32, thrown 90 degrees wide.
    Laser laser { .seconds = 10, .look = LaserLook::Drawing, .drawing_scale = 90, .drawings = { { .seconds = 1, .points = { { .at = { 0, 5 } }, { .at = { 32, 5 }, .lit = true } } } } };
    SUBCASE("at the first point it shines up the tube")
    {
        laser_beams(laser, 0, ways);
        REQUIRE(ways.size() == 1);
        CHECK(near(ways[0], { 0, 1, 0 }));
    }
    SUBCASE("half way across it has turned 45 degrees toward x")
    {
        laser_beams(laser, 0.25f, ways);
        REQUIRE(ways.size() == 1);
        CHECK(near(ways[0], { std::sqrt(0.5f), std::sqrt(0.5f), 0 }));
    }
    SUBCASE("a pad down the page turns it toward z, scaled the same")
    {
        laser.drawings[0].points[1].at = { 0, 13 };
        laser_beams(laser, 0.25f, ways);
        REQUIRE(ways.size() == 1);
        CHECK(near(ways[0], { 0, std::cos(std::numbers::pi_v<f32> / 16), std::sin(std::numbers::pi_v<f32> / 16) }));
    }
    SUBCASE("with the pen lifted there is no beam")
    {
        laser_beams(laser, 3, ways);
        CHECK(ways.empty());
    }
}

TEST_CASE("a park's display reads the aims its launchers keep and its keys for them")
{
    const std::vector<std::byte> park = invented_aimed_show();
    const auto file = rct3::save::read_save(park, { .verify_checksum = false });
    REQUIRE(file.has_value());
    const auto displays = read_park_displays(*file);
    REQUIRE(displays.has_value());
    REQUIRE(displays->size() == 1);
    const auto& launchers = displays->front().launchers;
    REQUIRE(launchers.size() == 2);
    CHECK(launchers[0].aim.elevation.value == doctest::Approx(0.2));
    CHECK(launchers[0].aim.angle.value == doctest::Approx(0.5));
    REQUIRE(launchers[0].keys.size() == 2);
    CHECK(launchers[0].keys[0].seconds == doctest::Approx(2));
    CHECK(launchers[0].keys[1].aim.angle.value == doctest::Approx(3));
    CHECK(launchers[1].keys.empty());
}

TEST_CASE("a launcher's aim runs between its keys and holds before and after them")
{
    const Launcher kept { .aim = { .elevation = { 0.2f }, .angle = { 0.5f } } };
    Launcher keyed = kept;
    keyed.keys = { { .seconds = 2, .aim = { .elevation = { 0.5f }, .angle = { 1 } } }, { .seconds = 6, .aim = { .elevation = { 1.5f }, .angle = { 3 } } } };

    SUBCASE("without keys it keeps its own")
    {
        CHECK(aim_at(kept, 4).angle.value == doctest::Approx(0.5));
    }
    SUBCASE("before the first key it holds that key")
    {
        CHECK(aim_at(keyed, 0).elevation.value == doctest::Approx(0.5));
    }
    SUBCASE("between two keys straight from one to the other")
    {
        CHECK(aim_at(keyed, 4).elevation.value == doctest::Approx(1));
        CHECK(aim_at(keyed, 5).angle.value == doctest::Approx(2.5));
    }
    SUBCASE("after the last it holds that")
    {
        CHECK(aim_at(keyed, 60).angle.value == doctest::Approx(3));
    }
    SUBCASE("two keys at one time take the later")
    {
        keyed.keys.insert(keyed.keys.begin() + 1, AimKey { .seconds = 2, .aim = { .elevation = { 0.1f }, .angle = { 0 } } });
        CHECK(aim_at(keyed, 2).elevation.value == doctest::Approx(0.1));
    }
}

TEST_CASE("an aim tilts a tube off the way it stands, then turns it round")
{
    const auto near = [](glm::vec3 a, glm::vec3 b) { return glm::distance(a, b) < 1e-5f; };
    constexpr f32 kQuarter = std::numbers::pi_v<f32> / 2;

    SUBCASE("unaimed, a standing tube points up with its front +z")
    {
        const glm::mat3 frame = aimed({ 0, 1, 0 }, { });
        CHECK(near(frame[1], { 0, 1, 0 }));
        CHECK(near(frame[2], { 0, 0, 1 }));
        CHECK(near(frame[0], glm::cross(frame[1], frame[2])));
    }
    SUBCASE("a standing tube tilted a quarter turn lies level toward +z, and a quarter turn round takes it to +x")
    {
        CHECK(near(aimed({ 0, 1, 0 }, { .elevation = { kQuarter } })[1], { 0, 0, 1 }));
        CHECK(near(aimed({ 0, 1, 0 }, { .elevation = { kQuarter }, .angle = { kQuarter } })[1], { 1, 0, 0 }));
    }
    SUBCASE("a leaning tube tilts further the way it leans, and turns round the way it stood")
    {
        const glm::vec3 leaning = glm::normalize(glm::vec3(1, 1, 0));
        CHECK(near(aimed(leaning, { .elevation = { kQuarter / 2 } })[1], { 1, 0, 0 }));
        const glm::mat3 turned = aimed(leaning, { .elevation = { 0.3f }, .angle = { 2 } });
        CHECK(glm::dot(turned[1], leaning) == doctest::Approx(std::cos(0.3f)));
        CHECK(glm::abs(glm::determinant(turned)) == doctest::Approx(1));
    }
}
