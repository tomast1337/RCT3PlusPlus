// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_options.hpp"

#include "config/json_text.hpp"

#include <algorithm>
#include <cmath>
#include <format>

namespace rct3::config
{

    namespace
    {

        namespace fs = std::filesystem;
        using O = GameOption;

        constexpr float kAny = 0; // Step of an option that takes any value in its range

        // Defaults are ours: the middle of the road, everything on that costs little.
        constexpr std::array<GameOptionInfo, kGameOptionCount> kOptions = { {
            { O::Fullscreen, "fullscreen", 0, 1, 1, 0 },
            { O::Currency, "currency", 0, 10, 1, 1 },
            { O::DistanceUnits, "distance_units", 0, 2, 1, 0 },
            { O::TemperatureUnits, "temperature_units", 0, 1, 1, 0 },
            { O::Brightness, "brightness", 0, 1, kAny, 0.5f },
            { O::LiveContent, "live_content", 0, 1, 1, 1 },
            { O::GameVolume, "game_volume", 0, 1, kAny, 0.8f },
            { O::MusicVolume, "music_volume", 0, 1, kAny, 0.6f },
            { O::InterfaceVolume, "interface_volume", 0, 1, kAny, 0.8f },
            { O::ImportMyMusic, "import_my_music", 0, 1, 1, 0 },
            { O::CycleMenuMusic, "cycle_menu_music", 0, 1, 1, 1 },
            { O::ShuffleBackgroundMusic, "shuffle_background_music", 0, 1, 1, 0 },
            { O::MenuTrack, "menu_track", 0, 50, 1, 0 },

            { O::InvertRideCameraY, "invert_ride_camera_y", 0, 1, 1, 0 },
            { O::AllPhotos, "all_photos", 0, 1, 1, 1 },
            { O::PhotoSpotPhotos, "photo_spot_photos", 0, 1, 1, 1 },
            { O::CoasterCameraPhotos, "coaster_camera_photos", 0, 1, 1, 1 },
            { O::AutoScrollAtEdge, "auto_scroll_at_edge", 0, 1, 1, 1 },
            { O::CameraMode, "camera_mode", 0, 3, 1, 3 },
            { O::CameraControls, "camera_controls", 0, 2, 1, 0 },
            { O::CameraIgnoresScenery, "camera_ignores_scenery", 0, 1, 1, 0 },
            { O::CameraFocusOnMiddle, "camera_focus_on_middle", 0, 1, 1, 0 },
            { O::FreeLookSpeed, "free_look_speed", 0, 1, 0.01f, 0.5f },
            { O::IsometricRotation, "isometric_rotation", 0.05f, 5, 0.05f, 1 },
            { O::PathCameraFollow, "path_camera_follow", 0, 1, 1, 1 },

            { O::GraphicsQuality, "graphics_quality", 0, 5, 1, 2 },
            { O::AntiAliasing, "anti_aliasing", 0, 8, 1, 0 },
            { O::Shadows, "shadows", 0, 2, 1, 2 },
            { O::BlendShadows, "blend_shadows", 0, 1, 1, 1 },
            { O::ShowDetailLevels, "show_detail_levels", 0, 1, 1, 0 },
            { O::PeopleDetail, "people_detail", 0, 3, kAny, 2 },
            { O::PeopleDistance, "people_distance", 0, 3, kAny, 2 },
            { O::AnimalDetail, "animal_detail", 0, 3, kAny, 2 },
            { O::AnimalDistance, "animal_distance", 0, 3, kAny, 2 },
            { O::VegetationDetail, "vegetation_detail", 0, 3, kAny, 2 },
            { O::VegetationDistance, "vegetation_distance", 0, 3, kAny, 2 },
            { O::FenceDetail, "fence_detail", 0, 3, kAny, 2 },
            { O::FenceDistance, "fence_distance", 0, 3, kAny, 2 },
            { O::BuildingDetail, "building_detail", 0, 3, kAny, 2 },
            { O::BuildingDistance, "building_distance", 0, 3, kAny, 2 },
            { O::PathDetail, "path_detail", 0, 3, kAny, 2 },
            { O::PathDistance, "path_distance", 0, 3, kAny, 2 },
            { O::SceneryDetail, "scenery_detail", 0, 3, kAny, 2 },
            { O::SceneryDistance, "scenery_distance", 0, 3, kAny, 2 },
            { O::SupportDetail, "support_detail", 0, 3, kAny, 2 },
            { O::SupportDistance, "support_distance", 0, 3, kAny, 2 },
            { O::TrackDetail, "track_detail", 0, 3, kAny, 2 },
            { O::TrackDistance, "track_distance", 0, 3, kAny, 2 },
            { O::RideDetail, "ride_detail", 0, 3, kAny, 2 },
            { O::RideDistance, "ride_distance", 0, 3, kAny, 2 },
            { O::ImposterDistance, "imposter_distance", 0, 1500, 5, 500 },
            { O::BlendDetailLevels, "blend_detail_levels", 0, 1, 1, 1 },
            { O::DetailBlendBand, "detail_blend_band", 0.05f, 0.5f, 0.05f, 0.2f },
            { O::CompressTextures, "compress_textures", 0, 1, 1, 0 },
            { O::AggressiveDepthBias, "aggressive_depth_bias", 0, 1, 1, 0 },
            { O::TextureDetail, "texture_detail", 0, 1, kAny, 1 },
            { O::SurfaceDetail, "surface_detail", 0, 2, 1, 0 },
            { O::Trees3D, "trees_3d", 0, 1, 1, 1 },
            { O::OffMapTrees, "off_map_trees", 0, 1, 1, 1 },
            { O::SimpleTerrain, "simple_terrain", 0, 1, 1, 0 },
            { O::ReflectiveWater, "reflective_water", 0, 1, 1, 1 },
            { O::ReflectBackground, "reflect_background", 0, 1, 1, 1 },
            { O::ReflectTerrain, "reflect_terrain", 0, 1, 1, 1 },
            { O::ReflectScenery, "reflect_scenery", 0, 1, 1, 1 },
            { O::ReflectPeople, "reflect_people", 0, 1, 1, 0 },
            { O::ReflectAnimals, "reflect_animals", 0, 1, 1, 0 },
            { O::ReflectParticles, "reflect_particles", 0, 1, 1, 0 },
            { O::WaterBumps, "water_bumps", 0, 1, 1, 1 },
            { O::FlowingWaterEnvironment, "flowing_water_environment", 0, 1, 1, 1 },
            { O::Clouds, "clouds", 0, 1, 1, 1 },
            { O::Sky, "sky", 0, 1, 1, 1 },
            { O::Mountains, "mountains", 0, 1, 1, 1 },
            { O::DayNight, "day_night", 0, 2, 1, 1 },
            { O::Bloom, "bloom", 0, 1, 1, 1 },
            { O::Refraction, "refraction", 0, 1, 1, 1 },
            { O::WaterDroplets, "water_droplets", 0, 1, 1, 1 },
            { O::LaserOcclusion, "laser_occlusion", 0, 1, 1, 1 },
            { O::AmbientOcclusion, "ambient_occlusion", 0, 1, 1, 1 },
        } };

        // The table is indexed by GameOption, so its order must follow the enum.
        static_assert([] {
            for (std::size_t i = 0; i < kOptions.size(); ++i)
            {
                if (static_cast<std::size_t>(kOptions[i].option) != i)
                    return false;
            }
            return true;
        }());

        constexpr std::string_view kBackgroundMusic = "background_music";

        constexpr std::size_t index(GameOption option)
        {
            return static_cast<std::size_t>(option);
        }

        const GameOptionInfo* find_option(std::string_view name)
        {
            const auto found = std::ranges::find(kOptions, name, &GameOptionInfo::name);
            return found == kOptions.end() ? nullptr : &*found;
        }

        float kept(const GameOptionInfo& info, float value)
        {
            if (info.step > 0)
                value = info.minimum + std::round((value - info.minimum) / info.step) * info.step;
            return std::clamp(value, info.minimum, info.maximum);
        }

    } // namespace

    std::span<const GameOptionInfo> game_options_table()
    {
        return kOptions;
    }

    const GameOptionInfo& game_option_info(GameOption option)
    {
        return kOptions.at(index(option));
    }

    GameOptions::GameOptions()
    {
        for (const GameOptionInfo& info : kOptions)
            values_[index(info.option)] = info.default_value;
    }

    GameOptions GameOptions::parse(std::string_view text, std::optional<std::vector<FileProblem>&> problems)
    {
        GameOptions options;
        const auto json = parse_json_object(text, problems);
        if (!json)
            return options;
        for (const auto& [name, value] : json->items())
        {
            const GameOptionInfo* info = find_option(name);
            if (name == kBackgroundMusic)
            {
                if (value.is_string())
                    options.background_music_ = value.get<std::string>();
                else
                    report(problems, std::format("{}: not a file name", name));
            }
            else if (info == nullptr)
                report(problems, std::format("unknown option {}", name));
            else if (value.is_boolean())
                options.set(info->option, value.get<bool>() ? 1.0f : 0.0f);
            else if (value.is_number() && std::isfinite(value.get<double>()))
                options.set(info->option, value.get<float>());
            else
                report(problems, std::format("{}: not a number", name));
        }
        return options;
    }

    std::expected<GameOptions, std::string> GameOptions::load(const fs::path& path, std::optional<std::vector<FileProblem>&> problems)
    {
        auto text = read_text_file(path);
        if (!text)
            return std::unexpected(text.error());
        return *text ? parse(**text, problems) : GameOptions { };
    }

    float GameOptions::get(GameOption option) const
    {
        return index(option) < kGameOptionCount ? values_[index(option)] : 0;
    }

    void GameOptions::set(GameOption option, float value)
    {
        if (index(option) < kGameOptionCount)
            values_[index(option)] = kept(kOptions[index(option)], value);
    }

    std::string GameOptions::text() const
    {
        Json json = Json::object();
        for (const GameOptionInfo& info : kOptions)
        {
            const float value = values_[index(info.option)];
            if (value == info.default_value)
                continue;
            if (value == std::trunc(value))
                json[info.name] = static_cast<long long>(value);
            else
                json[info.name] = value;
        }
        if (background_music_)
            json[kBackgroundMusic] = *background_music_;
        return json_text(json);
    }

    std::expected<void, std::string> GameOptions::save(const fs::path& path) const
    {
        return write_text_file(path, text());
    }

} // namespace rct3::config
