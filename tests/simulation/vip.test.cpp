// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/vip.hpp"

#include "simulation/components.hpp"
#include "simulation/crash.hpp"
#include "simulation/fireworks.hpp"
#include "simulation/guests.hpp"
#include "simulation/objectives.hpp"
#include "simulation/parties.hpp"
#include "simulation/rides.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"

#include <algorithm>
#include <cstdint>
#include <doctest/doctest.h>
#include <ranges>
#include <string>
#include <utility>

using namespace rct3;
using namespace rct3::simulation;
using namespace rct3::simulation::park;

namespace
{
    constexpr float kMonth = save::model::GameClock::kUnitsPerMonth;
    using T = save::model::VipTarget;

    save::model::ObjectiveSet one_goal(T target)
    {
        save::model::ObjectiveSet set;
        set.objectives = { { .difficulty = 0 } };
        set.conditions = { { .kind = save::model::ConditionKind::Vip, .id = 7, .vip = target } };
        return set;
    }

    std::vector<std::string> keys(const ParkMessages& m)
    {
        std::vector<std::string> out;
        for (const ParkMessage& x : m.list)
            out.push_back(x.text_key);
        return out;
    }

    /// A short path with a gate at one end; the gate is given its dice by a first step.
    Sandbox gated()
    {
        Sandbox park;
        park.path({ 0, 0 }, { 4, 0 });
        auto& r = park.world().registry;
        r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
        park.run(1);
        return park;
    }

    std::optional<entt::entity> the_vip(entt::registry& r)
    {
        const auto v = r.view<Vip>();
        return v.begin() != v.end() ? std::optional(*v.begin()) : std::nullopt;
    }
} // namespace

TEST_CASE("the player is told a month and a week ahead, then the VIP comes in at the gate by name")
{
    Sandbox park = gated();
    auto set = one_goal({ .who = 2, .next_visit = 10 * kMonth, .months_to_stay = 1 });
    ParkMessages messages;
    const VipSurroundings around { .name = [](std::uint8_t who) { return who == 2 ? std::string("Kara") : std::string(); } };
    advance_vips(park.world(), set, 8 * kMonth, 1, messages, around);
    CHECK(messages.list.empty());
    advance_vips(park.world(), set, 9.1f * kMonth, 1, messages, around);
    advance_vips(park.world(), set, 9.8f * kMonth, 1, messages, around);
    CHECK(keys(messages) == std::vector<std::string> { "VIPCondition_MonthNotice", "VIPCondition_WeekNotice" });
    CHECK_FALSE(the_vip(park.world().registry));
    advance_vips(park.world(), set, 10 * kMonth, 1, messages, around);
    auto& r = park.world().registry;
    const auto vip = the_vip(r);
    REQUIRE(vip);
    CHECK(r.get<Visitor>(*vip).name == "Kara");
    CHECK(r.get<Visitor>(*vip).female);
    CHECK(r.get<Vip>(*vip).goal == 7);
    CHECK(messages.list.back().text_key == "VIP_ArrivedInPark");
    CHECK(messages.list.back().subject_name == "Kara");
    // Only one comes at a time.
    advance_vips(park.world(), set, 10.1f * kMonth, 1, messages, around);
    CHECK(std::ranges::distance(r.view<Vip>()) == 1);
}

TEST_CASE("a VIP of another level sends no one; their date keeps ahead of the calendar")
{
    Sandbox park = gated();
    auto set = one_goal({ .next_visit = 2 * kMonth });
    set.conditions[0].difficulty = 1;
    ParkMessages messages;
    advance_vips(park.world(), set, 5.5f * kMonth, 1, messages, { });
    CHECK(messages.list.empty());
    CHECK_FALSE(the_vip(park.world().registry));
    CHECK(set.conditions[0].vip->next_visit >= 5.5f * kMonth);
    CHECK(set.conditions[0].vip->next_visit < 6.5f * kMonth);
}

TEST_CASE("litter on their path wears a VIP's patience down till they leave, and the visit fails")
{
    Sandbox park = gated();
    auto set = one_goal({ .wishes = T::kLitter, .next_visit = 0, .retry_months = 3, .months_to_stay = 4, .litter_tolerance = 2 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    auto& r = park.world().registry;
    const auto vip = the_vip(r);
    REQUIRE(vip);
    r.get<Mood>(*vip).happiness.value = 0;
    const Stride& stride = r.get<Stride>(*vip);
    park.litter({ 0, 0 }, 5);
    for (const auto [e, l] : r.view<Litter>().each())
        l.node = stride.from;
    for (std::int32_t i = 0; i < 10 && !r.get<Vip>(*vip).leaving; ++i)
        advance_vips(park.world(), set, 1, kTuning.vips.look.value, messages, { });
    CHECK(r.get<Vip>(*vip).leaving);
    CHECK(r.get<Vip>(*vip).patience.value == 0);
    CHECK(messages.list.back().text_key == "VIP_IsLeavingPark");
    CHECK(park.run_until([&] { return r.all_of<Left>(*vip); }, 300)); // They walk out at the gate
    advance_vips(park.world(), set, 2, 1, messages, { });
    CHECK(messages.list.back().text_key == "VIPCondition_Failure");
    CHECK_FALSE(set.conditions[0].vip->met);
    CHECK(set.conditions[0].vip->next_visit == doctest::Approx(2 + 3 * kMonth));
    CHECK_FALSE(the_vip(r));
}

TEST_CASE("a VIP who saw what they came for and kept their patience meets the goal")
{
    Sandbox park = gated();
    auto set = one_goal({ .wishes = T::kThemedArea | T::kFireworks, .next_visit = 0, .months_to_stay = 1, .theme = 4, .scenery_needed = 40 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    auto& r = park.world().registry;
    const auto vip = the_vip(r);
    REQUIRE(vip);
    r.ctx().emplace<FireworkShow>(); // A display going up
    std::int32_t asked_theme = -1;
    const VipSurroundings around { .themed = [&](glm::vec3, std::int32_t theme, std::int32_t) {
        asked_theme = theme;
        return 40;
    } };
    advance_vips(park.world(), set, 0.5f * kMonth, kTuning.vips.look.value, messages, around);
    CHECK(asked_theme == 4);
    CHECK(vip_opinion(r.get<Vip>(*vip), *set.conditions[0].vip, 0) == doctest::Approx(1));
    r.ctx().erase<FireworkShow>(); // Over, so they may walk out
    advance_vips(park.world(), set, 1.1f * kMonth, kTuning.vips.look.value, messages, around);
    CHECK(r.get<Vip>(*vip).leaving);
    REQUIRE(park.run_until([&] { return r.all_of<Left>(*vip); }, 300));
    advance_vips(park.world(), set, 1.2f * kMonth, 1, messages, around);
    CHECK(messages.list.back().text_key == "VIPCondition_Success");
    CHECK(set.conditions[0].vip->met);
    CHECK(check_goals(set, { }, 1.2f * kMonth).has_value()); // The level is won
}

TEST_CASE("a VIP's opinion counts each coaster wanted once, and is their happiness when they want nothing")
{
    Vip vip;
    const T wants { .wishes = T::kCoasters | T::kFireworks, .coasters_needed = 2 };
    CHECK(vip_opinion(vip, wants, 0.8f) == 0);
    vip.coasters = { entt::entity { 1 }, entt::entity { 2 }, entt::entity { 3 } };
    CHECK(vip_opinion(vip, wants, 0.8f) == doctest::Approx(2.0f / 3.0f));
    vip.fireworks = true;
    CHECK(vip_opinion(vip, wants, 0.8f) == 1);
    CHECK(vip_opinion(vip, { .wishes = T::kLitter }, 0.8f) == doctest::Approx(0.8f));
}

TEST_CASE("a VIP comes in with the bodyguards their goal names, who walk with them as their party")
{
    Sandbox park = gated();
    auto set = one_goal({ .next_visit = 0, .months_to_stay = 1, .guards = 3 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    auto& r = park.world().registry;
    const auto vip = the_vip(r);
    REQUIRE(vip);
    REQUIRE(r.all_of<Member>(*vip));
    const Party& party = r.get<Party>(r.get<Member>(*vip).party);
    CHECK(party.leader == *vip);
    CHECK(party.members.size() == 4);
    for (const entt::entity m : party.members)
        if (m != *vip)
        {
            CHECK(r.get<Bodyguard>(m).vip == *vip);
            CHECK(r.get<Errand>(m).kind == ErrandKind::Following);
        }
}

TEST_CASE("a VIP whose goal names no bodyguards comes in alone")
{
    Sandbox park = gated();
    auto set = one_goal({ .next_visit = 0, .months_to_stay = 1, .guards = 0 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    auto& r = park.world().registry;
    REQUIRE(the_vip(r));
    CHECK(r.view<Bodyguard>().empty());
}

TEST_CASE("a VIP walks to the coasters they want, most exciting first, and goes on once they have ridden enough")
{
    Sandbox park = gated();
    auto& r = park.world().registry;
    const entt::entity tame = park.flat_ride("Tame", { 2, 0 }, 100, { .excitement = 0.55f });
    const entt::entity wild = park.flat_ride("Wild", { 4, 0 }, 100, { .excitement = 0.7f });
    const entt::entity dull = park.flat_ride("Dull", { 3, 0 }, 100, { .excitement = 0.3f });
    for (const entt::entity ride : { tame, wild, dull })
        r.emplace<Trains>(ride);
    auto set = one_goal({ .wishes = T::kCoasters, .next_visit = 0, .months_to_stay = 1, .coasters_needed = 1, .min_excitement = 5 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    const auto vip = the_vip(r);
    REQUIRE(vip);
    CHECK(r.get<Vip>(*vip).stops == std::vector { wild, tame }); // Not the one too dull for them
    advance_vips(park.world(), set, 1, kTuning.vips.look.value, messages, { });
    CHECK(r.get<Errand>(*vip).kind == ErrandKind::Heading);
    CHECK(r.get<Errand>(*vip).target == wild);
    r.get<Errand>(*vip) = { ErrandKind::Riding, wild, 0 };
    advance_vips(park.world(), set, 2, kTuning.vips.look.value, messages, { });
    CHECK(r.get<Vip>(*vip).coasters == std::vector { wild });
    CHECK(r.get<Vip>(*vip).next_stop == 2); // One was all they wanted
    CHECK(vip_opinion(r.get<Vip>(*vip), *set.conditions[0].vip, 0) == 1);
}

TEST_CASE("a VIP held up on the way to a stop says why, says so again later, then gives it up")
{
    Sandbox park = gated();
    park.path({ 8, 0 }, { 9, 0 }); // Not joined to the rest
    auto& r = park.world().registry;
    const entt::entity joined = park.flat_ride("Joined", { 4, 0 }, 100, { .excitement = 0.7f });
    const entt::entity apart = park.flat_ride("Apart", { 9, 0 }, 100, { .excitement = 0.8f });
    r.emplace<Trains>(joined);
    r.emplace<Trains>(apart);
    std::string first = "VIP_CantGetToWaypoint";
    std::string again = "VIP_CantGetToWaypoint2";
    entt::entity held = apart;
    SUBCASE("no way there")
    {
    }
    SUBCASE("closed")
    {
        r.get<Attraction>(joined).open = false;
        r.get<CycleRide>(joined).excitement.value = 0.9f;
        held = joined;
        first = "VIP_AttractionClosed";
        again = "VIP_AttractionClosed2";
    }
    SUBCASE("broken down")
    {
        r.emplace<Upkeep>(joined, Upkeep { .broken = true });
        r.get<CycleRide>(joined).excitement.value = 0.9f;
        held = joined;
        first = "VIP_AttractionBroken";
        again = "VIP_AttractionBroken2";
    }
    SUBCASE("crashed")
    {
        r.emplace<Wrecks>(joined, Wrecks { .trains = { { Wreck { } } } });
        r.get<CycleRide>(joined).excitement.value = 0.9f;
        held = joined;
        first = "VIP_AttractionCrashed";
        again = "VIP_AttractionCrashed2";
    }
    auto set = one_goal({ .wishes = T::kCoasters, .next_visit = 0, .months_to_stay = 1, .coasters_needed = 2, .min_excitement = 5 });
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    const auto vip = the_vip(r);
    REQUIRE(vip);
    REQUIRE(r.get<Vip>(*vip).stops.front() == held);
    const auto look = [&] { advance_vips(park.world(), set, 1, kTuning.vips.look.value, messages, { }); };
    look();
    CHECK(messages.list.back().text_key == first);
    CHECK(r.get<Errand>(*vip).kind == ErrandKind::Wandering);
    const auto said = [&](const std::string& key) { return std::ranges::count(keys(messages), key); };
    for (std::int32_t i = 0; i < 100 && said(again) == 0; ++i)
        look();
    CHECK(said(first) == 1); // Once, not at every look
    CHECK(said(again) == 1);
    for (std::int32_t i = 0; i < 100 && said("VIP_GivenUpTryingToGetToWaypoint") == 0; ++i)
        look();
    CHECK(said(again) == 1);
    CHECK(r.get<Vip>(*vip).next_stop == 1);
    look(); // Off to the other one
    CHECK(r.get<Errand>(*vip).kind == (held == apart ? ErrandKind::Heading : ErrandKind::Wandering));
    CHECK(vip_opinion(r.get<Vip>(*vip), *set.conditions[0].vip, 0) < 1); // Short of what they came for so far
}

TEST_CASE("a VIP who wants a swim has seen the pool once they swim in one")
{
    Vip vip;
    const T wants { .wishes = T::kPool };
    CHECK(vip_opinion(vip, wants, 1) == 0);
    vip.pool = true;
    CHECK(vip_opinion(vip, wants, 1) == 1);
}
