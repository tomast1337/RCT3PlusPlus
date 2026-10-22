// SPDX-License-Identifier: GPL-3.0-or-later
#include "particles/simulation.hpp"

#include <algorithm>
#include <doctest/doctest.h>

using namespace rct3::particles;
using rct3::random::RandomStream;
using rct3::random::Seed;
using rct3::random::stream;

namespace
{

    /// One emitter sending `count` particles of `life` seconds evenly over a one-second effect.
    Effect steady(std::int32_t count, float life)
    {
        Effect e;
        e.roots = { 0 };
        e.emitters = { Emitter { .count = count, .particle = 0 } };
        e.particles = { Particle { .lifetime = life } };
        return e;
    }

    auto seeded()
    {
        return stream(Seed { 7 }, RandomStream::Effects);
    }

    /// `seconds` of time in frames of a tenth, as the park would call it.
    void run_for(EffectRun& run, float seconds)
    {
        for (int i = 0; i < static_cast<int>(seconds * 10 + 0.5f); ++i)
            run.advance(0.1f);
    }

} // namespace

TEST_CASE("an emitter spreads its particles over its share of the owner's life")
{
    const Effect e = steady(10, 5);
    EffectRun run(e, seeded(), false, 64);
    run.advance(0.2f);
    run.advance(0.2f);
    run.advance(0.1f);
    CHECK(run.particles().size() == 5);
    for (int i = 0; i < 10; ++i)
        run.advance(0.1f);
    CHECK(run.particles().size() == 10);
    CHECK_FALSE(run.finished());
    for (int i = 0; i < 40; ++i)
        run.advance(0.2f);
    CHECK(run.finished());
}

TEST_CASE("a looping run starts its effect again")
{
    const Effect e = steady(4, 0.5f);
    EffectRun run(e, seeded(), true, 64);
    std::size_t most = 0;
    for (int i = 0; i < 100; ++i)
    {
        run.advance(0.05f);
        most = std::max(most, run.particles().size());
    }
    CHECK_FALSE(run.finished());
    CHECK(most >= 2);
    CHECK(most <= 3);
}

TEST_CASE("the same seed gives the same particles")
{
    Effect e = steady(20, 2);
    e.emitters[0].speed_random = 3;
    e.emitters[0].position_random = 1;
    EffectRun a(e, seeded(), false, 64);
    EffectRun b(e, seeded(), false, 64);
    a.advance(0.25f);
    a.advance(0.25f);
    b.advance(0.25f);
    b.advance(0.25f);
    REQUIRE(a.particles().size() == b.particles().size());
    for (std::size_t i = 0; i < a.particles().size(); ++i)
        CHECK(a.particles()[i].position == b.particles()[i].position);
}

TEST_CASE("particles fall with gravity, head along the run's up and slow with drag")
{
    Effect e = steady(1, 5);
    e.emitters[0].end = 0; // All at once, at the start
    e.emitters[0].speed = 2;
    e.particles[0].gravity = 1;
    EffectRun run(e, seeded(), false, 8);
    run.place({ 10, 0, 0 }, glm::mat3(glm::vec3(0, 1, 0), glm::vec3(-1, 0, 0), glm::vec3(0, 0, 1))); // Up is -x
    run_for(run, 1);
    REQUIRE(run.particles().size() == 1);
    const LiveParticle& p = run.particles()[0];
    CHECK(p.velocity.x == doctest::Approx(-2));
    CHECK(p.velocity.y == doctest::Approx(-9.81).epsilon(0.02));
    CHECK(p.position.x == doctest::Approx(8).epsilon(0.02));

    e.particles[0].gravity = 0;
    e.particles[0].drag = 1;
    EffectRun slowed(e, seeded(), false, 8);
    run_for(slowed, 1);
    CHECK(glm::length(slowed.particles()[0].velocity) == doctest::Approx(2 * 0.3679).epsilon(0.02));
}

TEST_CASE("a particle's own emitters send theirs from where it is")
{
    Effect e = steady(1, 2);
    e.emitters[0].end = 0;
    e.emitters[0].speed = 1;
    e.emitters.push_back(Emitter { .start = 0.5f, .end = 0.5f, .count = 3, .particle = 1 });
    e.particles[0].emitters = { 1 };
    e.particles.push_back(Particle { .lifetime = 0.5f });
    EffectRun run(e, seeded(), false, 16);
    run_for(run, 1.1f);
    REQUIRE(run.particles().size() == 4);
    const auto born = std::ranges::count_if(run.particles(), [](const LiveParticle& p) { return p.kind == 1; });
    CHECK(born == 3);
    for (const LiveParticle& p : run.particles())
        CHECK(p.position.y == doctest::Approx(1).epsilon(0.05));
}

TEST_CASE("a full pool takes no more, and carried particles follow the run")
{
    Effect e = steady(50, 5);
    e.emitters[0].relative = true;
    EffectRun run(e, seeded(), false, 8);
    run.advance(0.25f);
    run.advance(0.25f);
    run.advance(0.25f);
    run.advance(0.25f);
    CHECK(run.particles().size() == 8);
    run.place({ 0, 0, 3 });
    for (const LiveParticle& p : run.particles())
        CHECK(p.position.z == doctest::Approx(3));
}

TEST_CASE("a particle's look follows its curves, size and the effect's tint")
{
    Effect e = steady(1, 2);
    e.tint = { 0.5f, 1, 1, 1 };
    Particle& kind = e.particles[0];
    kind.start_size = 1;
    kind.end_size = 3;
    kind.colour[3] = Curve { .times = { 0, 1 }, .values = { 1, 0 } };
    LiveParticle p { .age = 0.5f, .life = 2, .size = 0.5f };
    Look l = look(e, p);
    CHECK(l.size == doctest::Approx(0.75));
    CHECK(l.colour.a == doctest::Approx(0.75));
    CHECK(l.colour.r == doctest::Approx(1));
    kind.tinted = true;
    l = look(e, p);
    CHECK(l.colour.r == doctest::Approx(0.5));
}
