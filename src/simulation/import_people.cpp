// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_people.hpp"

#include "simulation/components.hpp"

#include <algorithm>

namespace rct3::simulation
{

    namespace
    {
        AgeGroup age_group(save::PersonType type)
        {
            switch (type)
            {
                case save::PersonType::Child:
                    return AgeGroup::Child;
                case save::PersonType::Teen:
                    return AgeGroup::Teen;
                default:
                    return AgeGroup::Adult;
            }
        }

        // The original keeps how full a guest is; we keep how much they need.
        float need(float fullness)
        {
            return std::clamp(1.0f - fullness, 0.0f, 1.0f);
        }
    } // namespace

    std::vector<entt::entity> import_guests(SimWorld& world, const save::PeopleModel& people)
    {
        entt::registry& r = world.registry;
        std::vector<entt::entity> out(people.guests.size(), entt::null);
        for (std::size_t i = 0; i < people.guests.size(); ++i)
        {
            const save::Guest& g = people.guests[i];
            if (!g.in_park)
                continue;
            const save::Person& p = g.person;
            const entt::entity e = r.create();
            out[i] = e;
            r.emplace<Visitor>(e, age_group(p.type), p.sub_type == std::to_underlying(save::Sex::Female), p.name, std::uint64_t { 0 });
            r.emplace<Pose>(e, glm::vec3 { p.position.x, p.position.y, p.position.z }, -p.heading); // The original turns the other way
            r.emplace<Needs>(e, need(g.needs.food), need(g.needs.drink), std::clamp(g.needs.toilet, 0.0f, 1.0f));
            r.emplace<Mood>(e, g.mood_now.happiness, g.mood_now.energy, g.mood_now.nausea);
            r.emplace<Wallet>(e, std::int64_t { g.cash.current });
        }
        return out;
    }

} // namespace rct3::simulation
