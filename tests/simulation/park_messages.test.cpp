// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/park_messages.hpp"

#include <doctest/doctest.h>

using namespace rct3;

namespace
{
    simulation::ParkMessage note(std::string key, float age = 0)
    {
        simulation::ParkMessage m;
        m.text_key = std::move(key);
        m.age = age;
        return m;
    }
} // namespace

TEST_CASE("a park's messages start from its save, oldest first")
{
    save::MessageLog log;
    save::Message older;
    older.text_key = "Invented_Thing:txt";
    older.time = 300;
    older.object = 77;
    older.object_name = "Big Wheel";
    older.panel = 8;
    save::Message newer;
    newer.text_key = "Won_Prize";
    newer.time = 20;
    newer.award = 4;
    log.ordered = { older, newer };

    const auto messages = simulation::park_messages(log);
    REQUIRE(messages.list.size() == 2);
    CHECK(messages.list[0].text_key == "Invented_Thing");
    CHECK(messages.list[0].age == 300);
    CHECK(messages.list[0].subject == 77u);
    CHECK(messages.list[0].subject_name == "Big Wheel");
    CHECK(messages.list[0].panel == 8);
    CHECK(messages.list[1].text_key == "Won_Prize");
    CHECK(messages.list[1].award == 4);
    CHECK(messages.list[1].serial > messages.list[0].serial);
    CHECK(messages.last_serial == messages.list[1].serial);
}

TEST_CASE("the list keeps the newest messages and lets old ones go")
{
    simulation::ParkMessages messages;
    for (std::size_t i = 0; i < simulation::kMessageCapacity + 3; ++i)
        simulation::post(messages, note("Note", static_cast<float>(i)));
    REQUIRE(messages.list.size() == simulation::kMessageCapacity);
    CHECK(messages.list.front().serial == 4);
    CHECK(messages.list.back().serial == simulation::kMessageCapacity + 3);

    simulation::ParkMessages few;
    simulation::post(few, note("Old", simulation::kMessageLifetime - 10));
    simulation::post(few, note("New"));
    simulation::age(few, 5);
    CHECK(few.list.size() == 2);
    CHECK(few.list.back().age == 5);
    simulation::age(few, 6);
    REQUIRE(few.list.size() == 1);
    CHECK(few.list.front().text_key == "New");
}
