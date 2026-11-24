// SPDX-License-Identifier: GPL-3.0-or-later
#pragma once

// Viewing galleries: windows in an animal enclosure's fence that guests pay to look through, and how an enclosure
// rates with those who come to look at it.
//
// An enclosure rates by its room, its plants, its water and how level its ground is, by how many animals live in it
// and by how well they are; one with no animals in it has nothing to see. A gallery is as exciting as the enclosure
// it looks into. Guests pick a gallery as they pick a ride, pay its ticket at the window, watch a while and come away
// thinking of what they saw.

#include "core/types.hpp"
#include "math/glm.hpp"
#include "simulation/animal_care.hpp"
#include "simulation/entt.hpp"
#include "simulation/sim_world.hpp"
#include "units/units.hpp"

#include <optional>
#include <span>

namespace rct3::simulation
{

    /// A viewing gallery, beside its Attraction of kind Gallery.
    struct Gallery
    {
        std::optional<i32> enclosure { };  ///< The one it looks into
        units::Share excitement { };       ///< As its enclosure last rated
        u32 windows = 0;                   ///< Guests who watch at once
        glm::vec2 window { 0.0f };         ///< From the middle of its entrance tile towards the gallery, one long
        units::Share animals_health { 1 }; ///< Of the animals seen through it, on average
        u32 watched = 0;                   ///< Guests who have watched at it since the park was loaded
        u32 pleased = 0;                   ///< Of those, the ones who liked what they saw
    };

    /// What an enclosure has for those who come to look at it.
    struct EnclosureLook
    {
        EnclosureSite site { };
        i32 animals = 0;           ///< Out of their crates
        units::Share health { 1 }; ///< Theirs, on average
        f32 room_wanted = 0;       ///< Tiles they want between them; 0 when not known
    };

    /// Values for galleries. Chosen by eye: each one picked to read well in play, none from a source.
    struct GalleryRates
    {
        f32 roomy = 120;                      ///< Tiles past which a bigger enclosure looks no better
        f32 watery = 0.15f;                   ///< Of its tiles under water, past which more water looks no better
        i32 lively = 4;                       ///< Animals past which more look no livelier
        units::Share size_weight { 0.3f };    ///< Of the habitat's look, its size
        units::Share plants_weight { 0.25f }; ///< Its plants
        units::Share water_weight { 0.15f };  ///< Its water
        units::Share ground_weight { 0.1f };  ///< How level its ground is
        units::Share room_weight { 0.2f };    ///< Its room for the animals in it
        units::Share sickly { 0.6f };         ///< Of the look lost to animals at no health
        u32 windows_per_panel = 3;            ///< Guests watching at each panel of a gallery
        units::Share any_animals { 0.5f };    ///< Of a ride's worth at full excitement, a look at any animals at all
        units::Seconds watch { 25 };          ///< A guest watches the animals for
        units::Share cheer { 0.15f };         ///< Happiness a guest gains watching a gallery of full excitement
        units::Share boring { 0.25f };        ///< Excitement below which guests find the animals boring
        units::Share unwell { 0.4f };         ///< Average health below which guests see the animals look unwell
    };

    inline constexpr GalleryRates kGalleryRates { };

    /// How enclosure `look` rates with those looking in, from nothing to see (0) to all one could wish for (1).
    [[nodiscard]] units::Share enclosure_rating(const EnclosureLook& look);

    /// Rates each enclosure of `sites` (by enclosure) with the animals living in it, and gives each gallery the rating
    /// of the one it looks into as its excitement.
    void rate_galleries(SimWorld& world, std::span<const EnclosureSite> sites);

    /// What watching at `gallery` is worth to a guest: a share of a ride's worth for any animals at all, and on top
    /// what a ride as exciting is worth; nothing with no animals to see.
    [[nodiscard]] units::Money gallery_worth(const Gallery& gallery);

    /// How many guests watch at the gallery `gallery` of `r` now.
    [[nodiscard]] usize watching(const entt::registry& r, entt::entity gallery);

    /// The guest `guest` of `r` starts watching at `gallery`: their happiness follows its excitement, and they think of
    /// what they see. `now` is the game time.
    // shortcut: the thought comes as they start watching, not as they leave; move it once guests step through a
    // gallery's walkway to its windows.
    void watch_animals(entt::registry& r, entt::entity guest, entt::entity gallery, f32 now);

} // namespace rct3::simulation
