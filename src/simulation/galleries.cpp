// SPDX-License-Identifier: GPL-3.0-or-later
#include "simulation/galleries.hpp"

#include "simulation/animals.hpp"
#include "simulation/components.hpp"
#include "simulation/guests.hpp"
#include "simulation/systems.hpp"
#include "simulation/tuning.hpp"

#include <algorithm>
#include <cmath>
#include <vector>

namespace rct3::simulation
{

    units::Share enclosure_rating(const EnclosureLook& look)
    {
        const auto& k = kGalleryRates;
        const EnclosureSite& site = look.site;
        if (look.animals <= 0 || site.tiles <= 0)
            return { };
        const auto tiles = static_cast<f32>(site.tiles);
        const f32 size = std::min(1.0f, tiles / k.roomy);
        const f32 planted = std::min(1.0f, static_cast<f32>(site.plants) / (tiles * kAnimalCareRates.plants_per_tile));
        const f32 water = std::min(1.0f, static_cast<f32>(site.water) / (tiles * k.watery));
        const f32 level = std::min(1.0f, static_cast<f32>(site.level) / tiles);
        const f32 room = look.room_wanted > 0 ? std::min(1.0f, tiles / look.room_wanted) : 1.0f;
        const f32 habitat = k.size_weight.value * size + k.plants_weight.value * planted + k.water_weight.value * water + k.ground_weight.value * level + k.room_weight.value * room;
        // Half the show is there being animals at all, the rest how many; sick ones still draw a crowd, if less of one.
        const f32 lively = 0.5f + 0.5f * std::min(1.0f, static_cast<f32>(look.animals) / static_cast<f32>(k.lively));
        const f32 well = 1 - k.sickly.value * (1 - look.health.value);
        return { std::clamp(habitat * lively * well, 0.0f, 1.0f) };
    }

    void rate_galleries(SimWorld& world, std::span<const EnclosureSite> sites)
    {
        auto& r = world.registry;
        if (r.view<const Gallery>().empty())
            return;
        std::vector<EnclosureLook> looks(sites.size());
        for (usize i = 0; i < sites.size(); ++i)
            looks[i].site = sites[i];
        std::vector<f32> health(sites.size(), 0.0f);
        for (const auto [e, animal] : r.view<const Animal>().each())
        {
            if (!animal.enclosure || static_cast<usize>(*animal.enclosure) >= looks.size() || animal.job == AnimalJob::Crated)
                continue;
            const auto at = static_cast<usize>(*animal.enclosure);
            ++looks[at].animals;
            health[at] += animal.health.value;
            if (const auto* care = r.try_get<const AnimalCare>(e))
                looks[at].room_wanted += static_cast<f32>(care->space);
        }
        for (usize i = 0; i < looks.size(); ++i)
            if (looks[i].animals > 0)
                looks[i].health = { health[i] / static_cast<f32>(looks[i].animals) };
        for (const auto [e, gallery] : r.view<Gallery>().each())
        {
            const bool known = gallery.enclosure && static_cast<usize>(*gallery.enclosure) < looks.size();
            gallery.excitement = known ? enclosure_rating(looks[static_cast<usize>(*gallery.enclosure)]) : units::Share { };
            gallery.animals_health = known ? looks[static_cast<usize>(*gallery.enclosure)].health : units::Share { 1 };
        }
    }

    units::Money gallery_worth(const Gallery& gallery)
    {
        // Any animals at all are worth a look, and more the better the enclosure: up to more than a ride as exciting.
        const f32 seen = kGalleryRates.any_animals.value + gallery.excitement.value;
        return { gallery.excitement.value > 0 ? std::lround(kTuning.arrivals.ride_worth.value * seen) : 0 };
    }

    usize watching(const entt::registry& r, entt::entity gallery)
    {
        return static_cast<usize>(std::ranges::count_if(r.view<const Errand>().each(), [&](const auto& each) {
            const Errand& errand = std::get<1>(each);
            return errand.kind == ErrandKind::Buying && errand.target == gallery;
        }));
    }

    void watch_animals(entt::registry& r, entt::entity guest, entt::entity gallery, f32 now)
    {
        const auto& k = kGalleryRates;
        Gallery& g = r.get<Gallery>(gallery);
        auto& happiness = r.get<Mood>(guest).happiness.value;
        happiness = clamp01(happiness + k.cheer.value * (g.excitement.value - k.boring.value) / (1 - k.boring.value));
        const ThoughtKind thought = g.animals_health.value < k.unwell.value ? ThoughtKind::AnimalLooksUnhealthy : g.excitement.value < k.boring.value ? ThoughtKind::AnimalIsBoring : random::chance(r.get<Dice>(guest).state, 0.5) ? ThoughtKind::AnimalLooksHealthy : ThoughtKind::SeeAnimal;
        think(r, guest, thought, gallery, now);
        ++g.watched;
        g.pleased += thought == ThoughtKind::SeeAnimal || thought == ThoughtKind::AnimalLooksHealthy ? 1 : 0;
    }

} // namespace rct3::simulation
