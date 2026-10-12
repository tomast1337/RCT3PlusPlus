// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer: a window, a test scene drawn through the render device and a Dear ImGui overlay.
//
//   rct3pp [--renderer NAME] [--no-vsync] [--frames N]
//
//   --renderer NAME   render backend: vulkan (default) or null
//   --no-vsync        present as fast as possible
//   --frames N        exit after N frames (smoke test)
//
// RCT3_VALIDATION set in the environment enables the graphics API's validation layer.
#include "imgui.h"
#include "logging/log.hpp"
#include "platform/window.hpp"
#include "profiling/profiler.hpp"
#include "render/backend.hpp"
#include "scene/test_scene.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <string>
#include <vector>

namespace
{

    constexpr const char* kValidationEnv = "RCT3_VALIDATION";

    void print_usage()
    {
        std::fprintf(stderr, "usage: rct3pp [--renderer vulkan|null] [--no-vsync] [--frames N]\n"); // command-line help, not a log message
    }

    void draw_profiler_window(const rct3::render::Device& device)
    {
        ImGui::SetNextWindowPos({ 12, 12 }, ImGuiCond_FirstUseEver);
        ImGui::SetNextWindowSize({ 460, 260 }, ImGuiCond_FirstUseEver);
        ImGui::Begin("Profiler");
        const rct3::render::FrameStats stats = device.stats();
        ImGui::Text("%.1f fps", static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Text("%s on %s: %u passes, %u draws, %llu primitives", std::string(rct3::render::backend_name(device.backend())).c_str(), std::string(device.device_name()).c_str(), stats.passes, stats.draws, static_cast<unsigned long long>(stats.primitives));
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
    const auto log = rct3::logging::logger("app");

    int max_frames = -1;
    render::Backend backend = render::Backend::Vulkan;
    bool vsync = true;
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
                print_usage();
                return 2;
            }
            backend = *parsed;
        }
        else if (std::strcmp(argv[i], "--no-vsync") == 0)
        {
            vsync = false;
        }
        else
        {
            print_usage();
            return 2;
        }
    }

    auto window = rct3::platform::Window::open({ }, backend);
    if (!window)
    {
        log.critical("{}", window.error());
        return 1;
    }

    const auto device = render::make_device(backend);
    const render::DeviceConfig config {
        .window = window->handle(),
        .vsync = vsync,
        .validation = std::getenv(kValidationEnv) != nullptr,
    };
    if (auto ready = device->init(config); !ready)
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

    int status = 0;
    try
    {
        prof::register_thread("Main");
        double last_time = window->time();
        for (int frame = 0; !window->should_close() && frame != max_frames; ++frame)
        {
            window->poll_events();

            const double now = window->time();
            prof::update(now - last_time);
            last_time = now;

            prof::section("Frame", [&] {
                prof::section("Ui", [&] {
                    device->ui_new_frame();
                    window->ui_new_frame();
                    ImGui::NewFrame();
                    draw_profiler_window(*device);
                    draw_log_window();
                    ImGui::ShowDemoWindow();
                });
                prof::section("Draw", [&] {
                    device->begin_frame();
                    device->begin_pass({ .clear_color = { 0.05f, 0.06f, 0.08f, 1.0f } });
                    rct3::scene::draw_test_scene(*device, *scene, now);
                    device->draw_ui();
                    device->end_pass();
                    device->end_frame();
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

    rct3::scene::destroy_test_scene(*device, *scene);
    device->shutdown();
    return status;
}
