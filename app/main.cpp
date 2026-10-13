// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer: a window, the interface drawn through the render device and a Dear ImGui overlay. With
// the game installed it first plays the startup logos and the loading screen, then shows the front screen
// built from real skins.
//
//   rct3pp [--renderer NAME] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--frames N] [--write-settings] [--help]
//
//   --renderer NAME   render backend: vulkan (default) or null
//   --no-vsync        present as fast as possible
//   --no-intro        skip the startup logos
//   --scale auto|N    interface scale, instead of the one in the settings file
//   --language NAME   interface language as the game names it, e.g. German or SChinese
//   --widget-demo     a window of every kind of control instead of the front screen
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
#include "platform/window.hpp"
#include "profiling/profiler.hpp"
#include "render/backend.hpp"
#include "scene/front_end_scene.hpp"
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
#include <string>
#include <vector>

namespace
{

    // Command-line help, not log messages.
    void print_usage(std::FILE* out)
    {
        std::fprintf(out, "usage: rct3pp [--renderer vulkan|null] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--frames N] [--write-settings] [--help]\n");
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
            ImVec4(0.55f, 0.55f, 0.55f, 1), ImVec4(0.40f, 0.85f, 0.40f, 1), ImVec4(0.95f, 0.80f, 0.25f, 1), ImVec4(0.95f, 0.35f, 0.35f, 1), ImVec4(1.00f, 0.55f, 0.55f, 1),
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
    bool quit = false;
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
        const auto current = window->size();
        for (const auto& s : window->screen_sizes())
        {
            if (s == current)
                setup.screen_size = setup.screen_sizes.size();
            setup.screen_sizes.push_back({ s.width, s.height });
        }
        setup.keys = key_map;
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
        if (requests.language)
        {
            settings.set(config::Setting::Language, *requests.language);
            save_settings();
            rct3::scene::destroy_front_end_scene(*device, *front);
            front.reset();
            load_widgets(true);
        }
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
                    if (!(front && rct3::scene::front_end_waiting_for_keys(*front)))
                    {
                        auto translated = key_map.translate(keys);
                        keys = std::move(translated.keyboard);
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
                        draw_profiler_window(*device);
                        draw_log_window();
                        ImGui::ShowDemoWindow();
                    }
                    if (phase == Phase::Running && (widgets || front))
                    {
                        // The developer windows come first: where they want the pointer, the game does not get it.
                        const auto p = window->pointer();
                        const rct3::ui::PointerState pointer { { p.x, p.y }, p.left, !ImGui::GetIO().WantCaptureMouse, { p.wheel_x, p.wheel_y } };
                        // Likewise keys, while a developer window has the keyboard.
                        const rct3::ui::KeyboardState no_keys;
                        const auto& keyboard = ImGui::GetIO().WantCaptureKeyboard ? no_keys : keys;
                        if (widgets)
                        {
                            rct3::scene::update_widget_demo(*device, *widgets, pointer, keyboard, static_cast<float>(delta));
                            if (const auto copied = widgets->desktop.take_clipboard())
                                window->set_clipboard(*copied);
                        }
                        else
                        {
                            rct3::scene::update_front_end_scene(*device, *front, pointer, keyboard, static_cast<float>(delta));
                            if (const auto copied = front->live->desktop.take_clipboard())
                                window->set_clipboard(*copied);
                            follow_front_end();
                        }
                    }
                });
                prof::section("Draw", [&] {
                    device->begin_frame();
                    device->begin_pass({ .clear_color = { 0.05f, 0.06f, 0.08f, 1.0f } });
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
                        if (widgets)
                            rct3::scene::draw_widget_demo(*device, *widgets);
                        else if (front)
                            rct3::scene::draw_front_end_scene(*device, *front);
                        else
                            rct3::scene::draw_ui_demo(*device, *ui_demo, now);
                    }
                    device->draw_ui();
                    device->end_pass();
                    device->end_frame();
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

    if (startup)
        rct3::scene::destroy_startup_screen(*device, *startup);
    if (widgets)
        rct3::scene::destroy_widget_demo(*device, *widgets);
    if (front)
        rct3::scene::destroy_front_end_scene(*device, *front);
    rct3::scene::destroy_ui_demo(*device, *ui_demo);
    device->shutdown();
    return status;
}
