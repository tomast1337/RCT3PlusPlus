// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/effect.hpp"

#include "core/types.hpp"
#include "particles/display.hpp"
#include "save/save_file.hpp"

#include <bit>
#include <cstdint>
#include <doctest/doctest.h>
#include <string>
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

    Attr refs(const std::string& n)
    {
        return { n, "array", 0, { { n + "Element", "reference", 8 } } };
    }

    Bytes ref_list(const std::vector<u64>& ids)
    {
        Bytes b;
        b.put_u32(static_cast<u32>(ids.size()));
        for (const u64 id : ids)
            b.put_u64(id);
        return b;
    }

    Bytes float_list(const std::vector<f32>& values)
    {
        Bytes b;
        b.put_u32(static_cast<u32>(values.size()));
        for (const f32 f : values)
            b.put_f32(f);
        return b;
    }

    enum : u32
    {
        kEffect,
        kEmitter,
        kParticle,
    };

    /// An effect (id 1) with two emitters (2, 3) sharing one particle (4), which has an emitter of its own (5)
    /// emitting the same particle again. The second emitter emits `second_emits`.
    std::vector<std::byte> invented_effect(u64 second_emits = 4)
    {
        const Attr opacity { "ColourA", "struct", 0, { { "ColourATimes", "array", 0, { { "ColourATimesElement", "float32", 4 } } }, { "ColourAValues", "array", 0, { { "ColourAValuesElement", "float32", 4 } } }, { "wrap", "bool", 1 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "ParticleBaseDesc", { { "Lifetime", "float32", 4 }, refs("ParticleEmitterDescs"), { "SoundName", "string", 0 } } },
            { "ParticleEmitterDesc", { { "NumParticles", "int32", 4 }, { "ParticleDesc", "reference", 8 }, { "StartTime", "float32", 4 } } },
            { "ParticleDesc", { opacity, { "Lifetime", "float32", 4 }, refs("ParticleEmitterDescs"), { "TextureNameReduced", "string", 0 } } },
        };
        Bytes b;
        b.put_u32(static_cast<u32>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).put_u32(static_cast<u32>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        Bytes sound;
        sound.put("Crackle", 7);
        Bytes skin;
        skin.put("Glow", 4);
        Bytes curve;
        curve.field(float_list({ 0.0f, 0.5f })).field(float_list({ 0.0f, 1.0f })).put_u8(0);
        b.put_u32(5);
        b.put_u32(kEffect).put_u64(1).put_f32(2.5f).field(ref_list({ 2, 3 })).field(sound);
        b.put_u32(kEmitter).put_u64(2).put_u32(10).put_u64(4).put_f32(0.0f);
        b.put_u32(kEmitter).put_u64(3).put_u32(20).put_u64(second_emits).put_f32(0.5f);
        b.put_u32(kParticle).put_u64(4).field(curve).put_f32(0.75f).field(ref_list({ 5 })).field(skin);
        b.put_u32(kEmitter).put_u64(5).put_u32(1).put_u64(4).put_f32(0.25f);
        return b.v;
    }

    Bytes text(const std::string& s)
    {
        Bytes b;
        return b.put(s.data(), s.size());
    }

    /// A display (id 1) with two launchers, the second named by an item object (id 2), and three shots out of
    /// time order. The last shot fires from launcher `last_launcher`.
    std::vector<std::byte> invented_display(u32 last_launcher = 1)
    {
        const Attr launcher { "SceneryEntriesElement", "struct", 20, { { "Col", "int32", 4 }, { "Row", "int32", 4 }, { "Dir", "int32", 4 }, { "SID", "reference", 8 } } };
        const Attr shot { "EntriesElement", "struct", 0, { { "Name", "string", 0 }, { "SceneryEntry", "int32", 4 }, { "SlotIndex", "int32", 4 }, { "Time", "float32", 4 }, { "Track", "float32", 4 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "SavedFireworkDisplay", { { "Name", "string", 0 }, { "MonthMask", "int32", 4 }, { "SceneryEntries", "array", 0, { launcher } }, { "Entries", "array", 0, { shot } } } },
            { "SIDDatabaseEntry", { { "SYMBOLNAME", "string", 0 } } },
        };
        Bytes b;
        b.put_u32(static_cast<u32>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).put_u32(static_cast<u32>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        Bytes launchers;
        launchers.put_u32(2);
        launchers.put_u32(3).put_u32(4).put_u32(0).put_u64(0);
        launchers.put_u32(5).put_u32(4).put_u32(1).put_u64(2);
        // The lane is a whole number in a float-typed field.
        const auto put_shot = [](Bytes& shots, const std::string& effect, u32 from, u32 tube, f32 seconds, i32 lane) {
            Bytes one;
            one.field(text(effect)).put_u32(from).put_u32(tube).put_f32(seconds).put_f32(std::bit_cast<f32>(lane));
            shots.field(one); // An element with a string in it is variable in size: it carries its length
        };
        Bytes shots;
        shots.put_u32(3);
        put_shot(shots, "Fireworks\\Peony.frw", 0, 2, 4.5f, 1);
        put_shot(shots, "Fireworks\\Comet.frw", 1, 0, 0.5f, 0);
        put_shot(shots, "Willow", last_launcher, 7, 2.0f, 3);
        b.put_u32(2);
        b.put_u32(0).put_u64(1).field(text("Night Sky")).put_u32(0x0f0).field(launchers).field(shots);
        b.put_u32(1).put_u64(2).field(text("Mortar"));
        return b.v;
    }

    /// A park (no other objects) scheduling one display at 21:00 in the third and fourth months, whose three shots
    /// name two placed launchers, ids 40 and 41, the first twice; and a show ride's own display, without shots.
    std::vector<std::byte> invented_park()
    {
        const Attr shot { "EntriesElement", "struct", 0, { { "Name", "string", 0 }, { "SceneryItemRef", "reference", 8 }, { "SlotIndex", "int32", 4 }, { "Time", "float32", 4 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "FireworkDisplay", { { "Name", "string", 0 }, { "MusicSelection", "string", 0 }, { "MonthMask", "int32", 4 }, { "StartTimeMinutes", "int32", 4 }, { "Entries", "array", 0, { shot } }, { "ShowController", "reference", 8 } } },
            { "ShowController", { { "Display", "reference", 8 } } },
        };
        Bytes b;
        b.put_u32(static_cast<u32>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).put_u32(static_cast<u32>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        const auto put_shot = [](Bytes& shots, const std::string& effect, u64 launcher, u32 tube, f32 seconds) {
            Bytes one;
            one.field(text(effect)).put_u64(launcher).put_u32(tube).put_f32(seconds);
            shots.field(one);
        };
        Bytes shots;
        shots.put_u32(3);
        put_shot(shots, "Fireworks\\Peony.frw", 41, 3, 2.0f);
        put_shot(shots, "Fireworks\\Comet.frw", 40, 0, 0.5f);
        put_shot(shots, "Fireworks\\Willow.frw", 40, 1, 1.0f);
        Bytes none;
        none.put_u32(0);
        b.put_u32(3);
        // The park's own names a show controller the park no longer has.
        b.put_u32(0).put_u64(1).field(text("Evening Show")).field(text("Tune.wma")).put_u32(0b1100).put_u32(21 * 60).field(shots).put_u64(7);
        b.put_u32(0).put_u64(2).field(text("Whale Show")).field(text("")).put_u32(0xffff).put_u32(22 * 60).field(none).put_u64(9);
        b.put_u32(1).put_u64(9).put_u64(2);
        return b.v;
    }

    /// A park's fountain show: two water jet shots (at 1 and 2 seconds) from one jet source and a firework (at 0.5).
    /// The jets' pattern lasts 10 seconds; its pressure is a blender with two points in use (12 at 5 of 10, 16 at
    /// 6) and a third past its count, and it has no elevation blender.
    std::vector<std::byte> invented_fountain_show()
    {
        const Attr shot { "EntriesElement", "struct", 0, { { "EffectType", "int32", 4 }, { "Name", "string", 0 }, { "ParticleSourceRef", "reference", 8 }, { "SceneryItemRef", "reference", 8 }, { "Time", "float32", 4 } } };
        const Attr point { "FixedPtsElement", "struct", 12, { { "BlenderElement", "reference", 8 }, { "PointTime", "float32", 4 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "FireworkDisplay", { { "Name", "string", 0 }, { "MonthMask", "int32", 4 }, { "StartTimeMinutes", "int32", 4 }, { "Entries", "array", 0, { shot } } } },
            { "WaterJetSource", { { "WaterJetBaseDesc", "reference", 8 } } },
            { "WaterJetBaseDesc", { { "Duration", "float32", 4 }, { "PressureBlender", "reference", 8 }, { "ElevationBlender", "reference", 8 } } },
            { "Blender", { { "MaxTime", "float32", 4 }, { "NumPts", "int32", 4 }, { "FixedPts", "array", 0, { point } } } },
            { "FloatBlenderElement", { { "Float", "float32", 4 } } },
        };
        Bytes b;
        b.put_u32(static_cast<u32>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).put_u32(static_cast<u32>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        const auto put_shot = [](Bytes& shots, u32 kind, const std::string& effect, u64 source, u64 launcher, f32 seconds) {
            Bytes one;
            one.put_u32(kind).field(text(effect)).put_u64(source).put_u64(launcher).put_f32(seconds);
            shots.field(one);
        };
        Bytes shots;
        shots.put_u32(3);
        put_shot(shots, 2, "Jets\\Spray.wjt", 2, 40, 2.0f);
        put_shot(shots, 0, "Fireworks\\Comet.frw", 0, 41, 0.5f);
        put_shot(shots, 2, "Jets\\Spray.wjt", 2, 40, 1.0f);
        Bytes points;
        points.put_u32(3);
        points.put_u64(5).put_f32(5.0f);
        points.put_u64(6).put_f32(6.0f);
        points.put_u64(5).put_f32(-1.0f); // Past the blender's count: not one of its points
        b.put_u32(6);
        b.put_u32(0).put_u64(1).field(text("Fountains")).put_u32(0xffff).put_u32(20 * 60).field(shots);
        b.put_u32(1).put_u64(2).put_u64(3);
        b.put_u32(2).put_u64(3).put_f32(10.0f).put_u64(4).put_u64(0);
        b.put_u32(3).put_u64(4).put_f32(10.0f).put_u32(2).field(points);
        b.put_u32(4).put_u64(5).put_f32(12.0f);
        b.put_u32(4).put_u64(6).put_f32(16.0f);
        return b.v;
    }

} // namespace

TEST_CASE("a fountain show's water jets carry their patterns, and the show is mostly water")
{
    const std::vector<std::byte> park = invented_fountain_show();
    const auto file = rct3::save::read_save(park, { .verify_checksum = false });
    REQUIRE(file.has_value());
    const auto displays = read_park_displays(*file);
    REQUIRE(displays.has_value());
    REQUIRE(displays->size() == 1);
    const Display& d = displays->front();
    REQUIRE(d.shots.size() == 3);
    CHECK(d.shots[0].kind == ShotKind::Firework);
    CHECK_FALSE(d.shots[0].jet.has_value());
    CHECK(d.shots[1].kind == ShotKind::WaterJet);
    CHECK(d.shots[1].effect == "Spray");
    REQUIRE(d.shots[1].jet.has_value());
    const JetPattern& jet = *d.shots[1].jet;
    CHECK(jet.seconds == doctest::Approx(10));
    CHECK(jet.pressure.times.size() == 2);
    CHECK(jet.pressure.at(0.0f) == doctest::Approx(12));
    CHECK(jet.pressure.at(0.55f) == doctest::Approx(14));
    CHECK(jet.pressure.at(1.0f) == doctest::Approx(16));
    CHECK(jet.elevation.values.empty());
    CHECK(main_kind(d) == ShotKind::WaterJet);
    CHECK(display_seconds(d) == doctest::Approx(12)); // The last jet shot at 2 runs its 10 seconds
}

TEST_CASE("a display with no shots is a firework display that ends at once")
{
    CHECK(main_kind(Display { }) == ShotKind::Firework);
    CHECK(display_seconds(Display { }) == doctest::Approx(0));
}

TEST_CASE("a firework display reads its launchers and its shots, earliest first")
{
    const auto display = read_display(invented_display());
    REQUIRE(display.has_value());
    CHECK(display->name == "Night Sky");
    CHECK(display->months == 0x0f0);
    REQUIRE(display->launchers.size() == 2);
    CHECK(display->launchers[0].item.empty());
    CHECK(display->launchers[1].item == "Mortar");
    CHECK(display->launchers[1].col == 5);
    CHECK(display->launchers[1].direction == 1);
    REQUIRE(display->shots.size() == 3);
    CHECK(display->shots[0].effect == "Comet");
    CHECK(display->shots[1].effect == "Willow");
    CHECK(display->shots[1].lane == 3);
    CHECK(display->shots[2].effect == "Peony");
    CHECK(display->shots[2].seconds == doctest::Approx(4.5));
    CHECK(display->shots[2].tube == 2);
}

TEST_CASE("a display shot from a launcher it does not have is refused")
{
    CHECK(read_display(invented_display(2)).error() == DisplayError::BadLauncher);
    CHECK(read_display(invented_effect()).error() == DisplayError::NoDisplay);
}

TEST_CASE("a curve runs straight between its points, holding its ends")
{
    const Curve c { .times = { 0.25f, 0.75f }, .values = { 0.0f, 1.0f } };
    CHECK(c.at(0.0f) == doctest::Approx(0));
    CHECK(c.at(0.5f) == doctest::Approx(0.5));
    CHECK(c.at(1.0f) == doctest::Approx(1));
    CHECK(Curve { }.at(0.3f) == doctest::Approx(1));
}

TEST_CASE("a wrapping curve runs on from its last point back to its first")
{
    const Curve c { .times = { 0.25f, 0.75f }, .values = { 0.0f, 1.0f }, .wrap = true };
    CHECK(c.at(1.0f) == doctest::Approx(0.5));
    CHECK(c.at(0.0f) == doctest::Approx(0.5));
    CHECK(c.at(0.25f) == doctest::Approx(0));
}

TEST_CASE("an effect reads with shared and circular references each read once")
{
    const auto effect = read_effect(invented_effect());
    REQUIRE(effect.has_value());
    CHECK(effect->lifetime == doctest::Approx(2.5));
    CHECK(effect->sound == "Crackle");
    REQUIRE(effect->roots.size() == 2);
    REQUIRE(effect->emitters.size() == 3);
    REQUIRE(effect->particles.size() == 1);
    const Emitter& second = effect->emitters[effect->roots[1]];
    CHECK(second.count == 20);
    CHECK(second.start == doctest::Approx(0.5));
    CHECK(second.end == doctest::Approx(1)); // Missing from the file: its default
    REQUIRE(second.particle);
    const Particle& p = effect->particles[*second.particle];
    CHECK(p.skin == "Glow");
    CHECK(p.lifetime == doctest::Approx(0.75));
    CHECK(p.colour[3].at(0.25f) == doctest::Approx(0.5));
    CHECK(p.colour[0].at(0.25f) == doctest::Approx(1));
    REQUIRE(p.emitters.size() == 1);
    CHECK(effect->emitters[p.emitters[0]].particle == second.particle);
}

TEST_CASE("files without an effect, or with a reference to the wrong kind, are refused")
{
    const std::vector<std::byte> data = invented_effect();
    CHECK(read_effect(std::span(data).first(8)).error() == EffectError::Container);
    CHECK(read_effect(invented_effect(1)).error() == EffectError::Unreferable); // The effect itself as a particle
}

TEST_CASE("a park's display fires from the launchers placed in the park; a show ride's own is not one")
{
    const std::vector<std::byte> park = invented_park(); // The file reads in place
    const auto file = rct3::save::read_save(park, { .verify_checksum = false });
    REQUIRE(file.has_value());
    const auto displays = read_park_displays(*file);
    REQUIRE(displays.has_value());
    REQUIRE(displays->size() == 1);
    const Display& d = displays->front();
    CHECK(d.name == "Evening Show");
    CHECK(d.music_file == "Tune.wma");
    CHECK(d.start_minutes == 21 * 60);
    REQUIRE(d.launchers.size() == 2);
    CHECK(d.launchers[0].placed == 41u);
    CHECK(d.launchers[1].placed == 40u);
    REQUIRE(d.shots.size() == 3);
    CHECK(d.shots[0].effect == "Comet");
    CHECK(d.shots[0].launcher == 1);
    CHECK(d.shots[1].launcher == 1);
    CHECK(d.shots[2].effect == "Peony");
    CHECK(d.shots[2].launcher == 0);
    CHECK(d.shots[2].tube == 3);
}

TEST_CASE("a display is due on its months only, from its start time for a short while")
{
    const Display d { .months = 0b1100, .start_minutes = 21 * 60 };
    CHECK_FALSE(display_due(d, 2, 20 * 60 + 59));
    CHECK(display_due(d, 2, 21 * 60));
    CHECK(display_due(d, 3, 21 * 60 + kStartWindowMinutes - 1));
    CHECK_FALSE(display_due(d, 3, 21 * 60 + kStartWindowMinutes));
    SUBCASE("not in a month it does not run in")
    {
        CHECK_FALSE(display_due(d, 1, 21 * 60 + 5));
        CHECK_FALSE(display_due(d, 4, 21 * 60 + 5));
        CHECK_FALSE(display_due(d, -1, 21 * 60 + 5));
    }
}

TEST_CASE("a display's shots go off once each, as its time reaches theirs, from their launcher's tube")
{
    const Display d { .launchers = { { }, { } }, .shots = { { .effect = "a", .launcher = 1, .tube = 4, .seconds = 0 }, { .effect = "b", .launcher = 0, .tube = 2, .seconds = 1 }, { .effect = "c", .launcher = 1, .tube = 0, .seconds = 1 }, { .effect = "d", .seconds = 3 } } };
    DisplayRun run;
    const auto first = advance_display(run, d, 0.5f);
    REQUIRE(first.size() == 1);
    CHECK(first[0].effect == "a");
    CHECK(first[0].launcher == 1);
    CHECK(first[0].tube == 4);
    CHECK(advance_display(run, d, 0.25f).empty());
    const auto both = advance_display(run, d, 0.25f);
    REQUIRE(both.size() == 2);
    CHECK(both[0].launcher == 0);
    CHECK(both[0].tube == 2);
    CHECK(both[1].effect == "c");
    CHECK(both[1].launcher == 1);
    // A long step fires all it passes.
    CHECK(advance_display(run, d, 10).size() == 1);
    CHECK(advance_display(run, d, 1).empty());
    CHECK(run.next_shot == d.shots.size());
}
