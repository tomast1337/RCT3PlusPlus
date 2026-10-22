// SPDX-License-Identifier: GPL-3.0-or-later
//
// The options the player changes in the game's options dialog: money and units, sound levels, camera
// behaviour and graphics detail. They are kept apart from the program's settings (settings.hpp), in their
// own file in the user's data folder (user_data.hpp), written whenever the dialog closes.
//
// Every option is a number with a range, registered once in the table in game_options.cpp: on/off options
// are 0 or 1, choices count from 0, levels are any value in their range or whole steps of it. The file is a
// JSON object holding the options that differ from their defaults (true and false read as 1 and 0); values
// it does not understand are reported and skipped. One option is a file name rather than a number: the
// park's music when zoomed out, kept under "background_music".
//
// Many options have no effect yet: they are kept so the dialog shows what the player chose.
#pragma once

#include "config/text_file.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <expected>
#include <filesystem>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace rct3::config
{

    enum class GameOption : std::uint8_t
    {
        // General
        Fullscreen,
        Currency,         ///< Index into the game's currency list
        DistanceUnits,    ///< 0 metric, 1 imperial, 2 SI
        TemperatureUnits, ///< 0 Celsius, 1 Fahrenheit
        Brightness,
        LiveContent, ///< Fetch live media from the internet; takes effect at the next start
        GameVolume,
        MusicVolume,
        InterfaceVolume,
        ImportMyMusic,          ///< Play the player's own music in the park
        CycleMenuMusic,         ///< A different menu track each start
        ShuffleBackgroundMusic, ///< The park's music when zoomed out is picked at random
        MenuTrack,              ///< Where the menu music's cycle stands; not in the dialog (menu_music.hpp)

        // Camera
        InvertRideCameraY,
        AllPhotos,
        PhotoSpotPhotos,
        CoasterCameraPhotos,
        AutoScrollAtEdge,
        CameraMode,     ///< 0 advanced, 1 free look, 2 isometric, 3 normal
        CameraControls, ///< Advanced mode's control scheme
        CameraIgnoresScenery,
        CameraFocusOnMiddle,
        FreeLookSpeed,
        IsometricRotation, ///< How far the pointer moves before the isometric view turns
        PathCameraFollow,  ///< The camera follows the path being laid; the path panel's tab, not in the dialog

        // Graphics
        GraphicsQuality, ///< 0 to 4 from fastest to finest, 5 custom
        AntiAliasing,    ///< 0 none, then each level the device offers
        Shadows,         ///< 0 none, 1 static, 2 dynamic
        BlendShadows,    ///< Shadows move smoothly between drawings from the sun, rather than step
        ShowDetailLevels,
        PeopleDetail,
        PeopleDistance,
        AnimalDetail,
        AnimalDistance,
        VegetationDetail,
        VegetationDistance,
        FenceDetail,
        FenceDistance,
        BuildingDetail,
        BuildingDistance,
        PathDetail,
        PathDistance,
        SceneryDetail,
        SceneryDistance,
        SupportDetail,
        SupportDistance,
        TrackDetail,
        TrackDistance,
        RideDetail,
        RideDistance,
        ImposterDistance, ///< Beyond it, scenery is drawn as flat pictures
        BlendDetailLevels,
        DetailBlendBand, ///< How far before a level's edge objects start blending into the next, as a share of its distance
        CompressTextures,
        AggressiveDepthBias,
        TextureDetail,
        SurfaceDetail, ///< 0 off, 1 fine grain up close, 2 also relief from maps made of the textures
        Trees3D,
        OffMapTrees,
        SimpleTerrain,
        ReflectiveWater,
        ReflectBackground,
        ReflectTerrain,
        ReflectScenery,
        ReflectPeople,
        ReflectAnimals,
        ReflectParticles,
        WaterBumps,
        FlowingWaterEnvironment,
        Clouds,
        Sky, ///< The sky's colours, sun, moon and stars
        Mountains,
        DayNight, ///< 0 instant, 1 blend, 2 cycle
        Bloom,
        LightShafts,      ///< Sunlight seen in the air, with dark gaps where shadows cross it
        AmbientOcclusion, ///< Creases and corners darkened where less of the sky reaches
        Refraction,
        WaterDroplets,
        LaserOcclusion,
        ParticleBudget, ///< At most this many particles alive at once, nearest effects first; 0 none

        Count_, ///< Number of options; not an option
    };

    inline constexpr std::size_t kGameOptionCount = static_cast<std::size_t>(GameOption::Count_);

    struct GameOptionInfo
    {
        GameOption option;
        std::string_view name;
        float minimum = 0;
        float maximum = 1;
        float step = 1; ///< Values are whole steps from the minimum; 0 for any value
        float default_value = 0;
    };

    /// Every option, in GameOption order.
    [[nodiscard]] std::span<const GameOptionInfo> game_options_table();
    [[nodiscard]] const GameOptionInfo& game_option_info(GameOption option);

    /// The detail and draw distance options of one kind of thing, as the graphics page pairs them.
    struct DetailPair
    {
        GameOption detail;
        GameOption distance;
    };
    inline constexpr std::array<DetailPair, 10> kDetailPairs = { {
        { GameOption::PeopleDetail, GameOption::PeopleDistance },
        { GameOption::AnimalDetail, GameOption::AnimalDistance },
        { GameOption::VegetationDetail, GameOption::VegetationDistance },
        { GameOption::FenceDetail, GameOption::FenceDistance },
        { GameOption::BuildingDetail, GameOption::BuildingDistance },
        { GameOption::PathDetail, GameOption::PathDistance },
        { GameOption::SceneryDetail, GameOption::SceneryDistance },
        { GameOption::SupportDetail, GameOption::SupportDistance },
        { GameOption::TrackDetail, GameOption::TrackDistance },
        { GameOption::RideDetail, GameOption::RideDistance },
    } };

    inline constexpr int kCustomQuality = 5;

    class GameOptions
    {
    public:
        /// Every option at its default.
        GameOptions();

        [[nodiscard]] static GameOptions parse(std::string_view text, std::optional<std::vector<FileProblem>&> problems = std::nullopt);
        /// Reads the file at `path`. A file that is not there gives the defaults.
        [[nodiscard]] static std::expected<GameOptions, std::string> load(const std::filesystem::path& path, std::optional<std::vector<FileProblem>&> problems = std::nullopt);

        [[nodiscard]] float get(GameOption option) const;
        [[nodiscard]] bool flag(GameOption option) const
        {
            return get(option) != 0;
        }
        [[nodiscard]] int choice(GameOption option) const
        {
            return static_cast<int>(get(option));
        }
        /// Sets the value, kept in range and snapped to a whole step.
        void set(GameOption option, float value);

        /// The park's music when zoomed out: a file of the game's music folder by name, empty for no music.
        /// Nothing until the player picks one. Ignored while ShuffleBackgroundMusic is on.
        [[nodiscard]] const std::optional<std::string>& background_music() const
        {
            return background_music_;
        }
        void set_background_music(std::optional<std::string> track)
        {
            background_music_ = std::move(track);
        }

        /// The file text: the options that differ from their defaults.
        [[nodiscard]] std::string text() const;
        /// Writes text() to `path`, creating its folder.
        [[nodiscard]] std::expected<void, std::string> save(const std::filesystem::path& path) const;

        friend bool operator==(const GameOptions&, const GameOptions&) = default;

    private:
        std::array<float, kGameOptionCount> values_ { };
        std::optional<std::string> background_music_;
    };

} // namespace rct3::config
