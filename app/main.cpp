// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer: a window, the interface drawn through the render device and a Dear ImGui overlay. With
// the game installed it first plays the startup logos and the loading screen, then shows the front screen
// built from real skins, with the menu music and the buttons' clicks.
//
//   rct3pp [--renderer NAME] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--no-audio] [--seed N] [--frames N] [--write-settings] [--help]
//
//   --renderer NAME   render backend: vulkan (default) or null
//   --no-vsync        present as fast as possible
//   --no-intro        skip the startup logos
//   --scale auto|N    interface scale, instead of the one in the settings file
//   --language NAME   interface language as the game names it, e.g. German or SChinese
//   --widget-demo     a window of every kind of control instead of the front screen
//   --no-audio        no sound at all
//   --seed N          the seed of every park's random numbers, decimal or 0x hex, so a run repeats; by
//                     default each park gets a fresh one (random/entropy.hpp), shown in the log
//   --frames N        exit after N frames (smoke test)
//   --write-settings  write the settings file, with any settings given here, and exit
//   --help            this text, the user's data folder (config/user_data.hpp), the settings
//                     (config/settings.hpp) and the environment variables read (config/environment.hpp)
#include "config/environment.hpp"
#include "config/game_install.hpp"
#include "config/game_options.hpp"
#include "config/settings.hpp"
#include "config/user_data.hpp"
#include "imgui.h"
#include "logging/log.hpp"
#include "math/hex_colour.hpp"
#include "platform/window.hpp"
#include "profiling/profiler.hpp"
#include "random/entropy.hpp"
#include "random/generator.hpp"
#include "render/backend.hpp"
#include "scene/front_end_scene.hpp"
#include "scene/menu_audio.hpp"
#include "scene/park_loading.hpp"
#include "scene/park_scene.hpp"
#include "scene/startup_screen.hpp"
#include "scene/ui_demo.hpp"
#include "scene/widget_demo.hpp"
#include "ui/key_map.hpp"
#include "ui/language.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace
{

    // Command-line help, not log messages.
    void print_usage(std::FILE* out)
    {
        std::fprintf(out, "usage: rct3pp [--renderer vulkan|null] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--no-audio] [--seed N] [--frames N] [--write-settings] [--help]\n");
    }

    void print_help(const std::optional<rct3::config::UserData>& data)
    {
        print_usage(stdout);
        std::printf("\nthe user's data (%s):\n", data ? data->root().string().c_str() : "no folder known");
        for (const auto& folder : rct3::config::user_folders_table())
            std::printf("  %-20s %.*s\n", (std::string(folder.name) + "/").c_str(), static_cast<int>(folder.description.size()), folder.description.data());
        std::printf("\nsettings (%s):\n", data ? data->file(rct3::config::UserFile::Settings).string().c_str() : "not kept");
        for (const auto& setting : rct3::config::settings_table())
            std::printf("  %-20.*s %.*s\n", static_cast<int>(setting.name.size()), setting.name.data(), static_cast<int>(setting.description.size()), setting.description.data());
        std::printf("\nenvironment:\n");
        for (const auto& var : rct3::config::env_vars())
            std::printf("  %-20.*s %.*s\n", static_cast<int>(var.name.size()), var.name.data(), static_cast<int>(var.description.size()), var.description.data());
    }

    // Any key or click the developer windows did not take skips the logos.
    bool skip_requested()
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool click = !io.WantCaptureMouse && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        const bool key = !io.WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter));
        return click || key;
    }

    void draw_profiler_window(const rct3::render::Device& device)
    {
        ImGui::SetNextWindowPos({ 12, 12 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 460, 260 }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Profiler");
        const rct3::render::FrameStats stats = device.stats();
        ImGui::Text("%.1f fps", static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Text("%s on %s: %u passes, %u draws, %llu primitives", std::string(rct3::render::backend_name(device.backend())).c_str(), std::string(device.device_name()).c_str(), stats.passes, stats.draws, static_cast<unsigned long long>(stats.primitives));
        ImGui::Text("%llu bytes of per-frame vertex data", static_cast<unsigned long long>(stats.transient_bytes));
        if (ImGui::BeginTable("scopes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp))
        {
            ImGui::TableSetupColumn("scope");
            ImGui::TableSetupColumn("last ms");
            ImGui::TableSetupColumn("avg ms");
            ImGui::TableSetupColumn("p95 ms");
            ImGui::TableHeadersRow();
            for (const auto& s : rct3::profiling::stats())
            {
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGui::TextUnformatted(s.name.c_str());
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", s.last);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", s.average);
                ImGui::TableNextColumn();
                ImGui::Text("%.3f", s.p95);
            }
            ImGui::EndTable();
        }
        ImGui::End();
    }

    // Recent log entries, coloured like the console. The ring is polled with a sequence cursor, so each frame
    // only copies what is new.
    /// An ImGui colour from hex text; see math/hex_colour.hpp.
    consteval ImVec4 im_hex(std::string_view text)
    {
        const glm::vec4 c = rct3::unit(rct3::hex(text));
        return { c.x, c.y, c.z, c.w };
    }

    void draw_log_window()
    {
        namespace logging = rct3::logging;
        static std::vector<logging::Entry> shown;
        static std::uint64_t cursor = 0;
        static int minimum = static_cast<int>(logging::Level::Info);
        static bool follow = true;

        for (auto& e : logging::global().entries_since(cursor))
        {
            cursor = e.sequence;
            shown.push_back(std::move(e));
        }
        constexpr std::size_t kKeep = 2000;
        if (shown.size() > kKeep)
            shown.erase(shown.begin(), shown.begin() + static_cast<std::ptrdiff_t>(shown.size() - kKeep));

        ImGui::SetNextWindowPos({ 12, 290 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 760, 240 }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Log");
        static constexpr std::array<const char*, 5> kLevels = { "Debug", "Info", "Warning", "Error", "Critical" };
        ImGui::SetNextItemWidth(110);
        ImGui::Combo("Minimum", &minimum, kLevels.data(), static_cast<int>(kLevels.size()));
        ImGui::SameLine();
        ImGui::Checkbox("Follow", &follow);
        ImGui::SameLine();
        if (ImGui::Button("Clear"))
        {
            shown.clear();
            logging::global().clear();
        }
        ImGui::Separator();

        static constexpr std::array<ImVec4, 5> kColors = {
            im_hex("#8c8c8c"), im_hex("#66d966"), im_hex("#f2cc40"), im_hex("#f25959"), im_hex("#ff8c8c"),
        };
        ImGui::BeginChild("lines", { 0, 0 }, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
        for (const auto& e : shown)
        {
            const int level = static_cast<int>(e.level);
            if (level < minimum)
                continue;
            ImGui::PushStyleColor(ImGuiCol_Text, kColors[static_cast<std::size_t>(level)]);
            ImGui::Text("[%s]", std::string(logging::level_tag(e.level)).c_str());
            ImGui::PopStyleColor();
            ImGui::SameLine();
            ImGui::TextDisabled("%s:", e.category.c_str());
            ImGui::SameLine();
            ImGui::TextUnformatted(e.message.c_str());
        }
        if (follow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f)
            ImGui::SetScrollHereY(1.0f);
        ImGui::EndChild();
        ImGui::End();
    }

} // namespace

int main(int argc, char** argv)
{
    namespace prof = rct3::profiling;
    namespace render = rct3::render;
    namespace config = rct3::config;
    const auto log = rct3::logging::logger("app");
    const auto env = config::Environment::from_process();

    // Everything kept for the user lives in one folder.
    const auto data = config::UserData::locate(env);
    if (!data)
        log.warning("no folder for the user's data known; set {}: settings, options and keys are not kept", config::env_name(config::EnvVar::DataFolder));
    const auto user_file = [&](config::UserFile file) { return data ? std::optional(data->file(file)) : std::nullopt; };
    // Reads one of the user's files; what cannot be read is logged, and the defaults stand in for it.
    const auto load_user_file = [&]<typename T>(config::UserFile file, T& into) {
        const auto path = user_file(file);
        if (!path)
            return;
        std::vector<config::FileProblem> problems;
        if (auto loaded = T::load(*path, &problems))
            into = std::move(*loaded);
        else
            log.warning("{}", loaded.error());
        for (const auto& problem : problems)
            log.warning("{}: {}", path->string(), problem.message);
    };

    const auto settings_file = user_file(config::UserFile::Settings);
    config::Settings settings;
    load_user_file(config::UserFile::Settings, settings);

    // Command-line options that are also settings go into `settings`, so --write-settings keeps them.
    int max_frames = -1;
    render::Backend backend = render::Backend::Vulkan;
    bool vsync = true;
    bool write_settings = false;
    bool widget_demo = false;
    bool sound = true;
    std::optional<std::uint64_t> seed;
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            max_frames = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--renderer") == 0 && i + 1 < argc)
        {
            const auto parsed = render::parse_backend(argv[++i]);
            if (!parsed)
            {
                print_usage(stderr);
                return 2;
            }
            backend = *parsed;
        }
        else if (std::strcmp(argv[i], "--no-vsync") == 0)
        {
            vsync = false;
        }
        else if (std::strcmp(argv[i], "--no-intro") == 0)
        {
            settings.set(config::Setting::SkipIntro, "yes");
        }
        else if (std::strcmp(argv[i], "--scale") == 0 && i + 1 < argc)
        {
            const char* value = argv[++i];
            if (const auto parsed = config::parse_interface_scale(value); !parsed)
            {
                std::fprintf(stderr, "%s\n", parsed.error().c_str());
                return 2;
            }
            settings.set(config::Setting::InterfaceScale, value);
        }
        else if (std::strcmp(argv[i], "--language") == 0 && i + 1 < argc)
        {
            settings.set(config::Setting::Language, argv[++i]);
        }
        else if (std::strcmp(argv[i], "--widget-demo") == 0)
        {
            widget_demo = true;
        }
        else if (std::strcmp(argv[i], "--no-audio") == 0)
        {
            sound = false;
        }
        else if (std::strcmp(argv[i], "--seed") == 0 && i + 1 < argc)
        {
            const auto parsed = rct3::random::parse_seed(argv[++i]);
            if (!parsed)
            {
                std::fprintf(stderr, "%s\n", parsed.error().c_str());
                return 2;
            }
            seed = *parsed;
        }
        else if (std::strcmp(argv[i], "--write-settings") == 0)
        {
            write_settings = true;
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            print_help(data);
            return 0;
        }
        else
        {
            print_usage(stderr);
            return 2;
        }
    }

    if (write_settings)
    {
        if (!settings_file)
        {
            std::fprintf(stderr, "no folder for the user's data known; set %.*s\n", static_cast<int>(config::env_name(config::EnvVar::DataFolder).size()), config::env_name(config::EnvVar::DataFolder).data());
            return 1;
        }
        if (auto saved = settings.save(*settings_file); !saved)
        {
            std::fprintf(stderr, "%s\n", saved.error().c_str());
            return 1;
        }
        std::printf("wrote %s\n", settings_file->string().c_str());
        return 0;
    }

    const bool intro = !settings.flag(config::Setting::SkipIntro);
    std::optional<float> scale;
    if (const auto parsed = config::parse_interface_scale(settings.get(config::Setting::InterfaceScale).value_or(std::string_view { })))
        scale = *parsed;
    else
        log.warning("settings: {}", parsed.error());

    // The keys the user set for the interface's commands.
    const auto keys_file = user_file(config::UserFile::Keys);
    rct3::ui::KeyMap key_map;
    load_user_file(config::UserFile::Keys, key_map);
    // The developer windows start hidden; a command shows and hides them.
    bool developer_windows = false;

    // Only the startup screens and widget demo use the game so far, so a missing install is not fatal for now.
    const auto install = config::find_game_install(env, settings);
    if (install)
        log.info("game install: {}", install->root().string());
    else
        log.warning("{}", config::describe(install.error()));

    auto window = rct3::platform::Window::open({ }, backend);
    if (!window)
    {
        log.critical("{}", window.error());
        return 1;
    }

    const auto device = render::make_device(backend);
    // Behind everything that does not cover the window.
    constexpr render::Color kBackground = render::hex("#0d0f14");
    const render::DeviceConfig device_config {
        .window = window->handle(),
        .vsync = vsync,
        .validation = env.flag(config::EnvVar::Validation),
    };
    if (auto ready = device->init(device_config); !ready)
    {
        log.critical("{} renderer: {}", render::backend_name(backend), ready.error());
        return 1;
    }
    log.info("rendering with {} on {}", render::backend_name(backend), device->device_name());

    auto ui_demo = rct3::scene::create_ui_demo(*device);
    if (!ui_demo)
    {
        log.critical("ui demo: {}", ui_demo.error());
        return 1;
    }
    // With the game installed: logos, then the loading screen while the skins load, then the front screen
    // (or the widget demo) in place of the generated demo.
    enum class Phase : std::uint8_t
    {
        Intro,
        Loading,
        Running,
    };
    Phase phase = Phase::Running;
    std::optional<rct3::scene::StartupScreen> startup;
    std::optional<rct3::scene::WidgetDemo> widgets;
    std::optional<rct3::scene::FrontEndScene> front;
    std::optional<rct3::scene::ParkScene> park;        ///< Played over the front end, which lends it the interface
    std::optional<std::filesystem::path> park_to_load; ///< Asked for this frame; its loading screen shows first
    bool quit = false;
    // The menus' sound starts with the first front screen and goes on through language changes. The options it
    // reads are the front end's once there is one; until then, those in the file.
    std::optional<rct3::audio::AudioEngine> audio;
    std::optional<rct3::scene::MenuAudio> menu_audio;
    config::GameOptions audio_options;
    const auto start_audio = [&] {
        auto engine = rct3::audio::AudioEngine::create();
        if (!engine)
        {
            log.warning("audio: {}", rct3::audio::to_string(engine.error()));
            return;
        }
        audio.emplace(std::move(*engine));
        const auto options_file = user_file(config::UserFile::GameOptions);
        bool keep = options_file.has_value();
        if (options_file)
        {
            if (auto loaded = config::GameOptions::load(*options_file))
                audio_options = std::move(*loaded);
            else
                keep = false; // The front end reports it; not written over
        }
        menu_audio = rct3::scene::start_menu_audio(*audio, *install, audio_options, std::random_device { }());
        // The menu music's cycle moved on.
        if (keep)
        {
            if (auto saved = audio_options.save(*options_file); !saved)
                log.warning("options: {}", saved.error());
        }
    };
    if (install)
    {
        auto created = rct3::scene::create_startup_screen(*device, *install, { });
        if (created)
        {
            startup = std::move(*created);
            startup->scale = scale;
            phase = intro ? Phase::Intro : Phase::Loading;
        }
        else
        {
            log.warning("startup screen: {}", created.error());
        }
    }
    const auto load_widgets = [&](bool reload) {
        if (sound && !reload && !audio)
            start_audio();
        // The language set, else the locale's, of those installed.
        const auto gui = install->find("gui");
        const auto installed = gui ? rct3::ui::installed_languages(*gui) : std::vector<std::string> { };
        std::string_view locale;
        for (const config::EnvVar var : { config::EnvVar::LcAll, config::EnvVar::LcMessages, config::EnvVar::Lang })
        {
            if (const auto value = env.get(var); value && locale.empty())
                locale = *value;
        }
        const std::string_view wanted = settings.get(config::Setting::Language).value_or(std::string_view { });
        const std::string language = rct3::ui::choose_language(installed, wanted, locale);
        if (!wanted.empty() && !rct3::ui::find_language(wanted))
            log.warning("language {}: not a language the game knows", wanted);
        log.info("language {}", language);
        const auto fallback_font = env.path(config::EnvVar::FallbackFont).value_or(rct3::scene::kDefaultFallbackFont);
        if (widget_demo)
        {
            auto loaded = rct3::scene::create_widget_demo(*device, *install, language, fallback_font);
            if (loaded)
            {
                widgets = std::move(*loaded);
                widgets->scale = scale;
            }
            else
                log.warning("skins: {}", loaded.error());
            return;
        }
        rct3::scene::FrontEndSceneSetup setup;
        setup.languages = installed;
        setup.language = language;
        setup.fallback_font = fallback_font;
        setup.options_file = user_file(config::UserFile::GameOptions);
        setup.data = data;
        const auto current = window->size();
        for (const auto& s : window->screen_sizes())
        {
            if (s == current)
                setup.screen_size = setup.screen_sizes.size();
            setup.screen_sizes.push_back({ s.width, s.height });
        }
        setup.keys = key_map;
        // The game's pictures, then the user's screenshots, shuffled together.
        if (const auto pictures = install->find("Slideshow/Default"))
            setup.picture_folders.push_back(*pictures);
        if (data)
            setup.picture_folders.push_back(data->folder(config::UserFolder::Screenshots));
        setup.picture_seed = std::random_device { }();
        setup.reload = reload;
        auto loaded = rct3::scene::create_front_end_scene(*device, *install, std::move(setup));
        if (loaded)
        {
            front = std::move(*loaded);
            front->scale = scale;
        }
        else
            log.warning("front end: {}", loaded.error());
    };
    const auto save_settings = [&] {
        if (!settings_file)
            return;
        if (auto saved = settings.save(*settings_file); !saved)
            log.warning("settings: {}", saved.error());
    };
    // What the front end asks of the window, and a new language, which loads it again with its options open.
    const auto follow_front_end = [&] {
        const auto requests = rct3::scene::take_front_end_requests(*front);
        quit = quit || requests.quit;
        if (requests.fullscreen)
            window->set_fullscreen(*requests.fullscreen);
        if (requests.screen_size)
        {
            const auto sizes = window->screen_sizes();
            if (*requests.screen_size < sizes.size())
                window->set_size(sizes[*requests.screen_size]);
        }
        if (requests.keys)
        {
            key_map = std::move(*requests.keys);
            if (keys_file)
            {
                if (auto saved = key_map.save(*keys_file); !saved)
                    log.warning("keys: {}", saved.error());
            }
        }
        if (requests.tutorial)
            park_to_load = std::move(requests.tutorial);
        if (requests.language)
        {
            settings.set(config::Setting::Language, *requests.language);
            save_settings();
            rct3::scene::destroy_front_end_scene(*device, *front);
            front.reset();
            load_widgets(true);
        }
    };
    // Loads a park while its loading screen shows, as the game does: on this thread, the screen drawn again
    // as each stage begins. The menus' music goes on meanwhile; the park quiets it once it shows.
    const auto load_park = [&](const std::filesystem::path& path) {
        const prof::Scope zone("LoadingScreen");
        auto shown = rct3::scene::ParkLoadStage::ReadFile; // The frame before loading began drew it
        const auto report = [&](rct3::scene::ParkLoadStage stage) {
            window->poll_events();
            if (audio && menu_audio)
                rct3::scene::update_menu_audio(*audio, *menu_audio, front->live->options, { });
            if (stage == shown)
                return;
            shown = stage;
            device->begin_frame();
            device->begin_pass({ .clear_color = kBackground });
            rct3::scene::draw_park_loading(*device, front->assets, front->list, stage);
            device->end_pass();
            device->end_frame();
        };
        auto loaded = rct3::scene::create_park_scene(*device, *install, *front->assets.strings, front->live->options, path, seed.value_or(rct3::random::fresh_seed()), report);
        if (loaded)
        {
            park = std::move(*loaded);
            park->scale = scale;
        }
        else
            log.warning("{}: {}", path.string(), loaded.error());
    };
    if (install && !startup)
        load_widgets(false);

    int status = 0;
    try
    {
        prof::register_thread("Main");
        double last_time = window->time();
        for (int frame = 0; !window->should_close() && !quit && frame != max_frames; ++frame)
        {
            window->poll_events();

            const double now = window->time();
            const double delta = now - last_time;
            prof::update(delta);
            last_time = now;

            prof::section("Frame", [&] {
                prof::section("Ui", [&] {
                    device->ui_new_frame();
                    window->ui_new_frame();
                    ImGui::NewFrame();
                    if (phase == Phase::Intro)
                    {
                        startup->sequence.update(static_cast<float>(delta), skip_requested());
                        if (startup->sequence.finished())
                            phase = Phase::Loading;
                    }
                    // The keys as the commands they are bound to, unless the interface is waiting for keys
                    // to bind: it then gets them as pressed, and they do nothing else.
                    rct3::ui::KeyboardState keys = window->keyboard();
                    std::vector<rct3::ui::PlayCommand> play_commands;
                    if (!(front && rct3::scene::front_end_waiting_for_keys(*front)))
                    {
                        auto translated = key_map.translate(keys);
                        keys = std::move(translated.keyboard);
                        play_commands = std::move(translated.play);
                        const auto toggles = std::ranges::count(translated.commands, rct3::ui::Command::DeveloperWindows);
                        developer_windows = developer_windows != (toggles % 2 == 1);
                        for (auto& k : keys.presses)
                        {
                            if (k.key == rct3::ui::Key::Paste)
                                k.text = window->clipboard();
                        }
                    }
                    if (phase == Phase::Running && developer_windows)
                    {
                        const prof::Scope zone("DeveloperWindows");
                        draw_profiler_window(*device);
                        draw_log_window();
                        ImGui::ShowDemoWindow();
                    }
                    if (phase == Phase::Running && park && front)
                    {
                        const auto p = window->pointer();
                        rct3::scene::ParkInput input;
                        input.pointer = { { p.x, p.y }, p.left, !ImGui::GetIO().WantCaptureMouse, { p.wheel_x, p.wheel_y } };
                        input.right = p.right && input.pointer.present;
                        input.in_window = p.inside && input.pointer.present;
                        input.middle = p.middle && input.pointer.present;
                        if (!ImGui::GetIO().WantCaptureKeyboard)
                        {
                            input.left_key = window->held(rct3::ui::Key::Left);
                            input.right_key = window->held(rct3::ui::Key::Right);
                            input.up_key = window->held(rct3::ui::Key::Up);
                            input.down_key = window->held(rct3::ui::Key::Down);
                            input.zoom_in_key = window->held(rct3::ui::Key::PageUp);
                            input.zoom_out_key = window->held(rct3::ui::Key::PageDown);
                            input.commands = play_commands;
                        }
                        const rct3::ui::KeyboardState no_keys;
                        prof::section("Park", [&] { rct3::scene::update_park_scene(*device, *park, front->assets, front->live->options, input, ImGui::GetIO().WantCaptureKeyboard ? no_keys : keys, static_cast<float>(delta)); });
                        auto sounds = park->desktop->take_sounds();
                        if (rct3::scene::take_park_alert(*park))
                            sounds.push_back(rct3::ui::InterfaceSound::Message);
                        if (audio && menu_audio)
                            prof::section("Audio", [&] { rct3::scene::update_menu_audio(*audio, *menu_audio, front->live->options, sounds, true); });
                        if (rct3::scene::take_park_leave(*park))
                        {
                            const prof::Scope zone("LeavePark");
                            rct3::scene::destroy_park_scene(*device, *park);
                            park.reset();
                        }
                    }
                    else if (phase == Phase::Running && (widgets || front))
                    {
                        // The developer windows come first: where they want the pointer, the game does not get it.
                        const auto p = window->pointer();
                        const rct3::ui::PointerState pointer { { p.x, p.y }, p.left, !ImGui::GetIO().WantCaptureMouse, { p.wheel_x, p.wheel_y } };
                        // Likewise keys, while a developer window has the keyboard.
                        const rct3::ui::KeyboardState no_keys;
                        const auto& keyboard = ImGui::GetIO().WantCaptureKeyboard ? no_keys : keys;
                        std::vector<rct3::ui::InterfaceSound> sounds;
                        if (widgets)
                        {
                            rct3::scene::update_widget_demo(*device, *widgets, pointer, keyboard, static_cast<float>(delta));
                            if (const auto copied = widgets->desktop.take_clipboard())
                                window->set_clipboard(*copied);
                            sounds = widgets->desktop.take_sounds();
                        }
                        else
                        {
                            prof::section("FrontEnd", [&] { rct3::scene::update_front_end_scene(*device, *front, pointer, keyboard, static_cast<float>(delta)); });
                            if (const auto copied = front->live->desktop.take_clipboard())
                                window->set_clipboard(*copied);
                            sounds = front->live->desktop.take_sounds();
                            prof::section("FollowFrontEnd", [&] { follow_front_end(); });
                        }
                        if (audio && menu_audio)
                            prof::section("Audio", [&] { rct3::scene::update_menu_audio(*audio, *menu_audio, front ? front->live->options : audio_options, sounds); });
                    }
                    // The system's pointer for what it is over or doing: the developer windows' while they have
                    // it, else the park's or the interface's. The game draws none of its own.
                    auto cursor = rct3::ui::CursorKind::Arrow;
                    if (ImGui::GetIO().WantCaptureMouse)
                        cursor = rct3::platform::Window::developer_cursor();
                    else if (phase == Phase::Running && park && front)
                        cursor = rct3::scene::park_scene_cursor(*park);
                    else if (phase == Phase::Running && widgets)
                        cursor = widgets->desktop.cursor().value_or(cursor);
                    else if (phase == Phase::Running && front)
                        cursor = front->live->desktop.cursor().value_or(cursor);
                    window->set_cursor(cursor);
                });
                prof::section("Draw", [&] {
                    prof::section("BeginFrame", [&] { device->begin_frame(); });
                    device->begin_pass({ .clear_color = park ? rct3::scene::park_background(*park) : kBackground });
                    if (phase == Phase::Intro)
                    {
                        rct3::scene::draw_startup_logo(*device, *startup);
                    }
                    else if (phase == Phase::Loading)
                    {
                        rct3::scene::draw_loading_screen(*device, *startup);
                    }
                    else
                    {
                        const prof::Scope zone("Scene");
                        if (widgets)
                            rct3::scene::draw_widget_demo(*device, *widgets);
                        else if (park && front)
                            rct3::scene::draw_park_scene(*device, *park, front->assets);
                        else if (park_to_load && front)
                            rct3::scene::draw_park_loading(*device, front->assets, front->list, rct3::scene::ParkLoadStage::ReadFile);
                        else if (front)
                            rct3::scene::draw_front_end_scene(*device, *front);
                        else
                            rct3::scene::draw_ui_demo(*device, *ui_demo, now);
                    }
                    prof::section("DeveloperUi", [&] { device->draw_ui(); });
                    prof::section("Submit", [&] {
                        device->end_pass();
                        device->end_frame();
                    });
                    if (park_to_load && front)
                    {
                        load_park(*std::exchange(park_to_load, std::nullopt));
                        // The park's time starts when it shows, not when it was asked for.
                        last_time = window->time();
                    }
                    // The loading screen is on screen now; load while it shows, as the game does.
                    if (phase == Phase::Loading)
                    {
                        load_widgets(false);
                        rct3::scene::destroy_startup_screen(*device, *startup);
                        startup.reset();
                        phase = Phase::Running;
                    }
                });
            });
            prof::capture_frame();
        }
    }
    catch (const std::exception& e)
    {
        // A failing graphics call after init means a lost device or a driver fault.
        log.critical("{}", e.what());
        status = 1;
    }

    if (audio && menu_audio)
        rct3::scene::stop_menu_audio(*audio, *menu_audio);
    if (startup)
        rct3::scene::destroy_startup_screen(*device, *startup);
    if (widgets)
        rct3::scene::destroy_widget_demo(*device, *widgets);
    if (park)
        rct3::scene::destroy_park_scene(*device, *park);
    if (front)
        rct3::scene::destroy_front_end_scene(*device, *front);
    rct3::scene::destroy_ui_demo(*device, *ui_demo);
    device->shutdown();
    return status;
}
