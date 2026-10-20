// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/import_people.hpp"

#include "simulation/components.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <unordered_map>

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
            r.emplace<Visitor>(e, age_group(p.type), p.sub_type == std::to_underlying(save::Sex::Female), p.name, std::uint64_t { 0 }, g.is_avatar);
            const glm::vec3 at { p.position.x, p.position.y, p.position.z };
            r.emplace<Pose>(e, at, -p.heading, at, -p.heading); // The original turns the other way
            r.emplace<Needs>(e, need(g.needs.food), need(g.needs.drink), std::clamp(g.needs.toilet, 0.0f, 1.0f));
            r.emplace<Mood>(e, g.mood_now.happiness, g.mood_now.energy, g.mood_now.nausea, kTuning.sickness.tolerance_per_saved_level * static_cast<float>(std::to_underlying(g.nausea_tolerance) + 1));
            r.emplace<Wallet>(e, std::int64_t { g.cash.current });
            // Saved on the rides' rating scale, about 0 to 10.
            if (g.ride_preference.max > g.ride_preference.min)
                r.emplace<Taste>(e, g.ride_preference.min / 10, g.ride_preference.max / 10);
        }
        return out;
    }

    std::size_t import_parties(SimWorld& world, const save::PeopleModel& people, std::span<const entt::entity> guests)
    {
        entt::registry& r = world.registry;
        std::unordered_map<save::PeopleObjectId, entt::entity> by_id;
        for (std::size_t i = 0; i < people.guests.size() && i < guests.size(); ++i)
            if (guests[i] != entt::null)
                by_id.emplace(people.guests[i].person.id, guests[i]);
        std::size_t made = 0;
        for (const save::GuestGroup& group : people.groups)
        {
            Party party;
            for (const save::PeopleRef ref : group.members)
            {
                const auto found = by_id.find(ref.id);
                if (ref && found != by_id.end() && r.all_of<Errand>(found->second) && !r.all_of<Member>(found->second) && std::ranges::find(party.members, found->second) == party.members.end())
                    party.members.push_back(found->second);
            }
            if (party.members.size() < 2)
                continue;
            const auto leader = by_id.find(group.leader.id);
            party.leader = leader != by_id.end() && std::ranges::find(party.members, leader->second) != party.members.end() ? leader->second : party.members.front();
            const entt::entity e = r.create();
            for (const entt::entity m : party.members)
            {
                r.emplace<Member>(m, e);
                if (auto& errand = r.get<Errand>(m); m != party.leader && errand.kind == ErrandKind::Wandering)
                    errand = { ErrandKind::Following, entt::null, 0 };
            }
            r.emplace<Party>(e, std::move(party));
            ++made;
        }
        return made;
    }

} // namespace rct3::simulation
