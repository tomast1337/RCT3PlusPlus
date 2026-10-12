// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer: a window, a test scene drawn through the render device and a Dear ImGui overlay. With
// the game installed it first plays the startup logos and the loading screen, then shows a window built from real skins.
//
//   rct3pp [--renderer NAME] [--no-vsync] [--no-intro] [--frames N] [--help]
//
//   --renderer NAME   render backend: vulkan (default) or null
//   --no-vsync        present as fast as possible
//   --no-intro        skip the startup logos
//   --frames N        exit after N frames (smoke test)
//   --help            this text and the environment variables read (config/environment.hpp)
#include "config/environment.hpp"
#include "config/game_install.hpp"
#include "imgui.h"
#include "logging/log.hpp"
#include "platform/window.hpp"
#include "profiling/profiler.hpp"
#include "render/backend.hpp"
#include "scene/startup_screen.hpp"
#include "scene/test_scene.hpp"
#include "scene/ui_demo.hpp"
#include "scene/widget_demo.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <optional>
#include <string>
#include <vector>

namespace
{

    // Command-line help, not log messages.
    void print_usage(std::FILE* out)
    {
        std::fprintf(out, "usage: rct3pp [--renderer vulkan|null] [--no-vsync] [--no-intro] [--frames N] [--help]\n");
    }

    void print_help()
    {
        print_usage(stdout);
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

    int max_frames = -1;
    render::Backend backend = render::Backend::Vulkan;
    bool vsync = true;
    bool intro = true;
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
            intro = false;
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            print_help();
            return 0;
        }
        else
        {
            print_usage(stderr);
            return 2;
        }
    }

    // Only the startup screens and widget demo use the game so far, so a missing install is not fatal for now.
    const auto install = config::find_game_install(env);
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

    auto scene = rct3::scene::create_test_scene(*device);
    if (!scene)
    {
        log.critical("test scene: {}", scene.error());
        return 1;
    }
    auto ui_demo = rct3::scene::create_ui_demo(*device);
    if (!ui_demo)
    {
        log.critical("ui demo: {}", ui_demo.error());
        return 1;
    }
    // With the game installed: logos, then the loading screen while the skins load, then the widget demo
    // in place of the generated demo.
    enum class Phase : std::uint8_t
    {
        Intro,
        Loading,
        Running,
    };
    Phase phase = Phase::Running;
    std::optional<rct3::scene::StartupScreen> startup;
    std::optional<rct3::scene::WidgetDemo> widgets;
    if (install)
    {
        auto created = rct3::scene::create_startup_screen(*device, *install, { });
        if (created)
        {
            startup = std::move(*created);
            phase = intro ? Phase::Intro : Phase::Loading;
        }
        else
        {
            log.warning("startup screen: {}", created.error());
        }
    }
    const auto load_widgets = [&] {
        auto loaded = rct3::scene::create_widget_demo(*device, *install);
        if (loaded)
            widgets = std::move(*loaded);
        else
            log.warning("skins: {}", loaded.error());
    };
    if (install && !startup)
        load_widgets();

    int status = 0;
    try
    {
        prof::register_thread("Main");
        double last_time = window->time();
        for (int frame = 0; !window->should_close() && frame != max_frames; ++frame)
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
                    if (phase == Phase::Running)
                    {
                        draw_profiler_window(*device);
                        draw_log_window();
                        ImGui::ShowDemoWindow();
                    }
                    if (phase == Phase::Running && widgets)
                    {
                        // The developer windows come first: where they want the pointer, the game does not get it.
                        const auto p = window->pointer();
                        const rct3::ui::PointerState pointer { { p.x, p.y }, p.left, !ImGui::GetIO().WantCaptureMouse };
                        rct3::scene::update_widget_demo(*device, *widgets, pointer);
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
                        rct3::scene::draw_test_scene(*device, *scene, now);
                        if (widgets)
                            rct3::scene::draw_widget_demo(*device, *widgets);
                        else
                            rct3::scene::draw_ui_demo(*device, *ui_demo, now);
                    }
                    device->draw_ui();
                    device->end_pass();
                    device->end_frame();
                    // The loading screen is on screen now; load while it shows, as the game does.
                    if (phase == Phase::Loading)
                    {
                        load_widgets();
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
    rct3::scene::destroy_ui_demo(*device, *ui_demo);
    rct3::scene::destroy_test_scene(*device, *scene);
    device->shutdown();
    return status;
}
