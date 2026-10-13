// SPDX-License-Identifier: GPL-3.0-or-later
#include "config/game_options.hpp"

#include "config/settings.hpp"
#include "config/text_file.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>

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
            { O::GameVolume, "game_volume", 0, 1, kAny, 0.8f },
            { O::MusicVolume, "music_volume", 0, 1, kAny, 0.6f },
            { O::InterfaceVolume, "interface_volume", 0, 1, kAny, 0.8f },
            { O::ImportMyMusic, "import_my_music", 0, 1, 1, 0 },
            { O::CycleMenuMusic, "cycle_menu_music", 0, 1, 1, 1 },

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

            { O::GraphicsQuality, "graphics_quality", 0, 5, 1, 2 },
            { O::AntiAliasing, "anti_aliasing", 0, 8, 1, 0 },
            { O::Shadows, "shadows", 0, 2, 1, 2 },
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
            { O::CompressTextures, "compress_textures", 0, 1, 1, 0 },
            { O::AggressiveDepthBias, "aggressive_depth_bias", 0, 1, 1, 0 },
            { O::TextureDetail, "texture_detail", 0, 1, kAny, 1 },
            { O::Trees3D, "trees_3d", 0, 1, 1, 1 },
            { O::OffMapTrees, "off_map_trees", 0, 1, 1, 1 },
            { O::SimpleTerrain, "simple_terrain", 0, 1, 1, 0 },
            { O::ReflectiveWater, "reflective_water", 0, 1, 1, 1 },
            { O::ReflectTerrain, "reflect_terrain", 0, 1, 1, 1 },
            { O::ReflectScenery, "reflect_scenery", 0, 1, 1, 1 },
            { O::ReflectPeople, "reflect_people", 0, 1, 1, 0 },
            { O::ReflectAnimals, "reflect_animals", 0, 1, 1, 0 },
            { O::ReflectParticles, "reflect_particles", 0, 1, 1, 0 },
            { O::WaterBumps, "water_bumps", 0, 1, 1, 1 },
            { O::FlowingWaterEnvironment, "flowing_water_environment", 0, 1, 1, 1 },
            { O::Clouds, "clouds", 0, 1, 1, 1 },
            { O::Mountains, "mountains", 0, 1, 1, 1 },
            { O::DayNight, "day_night", 0, 2, 1, 1 },
            { O::Bloom, "bloom", 0, 1, 1, 1 },
            { O::Refraction, "refraction", 0, 1, 1, 1 },
            { O::WaterDroplets, "water_droplets", 0, 1, 1, 1 },
            { O::LaserOcclusion, "laser_occlusion", 0, 1, 1, 1 },
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

        constexpr std::string_view kFileName = "options.ini";

        constexpr std::size_t index(GameOption option)
        {
            return static_cast<std::size_t>(option);
        }

        constexpr bool is_space(char c)
        {
            return c == ' ' || c == '\t' || c == '\r';
        }

        std::string_view trimmed(std::string_view s)
        {
            while (!s.empty() && is_space(s.front()))
                s.remove_prefix(1);
            while (!s.empty() && is_space(s.back()))
                s.remove_suffix(1);
            return s;
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

    GameOptions GameOptions::parse(std::string_view text, std::vector<GameOptionsProblem>* problems)
    {
        GameOptions options;
        const auto report = [&](std::size_t line, std::string message) {
            if (problems != nullptr)
                problems->push_back({ line, std::move(message) });
        };
        std::size_t number = 0;
        while (!text.empty())
        {
            const std::size_t end = text.find('\n');
            const std::string_view line = trimmed(text.substr(0, end));
            text = end == std::string_view::npos ? std::string_view { } : text.substr(end + 1);
            ++number;
            if (line.empty() || line.front() == '#')
                continue;
            const std::size_t equals = line.find('=');
            if (equals == std::string_view::npos)
            {
                report(number, "expected name = value");
                continue;
            }
            const std::string_view name = trimmed(line.substr(0, equals));
            const std::string_view value = trimmed(line.substr(equals + 1));
            const GameOptionInfo* info = find_option(name);
            if (info == nullptr)
            {
                report(number, "unknown option " + std::string(name));
                continue;
            }
            float number_value = 0;
            const auto [rest, ec] = std::from_chars(value.data(), value.data() + value.size(), number_value);
            if (ec != std::errc { } || rest != value.data() + value.size() || !std::isfinite(number_value))
            {
                report(number, std::string(name) + ": not a number");
                continue;
            }
            options.set(info->option, number_value);
        }
        return options;
    }

    std::expected<GameOptions, std::string> GameOptions::load(const fs::path& path, std::vector<GameOptionsProblem>* problems)
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
        std::string out = "# rct3++ game options, as the options dialog sets them. Lines are name = value.\n";
        for (const GameOptionInfo& info : kOptions)
        {
            const float value = values_[index(info.option)];
            if (value == info.default_value)
                continue;
            std::array<char, 32> buffer { };
            const auto written = std::to_chars(buffer.data(), buffer.data() + buffer.size(), value);
            out += info.name;
            out += " = ";
            out.append(buffer.data(), written.ptr);
            out += '\n';
        }
        return out;
    }

    std::expected<void, std::string> GameOptions::save(const fs::path& path) const
    {
        return write_text_file(path, text());
    }

    std::optional<fs::path> game_options_path(const Environment& env)
    {
        return config_file_path(env, kFileName);
    }

} // namespace rct3::config
