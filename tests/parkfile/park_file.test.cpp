// SPDX-License-Identifier: GPL-3.0-or-later
#include "parkfile/park_file.hpp"

#include "simulation/components.hpp"

#include <cstring>
#include <doctest/doctest.h>

using namespace rct3;
using simulation::SimWorld;

namespace
{
    SimWorld two_guests()
    {
        SimWorld world;
        world.tick = 1234;
        world.seed = 0xC0FF;
        auto& r = world.registry;
        const auto a = r.create();
        r.emplace<simulation::Visitor>(a, simulation::AgeGroup::Teen, true, "Ada Example", std::uint64_t { 7 });
        r.emplace<simulation::Pose>(a, glm::vec3 { 1.5f, 2.0f, -3.25f }, 0.5f);
        r.emplace<simulation::Needs>(a, 0.1f, 0.9f, 0.3f);
        r.emplace<simulation::Mood>(a, 0.8f, 0.6f, 0.0f);
        r.emplace<simulation::Wallet>(a, std::int64_t { 2550 });
        const auto gone = r.create();
        const auto b = r.create();
        r.destroy(gone); // A gap in the ids
        r.emplace<simulation::Visitor>(b, simulation::AgeGroup::Child, false, "", std::uint64_t { 0 });
        r.emplace<simulation::Needs>(b);
        return world;
    }

    std::vector<std::byte> header(std::uint32_t version, std::uint32_t chunks)
    {
        std::vector<std::byte> out(16);
        std::memcpy(out.data(), "R3PPPARK", 8);
        std::memcpy(out.data() + 8, &version, 4); // Tests run on little-endian machines
        std::memcpy(out.data() + 12, &chunks, 4);
        return out;
    }

    void chunk(std::vector<std::byte>& out, const char* tag, std::uint32_t version, std::vector<std::byte> payload)
    {
        const std::uint64_t length = payload.size();
        const std::size_t at = out.size();
        out.resize(at + 16);
        std::memcpy(out.data() + at, tag, 4);
        std::memcpy(out.data() + at + 4, &version, 4);
        std::memcpy(out.data() + at + 8, &length, 8);
        out.insert(out.end(), payload.begin(), payload.end());
    }
} // namespace

TEST_CASE("a park read back from its file is the park that was written")
{
    const SimWorld world = two_guests();
    const auto bytes = parkfile::write_park_file(world);
    const auto read = parkfile::read_park_file(bytes);
    REQUIRE_MESSAGE(read, read.error());
    CHECK(read->tick == 1234);
    CHECK(read->seed == 0xC0FF);
    const auto& r = read->registry;
    CHECK(r.view<entt::entity>().size() == 2);
    const auto a = static_cast<entt::entity>(0);
    const auto b = static_cast<entt::entity>(2);
    REQUIRE(r.valid(a));
    REQUIRE(r.valid(b));
    CHECK_FALSE(r.valid(static_cast<entt::entity>(1)));
    const auto& v = r.get<simulation::Visitor>(a);
    CHECK(v.age == simulation::AgeGroup::Teen);
    CHECK(v.female);
    CHECK(v.name == "Ada Example");
    CHECK(v.arrived == 7);
    CHECK(r.get<simulation::Pose>(a).position == glm::vec3 { 1.5f, 2.0f, -3.25f });
    CHECK(r.get<simulation::Needs>(a).thirst == 0.9f);
    CHECK(r.get<simulation::Mood>(a).happiness == 0.8f);
    CHECK(r.get<simulation::Wallet>(a).cash == 2550);
    CHECK(r.all_of<simulation::Visitor, simulation::Needs>(b));
    CHECK_FALSE(r.any_of<simulation::Pose, simulation::Mood, simulation::Wallet>(b));
}

TEST_CASE("a ride's departure settings are kept, and a load the format does not know is refused")
{
    SimWorld world;
    const auto ride = world.registry.create();
    world.registry.emplace<simulation::Departure>(ride, simulation::Departure { .wait_for = simulation::LoadWait::Half, .min_wait = 10, .max_wait = -1 });
    auto bytes = parkfile::write_park_file(world);
    const auto read = parkfile::read_park_file(bytes);
    REQUIRE_MESSAGE(read, read.error());
    const auto& d = read->registry.get<simulation::Departure>(ride);
    CHECK(d.wait_for == simulation::LoadWait::Half);
    CHECK(d.min_wait == 10);
    CHECK(d.max_wait == -1);
    CHECK(parkfile::write_park_file(*read) == bytes);
    // The load is the byte after the record's entity, 9 from the end.
    bytes[bytes.size() - 9] = std::byte { 5 };
    CHECK_FALSE(parkfile::read_park_file(bytes));
}

TEST_CASE("writing a park that was read gives the same bytes")
{
    const auto first = parkfile::write_park_file(two_guests());
    const auto read = parkfile::read_park_file(first);
    REQUIRE(read);
    CHECK(parkfile::write_park_file(*read) == first);
}

TEST_CASE("an empty park has a file too")
{
    const auto read = parkfile::read_park_file(parkfile::write_park_file(SimWorld { }));
    REQUIRE(read);
    CHECK(read->registry.view<entt::entity>().empty());
}

TEST_CASE("chunks of a newer build are skipped, but newer versions of known ones are refused")
{
    auto bytes = header(1, 3);
    chunk(bytes, "WRLD", 1, std::vector<std::byte>(16, std::byte { 0 }));
    chunk(bytes, "ENTS", 1, std::vector<std::byte>(4, std::byte { 0 }));
    chunk(bytes, "ZZZZ", 9, std::vector<std::byte>(5, std::byte { 7 }));
    CHECK(parkfile::read_park_file(bytes));

    auto newer = header(1, 1);
    chunk(newer, "WRLD", 2, std::vector<std::byte>(16, std::byte { 0 }));
    const auto refused = parkfile::read_park_file(newer);
    REQUIRE_FALSE(refused);
    CHECK(refused.error().contains("version 2"));
}

TEST_CASE("a file from a newer format is refused")
{
    const auto read = parkfile::read_park_file(header(parkfile::kParkFileVersion + 1, 0));
    REQUIRE_FALSE(read);
    CHECK(read.error().contains("newer"));
}

TEST_CASE("broken files are refused, not trusted")
{
    const auto good = parkfile::write_park_file(two_guests());

    SUBCASE("every cut is refused")
    {
        for (std::size_t n = 0; n < good.size(); ++n)
            CHECK_FALSE(parkfile::read_park_file(std::span(good).first(n)));
    }
    SUBCASE("trailing bytes")
    {
        auto longer = good;
        longer.push_back(std::byte { 0 });
        CHECK_FALSE(parkfile::read_park_file(longer));
    }
    SUBCASE("a component of an entity that is not in the park")
    {
        auto bytes = header(1, 2);
        std::vector<std::byte> one(4, std::byte { 0 });
        one[0] = std::byte { 1 }; // count 1
        chunk(bytes, "ENTS", 1, { std::byte { 0 }, std::byte { 0 }, std::byte { 0 }, std::byte { 0 } });
        std::vector<std::byte> wallet(4 + 4 + 8, std::byte { 0 });
        wallet[0] = std::byte { 1 }; // one record
        wallet[4] = std::byte { 5 }; // for entity 5
        chunk(bytes, "WALT", 1, wallet);
        const auto read = parkfile::read_park_file(bytes);
        REQUIRE_FALSE(read);
        CHECK(read.error().contains("entity 5"));
    }
    SUBCASE("a count larger than the file")
    {
        auto bytes = header(1, 1);
        chunk(bytes, "ENTS", 1, { std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0xFF }, std::byte { 0x7F } });
        CHECK_FALSE(parkfile::read_park_file(bytes));
    }
    SUBCASE("not a park file")
    {
        CHECK_FALSE(parkfile::read_park_file(std::vector<std::byte>(32, std::byte { 'x' })));
    }
}

TEST_CASE("a ride mid-breakdown and a staff member's patrol are read back as written")
{
    SimWorld world;
    auto& r = world.registry;
    const auto ride = r.create();
    simulation::Upkeep upkeep { .reliability = 0.4f, .broken = true, .since_inspection = 90, .inspect_every = -1, .breakdowns = 3, .kind = simulation::Breakdown::DoorsFailToOpen, .last = simulation::Breakdown::DoorsFailToOpen };
    upkeep.downtime = { 20, 4, 0, 0, 0, 0, 0, 7 };
    upkeep.samples = 11;
    upkeep.sample_clock = 0.75f;
    r.emplace<simulation::Upkeep>(ride, upkeep);
    const auto handyman = r.create();
    r.emplace<simulation::Patrol>(handyman, simulation::Patrol { { { 3, 4 }, { -1, 200 } }, 2 });
    const auto bytes = parkfile::write_park_file(world);
    const auto read = parkfile::read_park_file(bytes);
    REQUIRE_MESSAGE(read, read.error());
    const auto& back = read->registry.get<simulation::Upkeep>(ride);
    CHECK(back.broken);
    CHECK(back.kind == simulation::Breakdown::DoorsFailToOpen);
    CHECK(back.reliability == 0.4f);
    CHECK(back.downtime == upkeep.downtime);
    CHECK(back.downtime_share() == doctest::Approx(31.0 / 160));
    CHECK(back.samples == 11);
    CHECK(read->registry.get<simulation::Patrol>(handyman).tiles == std::vector<glm::ivec2> { { 3, 4 }, { -1, 200 } });
    CHECK(read->registry.get<simulation::Patrol>(handyman).zone == 2);
    CHECK(parkfile::write_park_file(*read) == bytes);
}
