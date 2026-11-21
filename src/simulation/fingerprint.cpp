// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/fingerprint.hpp"

#include "core/hash.hpp"
#include "simulation/coaster.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/rides.hpp"

#include <bit>
#include <utility>

namespace rct3::simulation
{

    u64 fingerprint(const SimWorld& world)
    {
        u64 h = core::kFnv64Offset;
        const auto mix = [&h](u64 v) { h = (h ^ v) * core::kFnv64Prime; };
        const auto mix_f = [&mix](f32 v) { mix(std::bit_cast<u32>(v)); };
        const auto mix_v = [&mix_f](glm::vec3 v) { mix_f(v.x), mix_f(v.y), mix_f(v.z); };
        const entt::registry& r = world.registry;
        mix(world.tick);
        mix(world.seed.value);
        for (const u64 w : world.gameplay.words)
            mix(w);
        for (const auto [id, storage] : r.storage())
        {
            mix(id);
            mix(storage.size());
            for (const entt::entity e : storage)
                mix(entt::to_integral(e));
        }
        for (const auto [e, pose] : r.view<const Pose>().each())
            mix_v(pose.position), mix_f(pose.heading.value);
        for (const auto [e, stride] : r.view<const Stride>().each())
            mix(stride.from.value), mix(stride.to.value), mix_f(stride.progress.value), mix(stride.route.size());
        for (const auto [e, errand] : r.view<const Errand>().each())
            mix(std::to_underlying(errand.kind)), mix(entt::to_integral(errand.target));
        for (const auto [e, needs] : r.view<const Needs>().each())
            mix_f(needs.hunger.value), mix_f(needs.thirst.value), mix_f(needs.bladder.value), mix_f(needs.swim.value);
        for (const auto [e, mood] : r.view<const Mood>().each())
            mix_f(mood.happiness.value), mix_f(mood.energy.value), mix_f(mood.nausea.value);
        for (const auto [e, wallet] : r.view<const Wallet>().each())
            mix(static_cast<u64>(wallet.cash.value)), mix(static_cast<u64>(wallet.spent.value));
        for (const auto [e, dice] : r.view<const Dice>().each())
            for (const u64 w : dice.state.words)
                mix(w);
        for (const auto [e, trains] : r.view<const Trains>().each())
            for (const Coaster& t : trains.trains)
                mix_f(t.travelled), mix_f(t.speed);
        return h;
    }

} // namespace rct3::simulation
