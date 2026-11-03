// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/components.hpp"
#include "simulation/import_park.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <array>
#include <doctest/doctest.h>
#include <utility>
#include <vector>

using namespace rct3::simulation;

TEST_CASE("guests use toilets, cash machines and first aid when they need them")
{
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        const auto toilet = park.shop("Toilet", Product::Toilet, { 6, 0 }, 20);
        const auto g = park.guest({ 0, 0 }, { .bladder = 0.9f });
        auto& r = park.world().registry;
        CHECK(park.run_until([&] { return r.get<Needs>(g).bladder.value < 0.1f; }, 60));
        CHECK(r.get<Attraction>(toilet).customers == 1);
        CHECK(r.get<Wallet>(g).cash.value == 5000 - 20);
        CHECK_FALSE(r.all_of<Carried>(g)); // Nothing to carry away
    }
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("Cash", Product::CashMachine, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .cash = 100 });
        CHECK(park.run_until([&] { return park.world().registry.get<Wallet>(g).cash.value > 100; }, 60));
    }
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("First aid", Product::FirstAid, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .nausea = 0.9f });
        CHECK(park.run_until([&] { return park.world().registry.get<Mood>(g).nausea.value == 0; }, 60));
    }
}

TEST_CASE("a guest will not pay more for a toilet than their need makes it worth")
{
    for (const bool dear : { false, true })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        // Just over what a guest needing it this much pays, or well under.
        const auto toilet = park.shop("Toilet", Product::Toilet, { 6, 0 }, dear ? kTuning.facilities.toilet_worth.value * 9 / 10 + 10 : kTuning.facilities.toilet_worth.value / 2);
        const auto g = park.guest({ 0, 0 }, { .bladder = 0.9f });
        auto& r = park.world().registry;
        const auto thought = [&](ThoughtKind kind) { return r.all_of<Thoughts>(g) && std::ranges::any_of(r.get<Thoughts>(g).recent, [&](const Thought& t) { return t.kind == kind; }); };
        park.run_seconds(30);
        CHECK(r.get<Attraction>(toilet).customers == (dear ? 0 : 1));
        CHECK(thought(ThoughtKind::NotPayingToilet) == dear);
    }
}

TEST_CASE("a guest stays at a toilet or first aid the longer the more they needed it")
{
    // How long the guest stands at the facility, from paying to walking on.
    const auto stay = [](Product product, float need) {
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("Facility", product, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .bladder = product == Product::Toilet ? need : 0.0f, .nausea = product == Product::FirstAid ? need : 0.0f });
        auto& r = park.world().registry;
        const auto buying = [&] { return r.get<Errand>(g).kind == ErrandKind::Buying; };
        REQUIRE(park.run_until(buying, 60));
        float seconds = 0;
        while (buying() && seconds < 60)
        {
            park.run_seconds(0.5f);
            seconds += 0.5f;
        }
        return seconds;
    };
    // Not quite sick enough to throw up on the way.
    for (const auto [product, most] : { std::pair { Product::Toilet, 1.0f }, std::pair { Product::FirstAid, 0.75f } })
    {
        const float longest = stay(product, most);
        CHECK(longest >= kTuning.facilities.relief.value * most - 1);
        CHECK(stay(product, 0.6f) < longest - 1);
        CHECK(stay(product, 0.6f) >= kTuning.facilities.served.value);
    }
}

TEST_CASE("a guest who cannot afford what they want draws cash, though they have more than a little")
{
    {
        // Only dearer food than they have money for.
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("Food", Product::Food, { 6, 0 }, 3000);
        park.shop("Cash", Product::CashMachine, { 0, 0 }, 0);
        const auto g = park.guest({ 3, 0 }, { .hunger = 0.9f, .cash = 2000 });
        CHECK(park.run_until([&] { return park.world().registry.get<Wallet>(g).withdrawn.value > 0; }, 60));
    }
    {
        // Turned away for want of money where they could not see the price first.
        Sandbox park;
        park.path({ 0, 0 }, { 6, 0 });
        park.shop("Cash", Product::CashMachine, { 6, 0 }, 0);
        const auto g = park.guest({ 0, 0 }, { .cash = 2000 });
        auto& r = park.world().registry;
        r.get<Wallet>(g).needs_cash = true;
        CHECK(park.run_until([&] { return r.get<Wallet>(g).withdrawn.value > 0; }, 60));
        CHECK_FALSE(r.get<Wallet>(g).needs_cash);
    }
}

TEST_CASE("tired guests sit on the benches they pass, as many as there are seats, and rest")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    park.item(PathItemKind::Bench, { 0, 0 });
    std::vector<entt::entity> guests;
    for (int i = 0; i < 6; ++i)
        guests.push_back(park.guest({ 1, 0 }, { .energy = 0.1f }));
    auto& r = park.world().registry;
    std::size_t most = 0;
    for (int t = 0; t < 400; ++t)
    {
        park.run(1);
        most = std::max<std::size_t>(most, r.view<Linger>().size());
    }
    CHECK(most > 0);
    CHECK(most <= 3);
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) { return r.get<Mood>(g).energy.value > 0.2f; }));
}

TEST_CASE("guests stop at a view, cheered by it, and vandals leave it be")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    const auto view = park.item(PathItemKind::PhotoSpot, { 0, 0 });
    const auto g = park.guest({ 1, 0 }, { .happiness = 0.3f });
    auto& r = park.world().registry;
    CHECK(park.run_until([&] { return r.all_of<Linger>(g); }, 120));
    CHECK(r.get<Linger>(g).at == PathItemKind::PhotoSpot);
    const float before = r.get<Mood>(g).happiness.value;
    park.run_seconds(10);
    CHECK(r.get<Mood>(g).happiness.value > before);
    r.get<Mood>(g).happiness.value = 0;
    park.run_seconds(600);
    CHECK_FALSE(r.get<PathItem>(view).broken);
}

TEST_CASE("guests seldom stop at an effect or animatronic, far less than at a view")
{
    std::array<std::size_t, 2> stopped { };
    for (const PathItemKind kind : { PathItemKind::Sight, PathItemKind::PhotoSpot })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 1, 0 });
        park.item(kind, { 0, 0 });
        for (int i = 0; i < 10; ++i)
            park.guest({ 1, 0 });
        auto& r = park.world().registry;
        for (int t = 0; t < 2000; ++t)
        {
            park.run(1);
            stopped[kind == PathItemKind::PhotoSpot] += r.view<Linger>().size();
        }
    }
    CHECK(stopped[1] > 0);
    CHECK(stopped[0] * 4 < stopped[1]);
}

TEST_CASE("guests think the scenery round them great, or else the paths tidy")
{
    for (const bool planted : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 3, 0 });
        std::vector<rct3::save::PlacedObject> trees;
        for (int i = 0; i < 30; ++i)
            trees.push_back({ .id = static_cast<std::uint64_t>(i + 1), .col = i % 4, .row = 1 });
        import_scenery(park.world(), trees, [&](const rct3::save::PlacedObject&) { return planted ? 1.0f : 0.0f; });
        const auto g = park.guest({ 0, 0 });
        auto& r = park.world().registry;
        const auto thought = [&](ThoughtKind kind) {
            const auto* thoughts = r.try_get<Thoughts>(g);
            return thoughts != nullptr && std::ranges::contains(thoughts->recent, kind, &Thought::kind);
        };
        CHECK(park.run_until([&] { return thought(planted ? ThoughtKind::GreatScenery : ThoughtKind::ReallyTidy); }, 3000));
        CHECK_FALSE(thought(planted ? ThoughtKind::ReallyTidy : ThoughtKind::GreatScenery));
    }
}

TEST_CASE("a party with the player's guest in it has its photo taken at a photo spot, others do not")
{
    for (const bool avatar : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 1, 0 });
        park.item(PathItemKind::PhotoSpot, { 0, 0 });
        const auto leader = park.guest({ 1, 0 });
        const auto friend_ = park.guest({ 1, 0 });
        park.party({ leader, friend_ });
        auto& r = park.world().registry;
        r.get<Visitor>(friend_).avatar = avatar;
        CHECK(park.run_until([&] { return r.all_of<Linger>(leader); }, 120));
        CHECK(r.get<Linger>(leader).at == PathItemKind::PhotoSpot);
        const auto* photos = r.ctx().find<Photos>();
        if (avatar)
        {
            REQUIRE(photos != nullptr);
            REQUIRE(photos->taken.size() == 1);
            CHECK(photos->taken[0].guest == friend_);
            CHECK(std::ranges::contains(r.get<Thoughts>(friend_).recent, ThoughtKind::Photographee, &Thought::kind));
            CHECK(std::ranges::contains(r.get<Thoughts>(leader).recent, ThoughtKind::Photographer, &Thought::kind));
        }
        else
            CHECK((photos == nullptr || photos->taken.empty()));
    }
}

TEST_CASE("special attractions are told apart by the kind their item stores")
{
    CHECK(facility_of(11) == Product::Toilet);
    CHECK(facility_of(12) == Product::CashMachine);
    CHECK(facility_of(13) == Product::FirstAid);
    CHECK(facility_of(14) == std::nullopt);
}

TEST_CASE("path items are told apart by the setting their item's data names")
{
    using rct3::park::ItemCategory;
    using rct3::park::PathFeature;
    const auto feature = std::to_underlying(ItemCategory::PathFeature);
    CHECK(sight_of(feature, PathFeature::Vista) == PathItemKind::PhotoSpot);
    CHECK(sight_of(feature, PathFeature::Speaker) == PathItemKind::Speaker);
    CHECK(sight_of(feature, PathFeature::PassportPost) == PathItemKind::PassportPost);
    CHECK(sight_of(feature, PathFeature::Fountain) == PathItemKind::Fountain);
    CHECK(sight_of(feature, PathFeature::WaterCannon) == PathItemKind::WaterCannon);
    CHECK(sight_of(feature, PathFeature::None) == std::nullopt);
    CHECK(sight_of(std::to_underlying(ItemCategory::Effect), PathFeature::None) == PathItemKind::Sight);
    CHECK(sight_of(std::to_underlying(ItemCategory::Animatronic), PathFeature::None) == PathItemKind::Sight);
    CHECK(sight_of(std::to_underlying(ItemCategory::Bench), PathFeature::None) == std::nullopt);
}

TEST_CASE("guests passing jumping fountains turn to look, and some like them, without stopping to watch")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    park.item(PathItemKind::Fountain, { 0, 0 });
    std::vector<entt::entity> guests;
    for (int i = 0; i < 6; ++i)
        guests.push_back(park.guest({ 1, 0 }));
    auto& r = park.world().registry;
    bool glanced = false;
    for (int t = 0; t < 600; ++t)
    {
        park.run(1);
        glanced = glanced || std::ranges::any_of(guests, [&](entt::entity g) { return r.all_of<Gesture>(g) && r.get<Gesture>(g).kind == GestureKind::Glance; });
        CHECK(r.view<Linger>().empty());
    }
    CHECK(glanced);
    CHECK(std::ranges::any_of(guests, [&](entt::entity g) {
        const auto* thoughts = r.try_get<Thoughts>(g);
        return thoughts != nullptr && std::ranges::contains(thoughts->recent, ThoughtKind::JumpingFountainsGood, &Thought::kind);
    }));
}

TEST_CASE("a guest passing a free water cannon may fire it a while, one at a time")
{
    Sandbox park;
    park.path({ 0, 0 }, { 1, 0 });
    park.item(PathItemKind::WaterCannon, { 0, 0 });
    for (int i = 0; i < 8; ++i)
        park.guest({ 1, 0 });
    auto& r = park.world().registry;
    std::size_t most = 0;
    entt::entity firing = entt::null;
    for (int t = 0; t < 3000; ++t)
    {
        park.run(1);
        std::size_t now = 0;
        for (const auto [g, linger] : r.view<const Linger>().each())
            if (linger.at == PathItemKind::WaterCannon)
            {
                ++now;
                firing = g;
            }
        most = std::max(most, now);
    }
    CHECK(most == 1);
    CHECK(firing != entt::null);
}

TEST_CASE("a party stops together at a view and likes it with scenery round it, or folds its arms at a bare one")
{
    for (const bool planted : { true, false })
    {
        Sandbox park;
        park.path({ 0, 0 }, { 1, 0 });
        park.item(PathItemKind::PhotoSpot, { 0, 0 });
        std::vector<rct3::save::PlacedObject> trees;
        for (int i = 0; i < 30; ++i)
            trees.push_back({ .id = static_cast<std::uint64_t>(i + 1), .col = i % 2, .row = 1 });
        import_scenery(park.world(), trees, [&](const rct3::save::PlacedObject&) { return planted ? 1.0f : 0.0f; });
        const auto leader = park.guest({ 1, 0 });
        const auto friend_ = park.guest({ 1, 0 });
        park.party({ leader, friend_ });
        auto& r = park.world().registry;
        REQUIRE(park.run_until([&] { return r.all_of<Linger>(leader); }, 120));
        for (const auto g : { leader, friend_ })
        {
            CHECK(r.all_of<Linger>(g));
            const auto* thoughts = r.try_get<Thoughts>(g);
            CHECK((thoughts != nullptr && std::ranges::contains(thoughts->recent, ThoughtKind::PhotoSpotGood, &Thought::kind)) == planted);
            CHECK((r.all_of<Gesture>(g) && r.get<Gesture>(g).kind == GestureKind::Disapproval) == !planted);
        }
    }
}
