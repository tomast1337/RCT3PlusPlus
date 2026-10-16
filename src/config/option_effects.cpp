// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/option_effects.hpp"

#include <array>

namespace rct3::config
{

    namespace
    {

        using O = GameOption;

        constexpr std::array kActedOn = {
            O::Fullscreen,       // The window
            O::Currency,         // Money in the park's windows
            O::DistanceUnits,    // Lengths and speeds
            O::TemperatureUnits, // The weather
            O::MusicVolume,
            O::InterfaceVolume,
            O::CycleMenuMusic,
            O::ShuffleBackgroundMusic,
            O::MenuTrack,
            O::AutoScrollAtEdge, // The park's camera
            O::GraphicsQuality,  // Only which rows the window shows, for now
            O::ShowDetailLevels, // Likewise
            O::Trees3D,          // When a park loads
            O::OffMapTrees,
            O::Clouds,
            O::Sky,
            O::Mountains,
            O::DayNight,
            O::ReflectiveWater, // The water
            O::ReflectBackground,
            O::ReflectTerrain,
            O::ReflectScenery,
            O::ReflectPeople,
            O::WaterBumps,
            O::Refraction,
            O::Shadows,
        };

    } // namespace

    std::span<const GameOption> acted_on_options()
    {
        return kActedOn;
    }

    std::bitset<kGameOptionCount> inert_options()
    {
        std::bitset<kGameOptionCount> inert;
        inert.set();
        for (const GameOption o : kActedOn)
            inert.reset(static_cast<std::size_t>(o));
        return inert;
    }

} // namespace rct3::config
