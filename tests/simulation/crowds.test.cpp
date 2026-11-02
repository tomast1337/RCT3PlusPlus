// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/crowds.hpp"

#include "simulation/components.hpp"
#include "simulation/objectives.hpp"
#include "simulation/sandbox.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"
#include "simulation/vip.hpp"

#include <algorithm>
#include <cmath>
#include <doctest/doctest.h>
#include <ranges>

using namespace rct3;
using namespace rct3::simulation;

namespace
{
    /// An entertainer in the middle of a path with guests about them, ready to look for a crowd.
    struct Stage
    {
        Sandbox park;
        entt::entity star = entt::null;
        std::vector<entt::entity> guests;

        explicit Stage(float happiness, float laziness)
        {
            park.path({ 0, 0 }, { 8, 0 });
            star = park.staff(StaffRole::Entertainer, { 4, 0 });
            auto& staff = park.world().registry.get<Staff>(star);
            staff.happiness.value = happiness;
            staff.laziness.value = laziness;
            for (const int col : { 3, 3, 5, 5, 4, 4 })
                guests.push_back(park.guest({ col, 0 }));
        }

        bool performing()
        {
            const auto* show = park.world().registry.try_get<Show>(star);
            return show != nullptr && !show->moves.empty();
        }

        std::vector<entt::entity> watching()
        {
            auto& r = park.world().registry;
            return guests | std::views::filter([&](entt::entity g) { return r.all_of<Watching>(g); }) | std::ranges::to<std::vector>();
        }
    };

    bool thought(entt::registry& r, entt::entity g, ThoughtKind kind)
    {
        const auto* t = r.try_get<Thoughts>(g);
        return t != nullptr && std::ranges::any_of(t->recent, [&](const Thought& x) { return x.kind == kind; });
    }
} // namespace

TEST_CASE("a better trained entertainer's show has more moves and fancier dances; a miserable or lazy one's is worse")
{
    random::RandomState dice = random::seeded(random::Seed { 3 });
    for (int i = 0; i < 20; ++i)
    {
        const auto plain = show_moves(0, false, dice);
        CHECK(plain.size() == static_cast<std::size_t>(kTuning.shows.moves));
        CHECK(std::ranges::contains(plain, ShowMove::Wave)); // Their height, untrained
        CHECK(std::ranges::none_of(plain, [](ShowMove m) { return m == ShowMove::DanceTwo || m == ShowMove::DanceThree; }));
        const auto best = show_moves(3, false, dice);
        CHECK(best.size() == plain.size() + 3);
        CHECK(std::ranges::contains(best, ShowMove::DanceThree));
        CHECK(show_seconds(best) > show_seconds(plain));
    }
    Staff staff { .role = StaffRole::Entertainer, .training = 3 };
    staff.happiness.value = 1;
    staff.laziness.value = 0;
    CHECK(show_quality(staff) == 3);
    CHECK_FALSE(poor_show(staff));
    const float keen = show_chance(staff);
    staff.training = 0;
    CHECK(show_chance(staff) < keen);
    staff.training = 3;
    staff.happiness.value = 0.1f;
    staff.laziness.value = 0.9f;
    CHECK(show_quality(staff) == 1);
    CHECK(poor_show(staff));
    CHECK(show_chance(staff) < keen);
}

TEST_CASE("a poor show has a fall in it, getting up after, and ends dejected")
{
    random::RandomState dice = random::seeded(random::Seed { 5 });
    for (int i = 0; i < 20; ++i)
    {
        const auto moves = show_moves(1, true, dice);
        const auto fall = std::ranges::find(moves, ShowMove::FallOver);
        REQUIRE(fall != moves.end());
        REQUIRE(fall + 1 != moves.end());
        CHECK(*(fall + 1) == ShowMove::GetUp);
        CHECK(moves.back() == ShowMove::Dejected);
        CHECK(std::ranges::none_of(moves, [](ShowMove m) { return m == ShowMove::DanceOne || m == ShowMove::DanceTwo || m == ShowMove::DanceThree; }));
    }
}

TEST_CASE("guests near an entertainer's show gather round, facing them, and clap after a good one, happier")
{
    Stage stage(1, 0);
    auto& r = stage.park.world().registry;
    REQUIRE(stage.park.run_until([&] { return stage.performing(); }, 30));
    CHECK(r.get<Staff>(stage.star).busy.value > 0);
    REQUIRE(stage.park.run_until([&] { return stage.watching().size() >= 2; }, 10));
    stage.park.run_seconds(3); // Time to step to their places
    const auto crowd = stage.watching();
    const glm::vec3 star = r.get<Pose>(stage.star).position;
    std::vector<float> happy;
    for (const entt::entity g : crowd)
    {
        const Pose& pose = r.get<Pose>(g);
        const glm::vec2 off { star.x - pose.position.x, star.z - pose.position.z };
        CHECK(glm::length(off) <= kTuning.shows.ring.value + 0.5f);
        CHECK(std::abs(std::remainder(pose.heading.value - std::atan2(off.x, off.y), 2 * glm::pi<float>())) < 0.1f); // Facing the star
        happy.push_back(r.get<Mood>(g).happiness.value);
    }
    // They stand still, the star too, while the show goes on.
    const glm::vec3 was = r.get<Pose>(crowd.front()).position;
    stage.park.run_seconds(1);
    if (stage.performing())
    {
        CHECK(glm::distance(r.get<Pose>(crowd.front()).position, was) < 0.05f);
        CHECK(r.get<Pose>(stage.star).position == star);
    }
    REQUIRE(stage.park.run_until([&] { return !stage.performing(); }, 60));
    for (std::size_t i = 0; i < crowd.size(); ++i)
    {
        const entt::entity g = crowd[i];
        CHECK_FALSE(r.all_of<Watching>(g));
        REQUIRE(r.all_of<Gesture>(g));
        const GestureKind kind = r.get<Gesture>(g).kind;
        CHECK((kind == GestureKind::Clap || kind == GestureKind::Cheer || kind == GestureKind::Laugh));
        CHECK(r.get<Mood>(g).happiness.value > happy[i]);
        CHECK(thought(r, g, ThoughtKind::GreatEntertainer));
    }
    // After a rest they walk on, and the entertainer waits a while before the next show.
    stage.park.run_seconds(kTuning.shows.clap.value + 1);
    CHECK(std::ranges::none_of(crowd, [&](entt::entity g) { return r.any_of<Gesture, Watching>(g); }));
    CHECK(r.get<Show>(stage.star).gap.value > 0);
}

TEST_CASE("after a poor show the crowd shakes their heads, no happier, and the entertainer is glummer")
{
    Stage stage(1, 0.9f);
    auto& r = stage.park.world().registry;
    REQUIRE(stage.park.run_until([&] { return stage.performing(); }, 60));
    CHECK(r.get<Show>(stage.star).poor);
    REQUIRE(stage.park.run_until([&] { return !stage.watching().empty(); }, 10));
    const auto crowd = stage.watching();
    std::vector<float> happy;
    for (const entt::entity g : crowd)
        happy.push_back(r.get<Mood>(g).happiness.value);
    const float glum = r.get<Staff>(stage.star).happiness.value;
    REQUIRE(stage.park.run_until([&] { return !stage.performing(); }, 60));
    for (std::size_t i = 0; i < crowd.size(); ++i)
    {
        REQUIRE(r.all_of<Gesture>(crowd[i]));
        CHECK(r.get<Gesture>(crowd[i]).kind == GestureKind::Grumble);
        CHECK(r.get<Mood>(crowd[i]).happiness.value <= happy[i]);
        CHECK(thought(r, crowd[i], ThoughtKind::BoringEntertainer));
    }
    CHECK(r.get<Staff>(stage.star).happiness.value < glum);
}

TEST_CASE("an entertainer picked up mid-show stops it, and the crowd walks on without a reaction")
{
    Stage stage(1, 0);
    auto& r = stage.park.world().registry;
    REQUIRE(stage.park.run_until([&] { return stage.performing() && !stage.watching().empty(); }, 40));
    const auto crowd = stage.watching();
    REQUIRE(pick_up(stage.park.world(), stage.star));
    stage.park.run(2);
    CHECK_FALSE(stage.performing());
    CHECK(std::ranges::none_of(crowd, [&](entt::entity g) { return r.any_of<Watching, Gesture>(g); }));
}

TEST_CASE("fans gather round a VIP, who signs for them one at a time; each autograph wears a VIP who minds hunters down")
{
    using T = save::VipTarget;
    Sandbox park;
    park.path({ 0, 0 }, { 6, 0 });
    auto& r = park.world().registry;
    r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
    park.run(1);
    save::ObjectiveSet set;
    set.objectives = { { .difficulty = 0 } };
    set.conditions = { { .kind = save::ConditionKind::Vip, .id = 7, .vip = T { .wishes = T::kAutographs, .next_visit = 0, .months_to_stay = 4, .autograph_tolerance = 4 } } };
    ParkMessages messages;
    advance_vips(park.world(), set, 0, 1, messages, { });
    const auto vips = r.view<Vip>();
    REQUIRE(vips.begin() != vips.end());
    const entt::entity vip = *vips.begin();
    std::vector<entt::entity> fans;
    for (const int col : { 1, 1, 2, 2 })
        fans.push_back(park.guest({ col, 0 }));
    const auto signed_for = [&](entt::entity g) {
        const auto* got = r.try_get<Autographs>(g);
        return got != nullptr && std::ranges::contains(got->from, vip);
    };
    std::vector<float> happy;
    for (const entt::entity g : fans)
        happy.push_back(r.get<Mood>(g).happiness.value);
    REQUIRE(park.run_until([&] { return std::ranges::any_of(fans, [&](entt::entity g) { return r.all_of<Watching>(g) && r.get<Watching>(g).autograph; }); }, 30));
    REQUIRE(park.run_until([&] { return std::ranges::count_if(fans, signed_for) >= 2; }, 60));
    for (std::size_t i = 0; i < fans.size(); ++i)
        if (signed_for(fans[i]))
            CHECK(r.get<Mood>(fans[i]).happiness.value > happy[i]);
    // Signed for, they do not ask that VIP again.
    park.run_seconds(2 * kTuning.fans.poll.value);
    for (const entt::entity g : fans)
        if (signed_for(g))
            CHECK_FALSE(r.all_of<Watching>(g));
    // The VIP's next look counts the autographs given since: a quarter of their patience each.
    r.get<Mood>(vip).happiness.value = 0;
    const auto given = r.get<Vip>(vip).signatures;
    CHECK(given >= 1);
    advance_vips(park.world(), set, 1, kTuning.vips.look.value, messages, { });
    CHECK(r.get<Vip>(vip).patience.value == doctest::Approx(std::max(0.0f, 1.0f - static_cast<float>(given) / 4)));
    CHECK(r.get<Vip>(vip).signatures == 0);
}

namespace
{
    /// A VIP let in at the gate on (0, 0) of a long path, walking on to its far end.
    struct VipWalk
    {
        Sandbox park;
        save::ObjectiveSet set;
        entt::entity vip = entt::null;

        VipWalk()
        {
            using T = save::VipTarget;
            park.path({ 0, 0 }, { 14, 0 });
            auto& r = park.world().registry;
            r.emplace<ParkGate>(r.create(), ParkGate { park.world().paths.find({ 0, 0, 0 }).value() });
            park.run(1);
            set.objectives = { { .difficulty = 0 } };
            set.conditions = { { .kind = save::ConditionKind::Vip, .id = 7, .vip = T { .next_visit = 0, .months_to_stay = 4 } } };
            ParkMessages messages;
            advance_vips(park.world(), set, 0, 1, messages, { });
            vip = *r.view<Vip>().begin();
            r.get<Vip>(vip).poll.value = 1000; // No fans of their own finding
            Stride& stride = r.get<Stride>(vip);
            stride.route = park.world().paths.route(stride.to, park.world().paths.find({ 14, 0, 0 }).value()).value();
        }

        float tile_of(entt::entity e)
        {
            return park.world().registry.get<Pose>(e).position.x / park.world().paths.tile_size;
        }
    };
} // namespace

TEST_CASE("a fan walks after a VIP who walks on, along the paths, and is signed once they catch up")
{
    VipWalk walk;
    auto& r = walk.park.world().registry;
    REQUIRE(walk.park.run_until([&] { return walk.tile_of(walk.vip) >= 6; }, 60));
    const entt::entity fan = walk.park.guest({ 0, 0 });
    r.emplace<Watching>(fan, Watching { .star = walk.vip, .autograph = true });
    walk.park.run(1);
    REQUIRE(r.all_of<Watching>(fan));
    CHECK(r.get<Watching>(fan).chasing);
    CHECK_FALSE(r.get<Stride>(fan).route.empty());
    const auto signed_for = [&] {
        const auto* got = r.try_get<Autographs>(fan);
        return got != nullptr && std::ranges::contains(got->from, walk.vip);
    };
    REQUIRE(walk.park.run_until(signed_for, kTuning.fans.give_up.value));
    CHECK(walk.tile_of(fan) >= 5); // Followed several tiles
    CHECK(glm::distance(r.get<Pose>(fan).position, r.get<Pose>(walk.vip).position) <= kTuning.fans.arm.value);
}

TEST_CASE("a fan gives up on a VIP too far off, out of reach on the paths, or after a while")
{
    SUBCASE("too far")
    {
        VipWalk walk;
        auto& r = walk.park.world().registry;
        REQUIRE(walk.park.run_until([&] { return walk.tile_of(walk.vip) >= 13; }, 120));
        const entt::entity fan = walk.park.guest({ 0, 0 });
        r.emplace<Watching>(fan, Watching { .star = walk.vip, .autograph = true });
        walk.park.run(1);
        CHECK_FALSE(r.all_of<Watching>(fan));
    }
    SUBCASE("no way there")
    {
        VipWalk walk;
        auto& r = walk.park.world().registry;
        walk.park.path({ 0, 3 }, { 2, 3 }); // Apart from the VIP's path
        const entt::entity fan = walk.park.guest({ 0, 3 });
        r.emplace<Watching>(fan, Watching { .star = walk.vip, .autograph = true });
        walk.park.run(1);
        CHECK_FALSE(r.all_of<Watching>(fan));
    }
    SUBCASE("after a while")
    {
        VipWalk walk;
        auto& r = walk.park.world().registry;
        const entt::entity fan = walk.park.guest({ 0, 0 });
        r.emplace<Watching>(fan, Watching { .star = walk.vip, .autograph = true, .waited = kTuning.fans.give_up });
        walk.park.run(1);
        CHECK_FALSE(r.all_of<Watching>(fan));
    }
}
