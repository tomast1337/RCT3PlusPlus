// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/effect.hpp"

#include "particles/display.hpp"

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
        Bytes& put(const void* p, std::size_t n)
        {
            const auto* b = static_cast<const std::byte*>(p);
            v.insert(v.end(), b, b + n);
            return *this;
        }
        Bytes& u8(std::uint8_t x)
        {
            return put(&x, 1);
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
        Bytes& name(const std::string& s)
        {
            u16(static_cast<std::uint16_t>(s.size()));
            return put(s.data(), s.size());
        }
        /// A variable-size field: its length, then its bytes.
        Bytes& field(const Bytes& b)
        {
            u32(static_cast<std::uint32_t>(b.v.size()));
            v.insert(v.end(), b.v.begin(), b.v.end());
            return *this;
        }
    };

    struct Attr
    {
        std::string name, type;
        std::uint32_t size = 0;
        std::vector<Attr> children { };
    };

    void write_attr(Bytes& b, const Attr& a)
    {
        b.name(a.name).name(a.type).u32(a.size).u32(static_cast<std::uint32_t>(a.children.size()));
        for (const Attr& c : a.children)
            write_attr(b, c);
    }

    Attr refs(const std::string& n)
    {
        return { n, "array", 0, { { n + "Element", "reference", 8 } } };
    }

    Bytes ref_list(const std::vector<std::uint64_t>& ids)
    {
        Bytes b;
        b.u32(static_cast<std::uint32_t>(ids.size()));
        for (const std::uint64_t id : ids)
            b.u64(id);
        return b;
    }

    Bytes float_list(const std::vector<float>& values)
    {
        Bytes b;
        b.u32(static_cast<std::uint32_t>(values.size()));
        for (const float f : values)
            b.f32(f);
        return b;
    }

    enum : std::uint32_t
    {
        kEffect,
        kEmitter,
        kParticle,
    };

    /// An effect (id 1) with two emitters (2, 3) sharing one particle (4), which has an emitter of its own (5)
    /// emitting the same particle again. The second emitter emits `second_emits`.
    std::vector<std::byte> invented_effect(std::uint64_t second_emits = 4)
    {
        const Attr opacity { "ColourA", "struct", 0, { { "ColourATimes", "array", 0, { { "ColourATimesElement", "float32", 4 } } }, { "ColourAValues", "array", 0, { { "ColourAValuesElement", "float32", 4 } } }, { "wrap", "bool", 1 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "ParticleBaseDesc", { { "Lifetime", "float32", 4 }, refs("ParticleEmitterDescs"), { "SoundName", "string", 0 } } },
            { "ParticleEmitterDesc", { { "NumParticles", "int32", 4 }, { "ParticleDesc", "reference", 8 }, { "StartTime", "float32", 4 } } },
            { "ParticleDesc", { opacity, { "Lifetime", "float32", 4 }, refs("ParticleEmitterDescs"), { "TextureNameReduced", "string", 0 } } },
        };
        Bytes b;
        b.u32(static_cast<std::uint32_t>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).u32(static_cast<std::uint32_t>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        Bytes sound;
        sound.put("Crackle", 7);
        Bytes skin;
        skin.put("Glow", 4);
        Bytes curve;
        curve.field(float_list({ 0.0f, 0.5f })).field(float_list({ 0.0f, 1.0f })).u8(0);
        b.u32(5);
        b.u32(kEffect).u64(1).f32(2.5f).field(ref_list({ 2, 3 })).field(sound);
        b.u32(kEmitter).u64(2).u32(10).u64(4).f32(0.0f);
        b.u32(kEmitter).u64(3).u32(20).u64(second_emits).f32(0.5f);
        b.u32(kParticle).u64(4).field(curve).f32(0.75f).field(ref_list({ 5 })).field(skin);
        b.u32(kEmitter).u64(5).u32(1).u64(4).f32(0.25f);
        return b.v;
    }

    Bytes text(const std::string& s)
    {
        Bytes b;
        return b.put(s.data(), s.size());
    }

    /// A display (id 1) with two launchers, the second named by an item object (id 2), and three shots out of
    /// time order. The last shot fires from launcher `last_launcher`.
    std::vector<std::byte> invented_display(std::uint32_t last_launcher = 1)
    {
        const Attr launcher { "SceneryEntriesElement", "struct", 20, { { "Col", "int32", 4 }, { "Row", "int32", 4 }, { "Dir", "int32", 4 }, { "SID", "reference", 8 } } };
        const Attr shot { "EntriesElement", "struct", 0, { { "Name", "string", 0 }, { "SceneryEntry", "int32", 4 }, { "SlotIndex", "int32", 4 }, { "Time", "float32", 4 }, { "Track", "float32", 4 } } };
        const std::vector<std::pair<std::string, std::vector<Attr>>> classes {
            { "SavedFireworkDisplay", { { "Name", "string", 0 }, { "MonthMask", "int32", 4 }, { "SceneryEntries", "array", 0, { launcher } }, { "Entries", "array", 0, { shot } } } },
            { "SIDDatabaseEntry", { { "SYMBOLNAME", "string", 0 } } },
        };
        Bytes b;
        b.u32(static_cast<std::uint32_t>(classes.size()));
        for (const auto& [name, attrs] : classes)
        {
            b.name(name).u32(static_cast<std::uint32_t>(attrs.size()));
            for (const Attr& a : attrs)
                write_attr(b, a);
        }
        Bytes launchers;
        launchers.u32(2);
        launchers.u32(3).u32(4).u32(0).u64(0);
        launchers.u32(5).u32(4).u32(1).u64(2);
        // The lane is a whole number in a float-typed field.
        const auto put_shot = [](Bytes& shots, const std::string& effect, std::uint32_t from, std::uint32_t tube, float seconds, std::int32_t lane) {
            Bytes one;
            one.field(text(effect)).u32(from).u32(tube).f32(seconds).f32(std::bit_cast<float>(lane));
            shots.field(one); // An element with a string in it is variable in size: it carries its length
        };
        Bytes shots;
        shots.u32(3);
        put_shot(shots, "Fireworks\\Peony.frw", 0, 2, 4.5f, 1);
        put_shot(shots, "Fireworks\\Comet.frw", 1, 0, 0.5f, 0);
        put_shot(shots, "Willow", last_launcher, 7, 2.0f, 3);
        b.u32(2);
        b.u32(0).u64(1).field(text("Night Sky")).u32(0x0f0).field(launchers).field(shots);
        b.u32(1).u64(2).field(text("Mortar"));
        return b.v;
    }

} // namespace

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
