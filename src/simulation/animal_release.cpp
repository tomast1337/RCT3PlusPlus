// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/animal_release.hpp"

#include "simulation/tuning.hpp"
#include "strings/ascii.hpp"

#include <algorithm>

namespace rct3::simulation
{

    bool may_release(const Animal& animal) noexcept
    {
        return animal.adult && animal.health.value >= kReleaseHealth.value;
    }

    void count_release(save::model::ObjectiveSet& goals, std::string_view species)
    {
        for (save::model::Condition& g : goals.conditions)
        {
            if (g.kind != save::model::ConditionKind::AnimalsReleased || !g.animals || !strings::equals_ignoring_case(g.animals->species, species))
                continue;
            const bool running = !g.challenge || std::ranges::any_of(goals.challenges, [&](const save::model::Challenge& c) { return c.challenge_id == *g.challenge && c.state == save::model::ChallengeState::Running; });
            g.animals->done += running ? 1 : 0;
        }
    }

    bool release_animal(SimWorld& world, entt::entity animal, f32 rarity, save::model::ObjectiveSet& goals, f32 now)
    {
        entt::registry& r = world.registry;
        const Animal* a = r.valid(animal) ? r.try_get<Animal>(animal) : nullptr;
        if (a == nullptr || !may_release(*a))
            return false;
        count_release(goals, a->species);
        goals.publicity_until = std::max(goals.publicity_until, now) + std::clamp(rarity, 0.0f, 1.0f) * kTuning.prizes.publicity.value;
        // Keepers going after it, mates and young let go of it as they do of an animal killed: by its id no longer valid.
        r.destroy(animal);
        return true;
    }

} // namespace rct3::simulation
