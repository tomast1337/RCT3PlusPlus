// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer: a window, the interface drawn through the render device and a Dear ImGui overlay. With
// the game installed it first plays the startup logos and the loading screen, then shows the front screen
// built from real skins, with the menu music and the buttons' clicks.
//
//   rct3pp [--renderer NAME] [--headless] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--no-audio] [--audio] [--park FILE] [--no-simulate] [--script FILE] [--seed N] [--frames N] [--seconds N] [--max-fps N] [--profile FILE] [--write-settings] [--help]
//
//   --renderer NAME   render backend: vulkan (default) or null
//   --headless        no window and no display needed: the null renderer, no sound, no vsync, no logos, 60 fps
//   --no-vsync        present as fast as possible
//   --no-intro        skip the startup logos
//   --scale auto|N    interface scale, instead of the one in the settings file
//   --language NAME   interface language as the game names it, e.g. German or SChinese
//   --widget-demo     a window of every kind of control instead of the front screen
//   --no-audio        no sound at all; kept by --write-settings
//   --audio           sound for this run, even when the settings turn it off
//   --park FILE       load this park save straight away
//   --no-simulate     show parks as saved, standing still, without running the simulation
//   --script FILE     run the JavaScript FILE, which plays the game through its pointer (scene/script_host.hpp);
//                     the game exits when it ends, with 1 when it failed
//   --script-report DIR  put the script's screenshots and log in DIR for review
//   --seed N          the seed of every park's random numbers, decimal or 0x hex, so a run repeats; by
//                     default each park gets a fresh one (random/entropy.hpp), shown in the log
//   --frames N        exit after N frames (smoke test)
//   --seconds N       exit after N seconds of real time
//   --max-fps N       wait between frames so no more than N are drawn a second; 0 for no limit
//   --write-settings  write the settings file, with any settings given here, and exit
//   --help            this text, the user's data folder (config/user_data.hpp), the settings
//                     (config/settings.hpp) and the environment variables read (config/environment.hpp)
#include "config/environment.hpp"
#include "config/game_install.hpp"
#include "config/game_options.hpp"
#include "config/game_saves.hpp"
#include "config/settings.hpp"
#include "config/text_file.hpp"
#include "config/user_data.hpp"
#include "image/png_encoder.hpp"
#include "imgui.h"
#include "imgui_internal.h"
#include "logging/crash.hpp"
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
#include "scene/park_windows.hpp"
#include "scene/script_host.hpp"
#include "scene/startup_screen.hpp"
#include "scene/ui_demo.hpp"
#include "scene/widget_demo.hpp"
#include "simulation/park_messages.hpp"
#include "simulation/ratings.hpp"
#include "strings/unicode.hpp"
#include "ui/inspect.hpp"
#include "ui/key_map.hpp"
#include "ui/language.hpp"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <csignal>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace
{

    // Command-line help, not log messages.
    void print_usage(std::FILE* out)
    {
        std::fprintf(out, "usage: rct3pp [--renderer vulkan|null] [--headless] [--no-vsync] [--no-intro] [--scale auto|N] [--language NAME] [--widget-demo] [--no-audio] [--audio] [--park FILE] [--no-simulate] [--script FILE] [--seed N] [--frames N] [--seconds N] [--max-fps N] [--profile FILE] [--write-settings] [--help]\n");
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

    // Set by SIGINT and SIGTERM, so a long run ends through the normal shutdown.
    volatile std::sig_atomic_t g_stop_requested = 0;

    extern "C" void request_stop(int)
    {
        g_stop_requested = 1;
    }

    // Any key or click the developer windows did not take skips the logos.
    bool skip_requested()
    {
        const ImGuiIO& io = ImGui::GetIO();
        const bool click = !io.WantCaptureMouse && (ImGui::IsMouseClicked(ImGuiMouseButton_Left) || ImGui::IsMouseClicked(ImGuiMouseButton_Right));
        const bool key = !io.WantCaptureKeyboard && (ImGui::IsKeyPressed(ImGuiKey_Escape) || ImGui::IsKeyPressed(ImGuiKey_Space) || ImGui::IsKeyPressed(ImGuiKey_Enter));
        return click || key;
    }

    void draw_profiler(const rct3::render::Device& device)
    {
        const rct3::render::FrameStats stats = device.stats();
        ImGui::Text("%.1f fps", static_cast<double>(ImGui::GetIO().Framerate));
        ImGui::Text("%s on %s: %u passes, %u draws, %llu primitives", std::string(rct3::render::backend_name(device.backend())).c_str(), std::string(device.device_name()).c_str(), stats.passes, stats.draws, static_cast<unsigned long long>(stats.primitives));
        ImGui::Text("%llu bytes of per-frame vertex data", static_cast<unsigned long long>(stats.transient_bytes));
        // The graph shows the picked scope's recent frames; clicking a row picks another.
        static std::string graphed = "[Main] Frame";
        const std::vector<rct3::profiling::ScopeStats> scopes = rct3::profiling::stats();
        if (const auto it = std::ranges::find(scopes, graphed, &rct3::profiling::ScopeStats::name); it != scopes.end() && !it->history.empty())
        {
            const std::vector<float> history(it->history.begin(), it->history.end());
            const std::string label = std::format("{}: {:.2f} ms, peak {:.2f}", graphed, it->last, it->period_max);
            ImGui::PlotLines("##graph", history.data(), static_cast<int>(history.size()), 0, label.c_str(), 0.0f, static_cast<float>(it->period_max * 1.2), { ImGui::GetContentRegionAvail().x, 80 });
        }
        // Scopes as a tree: "[Thread] A/B" goes under "[Thread] A". Sibling rows sort by the picked column.
        std::vector<std::vector<std::size_t>> children(scopes.size() + 1); // The last list holds the roots
        for (std::size_t i = 0; i < scopes.size(); ++i)
        {
            const std::string& name = scopes[i].name;
            const std::size_t slash = name.rfind('/');
            const auto parent = slash == std::string::npos ? scopes.end() : std::ranges::find(scopes, std::string_view(name).substr(0, slash), &rct3::profiling::ScopeStats::name);
            children[parent == scopes.end() ? scopes.size() : static_cast<std::size_t>(parent - scopes.begin())].push_back(i);
        }
        constexpr ImGuiTableFlags kTable = ImGuiTableFlags_RowBg | ImGuiTableFlags_BordersV | ImGuiTableFlags_Resizable | ImGuiTableFlags_Sortable | ImGuiTableFlags_SortTristate | ImGuiTableFlags_ScrollY | ImGuiTableFlags_SizingStretchProp;
        if (ImGui::BeginTable("scopes", 6, kTable))
        {
            ImGui::TableSetupScrollFreeze(0, 1);
            ImGui::TableSetupColumn("scope", ImGuiTableColumnFlags_NoHide, 3.0f);
            ImGui::TableSetupColumn("last ms");
            ImGui::TableSetupColumn("avg ms", ImGuiTableColumnFlags_DefaultSort | ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableSetupColumn("p95 ms", ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableSetupColumn("peak ms", ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableSetupColumn("% parent", ImGuiTableColumnFlags_PreferSortDescending);
            ImGui::TableHeadersRow();

            const ImGuiTableSortSpecs* sort = ImGui::TableGetSortSpecs();
            const int column = sort != nullptr && sort->SpecsCount > 0 ? sort->Specs[0].ColumnIndex : -1;
            const bool descending = column >= 0 && sort->Specs[0].SortDirection == ImGuiSortDirection_Descending;
            const auto value = [&](std::size_t i) {
                const auto& s = scopes[i];
                switch (column)
                {
                    case 1:
                        return s.last;
                    case 2:
                        return s.average;
                    case 3:
                        return s.p95;
                    case 4:
                        return s.period_max;
                    default:
                        return s.average; // "% parent" orders siblings like the average
                }
            };
            const auto draw = [&](this const auto& self, std::size_t i, double parent_ms) -> void {
                const auto& s = scopes[i];
                std::vector<std::size_t> kids = children[i];
                if (column == 0)
                    std::ranges::sort(kids, [&](std::size_t x, std::size_t y) { return descending ? scopes[y].name < scopes[x].name : scopes[x].name < scopes[y].name; });
                else if (column > 0)
                    std::ranges::sort(kids, [&](std::size_t x, std::size_t y) { return descending ? value(y) < value(x) : value(x) < value(y); });
                ImGui::TableNextRow();
                ImGui::TableNextColumn();
                ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_SpanAllColumns | ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_OpenOnDoubleClick | ImGuiTreeNodeFlags_DefaultOpen;
                if (kids.empty())
                    flags |= ImGuiTreeNodeFlags_Leaf | ImGuiTreeNodeFlags_NoTreePushOnOpen;
                if (s.name == graphed)
                    flags |= ImGuiTreeNodeFlags_Selected;
                const std::size_t slash = s.name.rfind('/');
                const char* label = slash == std::string::npos ? s.name.c_str() : s.name.c_str() + slash + 1;
                const bool open = ImGui::TreeNodeEx(s.name.c_str(), flags, "%s", label);
                if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
                    graphed = s.name;
                for (const double ms : { s.last, s.average, s.p95, s.period_max })
                {
                    ImGui::TableNextColumn();
                    ImGui::Text("%.3f", ms);
                }
                ImGui::TableNextColumn();
                // Over 100%, the scope ran in only some frames (loading, say) and the averages do not compare.
                if (parent_ms > 0 && s.average <= parent_ms)
                    ImGui::Text("%.0f%%", 100 * s.average / parent_ms);
                if (open && !kids.empty())
                {
                    for (const std::size_t k : kids)
                        self(k, s.average);
                    ImGui::TreePop();
                }
            };
            std::vector<std::size_t> roots = children[scopes.size()];
            if (column > 0)
                std::ranges::sort(roots, [&](std::size_t x, std::size_t y) { return descending ? value(y) < value(x) : value(x) < value(y); });
            else if (column == 0 && descending)
                std::ranges::reverse(roots);
            for (const std::size_t r : roots)
                draw(r, 0);
            ImGui::EndTable();
        }
    }

    // Switches for looking into the park while it runs.
    void draw_park_debug(rct3::scene::ParkScene& park)
    {
        ImGui::Checkbox("Wireframe", &park.wireframe);
        ImGui::Checkbox("Free camera", &park.free_camera);
        ImGui::SetItemTooltip("No tilt, zoom or map limits, and the camera leaves the ground");
        ImGui::SliderFloat("Haze", &park.haze_rate, 0.0f, 0.003f, "%.5f");
        if (ImGui::Checkbox("Show level blending", &park.tint_crossing))
        {
            park.levels = { }; // The crossing objects are built again with or without the tint
            park.since_rebuild = rct3::scene::kObjectRebuildInterval;
        }
        ImGui::SetItemTooltip("Objects blending between two levels of detail show the finer level blue, the coarser red");
        if (ImGui::Checkbox("Simulate guests", &park.simulate))
        {
            park.people_levels.clear(); // The still people are built again without or with the simulated ones
            park.since_people_levels = rct3::scene::kObjectRebuildInterval;
        }
        ImGui::SetItemTooltip("Guests walk the paths and visit shops; the park as saved is left as it was");
        ImGui::Text("Simulation tick %llu", static_cast<unsigned long long>(park.sim.tick));
        const rct3::park::ParkCamera& c = park.camera;
        ImGui::Text("Target %.1f %.1f %.1f", static_cast<double>(c.target.x), static_cast<double>(c.target.y), static_cast<double>(c.target.z));
        ImGui::Text("Yaw %.2f, pitch %.2f, distance %.1f", static_cast<double>(c.yaw), static_cast<double>(c.pitch), static_cast<double>(c.distance));
    }

    // Recent log entries, coloured like the console. The ring is polled with a sequence cursor, so each frame
    // only copies what is new.
    /// An ImGui colour from hex text; see math/hex_colour.hpp.
    consteval ImVec4 im_hex(std::string_view text)
    {
        const glm::vec4 c = rct3::unit(rct3::hex(text));
        return { c.x, c.y, c.z, c.w };
    }

    void draw_log()
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
    }

    // Looking into the interface: the widget under the pointer, kept while the pointer is off the game (or
    // Control is held), and the one a developer window points at, outlined over the game.
    struct WidgetInspector
    {
        const rct3::ui::Widget* picked = nullptr;
        const rct3::ui::Widget* outlined = nullptr;
        bool newly_picked = false; ///< The tree opens down to it
        float scale = 1;           ///< Framebuffer pixels per interface unit
    };
    WidgetInspector g_inspector;

    std::string widget_flags(const rct3::ui::Widget& w)
    {
        std::string flags;
        for (const auto& [on, tag] : { std::pair { !w.visible, " hidden" }, { !w.enabled, " disabled" }, { w.takes_pointer(), " pointer" }, { w.hovered(), " hovered" }, { w.pressed(), " pressed" }, { w.focused(), " focused" }, { w.modal, " modal" }, { w.popup, " popup" } })
        {
            if (on)
                flags += tag;
        }
        return flags;
    }

    std::string widget_label(const rct3::ui::Widget& w)
    {
        const rct3::ui::Rect& r = w.rect();
        return std::format("{} \"{}\"  {:.0f},{:.0f}  {:.0f}x{:.0f}{}", rct3::ui::type_name(w), w.name(), r.x0, r.y0, r.width(), r.height(), widget_flags(w));
    }

    void pick_widget(const rct3::ui::Desktop& desktop, const rct3::render::ScreenView& view, const rct3::platform::Window::Pointer& p)
    {
        WidgetInspector& in = g_inspector;
        const rct3::ui::Rect root = desktop.root().rect();
        in.scale = root.width() > 0 ? static_cast<float>(view.size.width) / root.width() : 1;
        const float x = p.x - static_cast<float>(view.x);
        const float y = p.y - static_cast<float>(view.y);
        const rct3::ui::Widget* before = in.picked;
        if (!ImGui::GetIO().KeyCtrl && p.inside && x >= 0 && y >= 0 && x < static_cast<float>(view.size.width) && y < static_cast<float>(view.size.height))
            in.picked = rct3::ui::widget_at(desktop.root(), { x / in.scale, y / in.scale });
        if (!rct3::ui::in_tree(desktop.root(), in.picked)) // Gone since: a window closed, or another screen
            in.picked = nullptr;
        in.newly_picked = in.picked != before;
        in.outlined = in.picked;
    }

    void draw_ui_inspector()
    {
        namespace ui = rct3::ui;
        WidgetInspector& in = g_inspector;
        ImGui::TextDisabled("Point at the game; hold Ctrl to keep the widget while moving here");
        if (in.picked == nullptr)
        {
            ImGui::TextUnformatted("No widget");
            return;
        }
        const std::string path = ui::widget_path(*in.picked);
        ImGui::Text("Path: %s", path.empty() ? "(the desktop)" : path.c_str());
        ImGui::SameLine();
        if (ImGui::SmallButton("Copy"))
            ImGui::SetClipboardText(path.c_str());
        if (!in.picked->tooltip.empty())
            ImGui::TextWrapped("Tooltip: %s", rct3::strings::to_utf8(in.picked->tooltip).c_str());
        ImGui::Separator();
        // The desktop down to the widget; the line under the pointer is outlined instead.
        std::vector<const ui::Widget*> chain;
        for (const ui::Widget* w = in.picked; w != nullptr; w = w->parent())
            chain.insert(chain.begin(), w);
        for (std::size_t depth = 0; depth < chain.size(); ++depth)
        {
            ImGui::Text("%*s%s", static_cast<int>(depth * 2), "", widget_label(*chain[depth]).c_str());
            if (ImGui::IsItemHovered())
                in.outlined = chain[depth];
        }
    }

    void draw_widget_node(const rct3::ui::Widget& w)
    {
        WidgetInspector& in = g_inspector;
        ImGuiTreeNodeFlags flags = ImGuiTreeNodeFlags_OpenOnArrow | ImGuiTreeNodeFlags_SpanAvailWidth;
        if (w.children().empty())
            flags |= ImGuiTreeNodeFlags_Leaf;
        if (&w == in.picked)
            flags |= ImGuiTreeNodeFlags_Selected;
        if (in.newly_picked && in.picked != nullptr && &w != in.picked && rct3::ui::in_tree(w, in.picked))
            ImGui::SetNextItemOpen(true);
        if (!w.visible)
            ImGui::PushStyleColor(ImGuiCol_Text, ImGui::GetStyleColorVec4(ImGuiCol_TextDisabled));
        const bool open = ImGui::TreeNodeEx(&w, flags, "%s", widget_label(w).c_str());
        if (!w.visible)
            ImGui::PopStyleColor();
        if (ImGui::IsItemHovered())
            in.outlined = &w;
        if (ImGui::IsItemClicked() && !ImGui::IsItemToggledOpen())
            in.picked = &w;
        if (in.newly_picked && &w == in.picked)
            ImGui::SetScrollHereY();
        if (!open)
            return;
        for (const auto& c : w.children())
            draw_widget_node(*c);
        ImGui::TreePop();
    }

    // Every widget of the interface; a click picks one for the inspector.
    void draw_widget_tree(const rct3::ui::Desktop& desktop)
    {
        ImGui::SetNextItemOpen(true, ImGuiCond_Once);
        draw_widget_node(desktop.root());
    }

    void draw_widget_outline(const rct3::render::ScreenView& view)
    {
        const WidgetInspector& in = g_inspector;
        if (in.outlined == nullptr)
            return;
        const ImVec2 to_ui = ImGui::GetIO().DisplayFramebufferScale;
        const auto corner = [&](float cx, float cy) { return ImVec2 { (static_cast<float>(view.x) + cx * in.scale) / to_ui.x, (static_cast<float>(view.y) + cy * in.scale) / to_ui.y }; };
        ImDrawList& draw = *ImGui::GetForegroundDrawList();
        // The nearest holder smaller than the screen; full-screen frames and the desktop would only outline the game.
        const rct3::ui::Widget* holder = in.outlined->parent();
        while (holder != nullptr && holder->parent() != nullptr && holder->rect().width() >= holder->parent()->rect().width() && holder->rect().height() >= holder->parent()->rect().height())
            holder = holder->parent();
        if (holder != nullptr && holder->parent() != nullptr)
            draw.AddRect(corner(holder->rect().x0, holder->rect().y0), corner(holder->rect().x1, holder->rect().y1), IM_COL32(255, 200, 60, 160));
        const rct3::ui::Rect& r = in.outlined->rect();
        draw.AddRectFilled(corner(r.x0, r.y0), corner(r.x1, r.y1), IM_COL32(64, 150, 255, 40));
        draw.AddRect(corner(r.x0, r.y0), corner(r.x1, r.y1), IM_COL32(64, 150, 255, 255), 0.0f, 2.0f);
        const std::string label = rct3::ui::type_name(*in.outlined) + " " + std::string(in.outlined->name());
        const ImVec2 at = corner(r.x0, r.y0);
        const ImVec2 size = ImGui::CalcTextSize(label.c_str());
        const ImVec2 chip { at.x, at.y >= size.y + 4 ? at.y - size.y - 4 : at.y };
        draw.AddRectFilled(chip, { chip.x + size.x + 8, chip.y + size.y + 4 }, IM_COL32(20, 20, 20, 230), 3);
        draw.AddText({ chip.x + 4, chip.y + 2 }, IM_COL32(140, 200, 255, 255), label.c_str());
    }

    /// A developer window; docked where the layout puts it, or wherever the user drags it since.
    template<typename F> void developer_window(const char* title, F&& draw)
    {
        if (ImGui::Begin(title))
            draw();
        ImGui::End();
    }

    /// The dock space over the whole screen, laid out the first time (later the user's layout, kept in
    /// imgui.ini), and where the game draws: the empty node in the middle, in framebuffer pixels.
    rct3::render::ScreenView dock_developer_windows()
    {
        const ImGuiID space = ImGui::DockSpaceOverViewport(0, nullptr, ImGuiDockNodeFlags_PassthruCentralNode);
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(space);
        // Once a run, and only when imgui.ini kept no split of its own; windows the user floats later stay so.
        static bool checked = false;
        if (!std::exchange(checked, true) && node != nullptr && node->IsLeafNode())
        {
            // Our own proportions: the profiler on the left, the log below, the interface and the park on the right.
            ImGuiID middle = space;
            const ImGuiID left = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Left, 0.22f, nullptr, &middle);
            const ImGuiID right = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Right, 0.3f, nullptr, &middle);
            const ImGuiID bottom = ImGui::DockBuilderSplitNode(middle, ImGuiDir_Down, 0.3f, nullptr, &middle);
            ImGuiID right_bottom = right;
            const ImGuiID right_top = ImGui::DockBuilderSplitNode(right, ImGuiDir_Up, 0.45f, nullptr, &right_bottom);
            ImGui::DockBuilderDockWindow("Profiler", left);
            ImGui::DockBuilderDockWindow("Log", bottom);
            ImGui::DockBuilderDockWindow("Interface", right_top);
            ImGui::DockBuilderDockWindow("Park", right_top);
            ImGui::DockBuilderDockWindow("Widgets", right_bottom);
            ImGui::DockBuilderFinish(space);
        }
        const ImVec2 to_px = ImGui::GetIO().DisplayFramebufferScale;
        const ImGuiDockNode* central = ImGui::DockBuilderGetCentralNode(space);
        const ImVec2 pos = central != nullptr ? central->Pos : ImVec2 { };
        const ImVec2 size = central != nullptr ? central->Size : ImGui::GetMainViewport()->Size;
        return { static_cast<std::uint32_t>(std::max(0.0f, pos.x * to_px.x)), static_cast<std::uint32_t>(std::max(0.0f, pos.y * to_px.y)), { static_cast<std::uint32_t>(std::max(1.0f, size.x * to_px.x)), static_cast<std::uint32_t>(std::max(1.0f, size.y * to_px.y)) } };
    }

} // namespace

int main(int argc, char** argv)
{
    rct3::logging::install_crash_report();
    namespace prof = rct3::profiling;
    namespace render = rct3::render;
    namespace config = rct3::config;
    const auto log = rct3::logging::logger("app");
    const auto env = config::Environment::from_process();

    // Everything kept for the user lives in one folder.
    const auto data = config::UserData::locate(env);
    if (!data)
        log.warning("no folder for the user's data known; set {}: settings, options and keys are not kept", config::env_name(config::EnvVar::DataFolder));
    // Each run's log is kept; a crash appends its report there.
    if (data)
    {
        if (auto opened = rct3::logging::global().open_file(data->folder(config::UserFolder::Logs)))
        {
            rct3::logging::install_crash_report(*opened);
            log.info("log: {}", opened->string());
        }
        else
            log.warning("{}", opened.error());
    }
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
    bool headless = false;
    std::optional<bool> sound_this_run;
    double max_seconds = -1;
    std::optional<double> max_fps;
    std::optional<std::filesystem::path> park_file;
    std::optional<std::filesystem::path> script_file;
    std::optional<std::filesystem::path> script_report;
    std::optional<std::uint64_t> seed;
    bool simulate = true;                         ///< The park starts with the simulation running
    std::optional<std::filesystem::path> profile; ///< Where to write the profiler's table on the way out
    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
        {
            max_frames = std::atoi(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--seconds") == 0 && i + 1 < argc)
        {
            max_seconds = std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--max-fps") == 0 && i + 1 < argc)
        {
            max_fps = std::atof(argv[++i]);
        }
        else if (std::strcmp(argv[i], "--headless") == 0)
        {
            headless = true;
        }
        else if (std::strcmp(argv[i], "--park") == 0 && i + 1 < argc)
        {
            park_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--script") == 0 && i + 1 < argc)
        {
            script_file = argv[++i];
        }
        else if (std::strcmp(argv[i], "--script-report") == 0 && i + 1 < argc)
        {
            script_report = argv[++i];
        }
        else if (std::strcmp(argv[i], "--no-simulate") == 0)
        {
            simulate = false;
        }
        else if (std::strcmp(argv[i], "--profile") == 0 && i + 1 < argc)
        {
            profile = argv[++i];
        }
        else if (std::strcmp(argv[i], "--audio") == 0)
        {
            sound_this_run = true;
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
            settings.set(config::Setting::NoAudio, "yes");
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

    if (headless)
    {
        backend = render::Backend::Null;
        vsync = false;
    }
    // Without vsync or a window to show, a headless run would otherwise keep a core busy.
    const double frame_limit = max_fps.value_or(headless ? 60.0 : 0.0);
    const bool sound = !headless && sound_this_run.value_or(!settings.flag(config::Setting::NoAudio));
    const bool intro = !headless && !settings.flag(config::Setting::SkipIntro);
    std::optional<float> scale;
    if (const auto parsed = config::parse_interface_scale(settings.get(config::Setting::InterfaceScale).value_or(std::string_view { })))
        scale = *parsed;
    else
        log.warning("settings: {}", parsed.error());

    // The keys the user set for the interface's commands.
    const auto keys_file = user_file(config::UserFile::Keys);
    rct3::ui::KeyMap key_map;
    load_user_file(config::UserFile::Keys, key_map);
    // How rides are rated: the user's own model when they keep one, else the one built in.
    rct3::simulation::RatingModel ride_ratings = rct3::simulation::builtin_rating_model();
    if (const auto path = user_file(config::UserFile::RideRatings))
    {
        const auto text = config::read_text_file(*path);
        if (!text)
            log.warning("{}", text.error());
        else if (*text)
        {
            if (auto parsed = rct3::simulation::parse_rating_model(**text))
                ride_ratings = std::move(*parsed);
            else
                log.warning("{}: {}", path->string(), parsed.error());
        }
    }
    // The developer windows start hidden; a command shows and hides them.
    bool developer_windows = false;

    // Only the startup screens and widget demo use the game so far, so a missing install is not fatal for now.
    const auto install = config::find_game_install(env, settings);
    if (install)
        log.info("game install: {}", install->root().string());
    else
        log.warning("{}", config::describe(install.error()));

    auto window = rct3::platform::Window::open({ .headless = headless }, backend);
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
    const std::vector<std::uint32_t> sample_levels = device->sample_levels();

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
    std::optional<rct3::scene::ParkScene> park; ///< Played over the front end, which lends it the interface
    std::optional<std::filesystem::path> park_to_load = park_file;
    std::uint64_t park_log_from = 0; ///< The log's last entry before the park loading now began; 0 before ///< Asked for this frame; its loading screen shows first
    bool quit = false;
    bool screenshot = false;           ///< Asked for this frame; read from the screen as the frame ends
    std::filesystem::path script_shot; ///< Where the screenshot a script asked for goes; empty for the user's folder
    // Writes a screenshot or photo to the user's folder for it, and tells the player where it went.
    const auto save_picture = [&](rct3::render::ScreenPixels pixels, config::UserFolder into, std::string_view prefix, std::string_view told) {
        if (!data)
        {
            log.warning("picture: no folder for the user's data known; set {}", config::env_name(config::EnvVar::DataFolder));
            return;
        }
        auto saved = data->make_folder(into).and_then([&](const std::filesystem::path& folder) { return rct3::image::save_numbered_png(folder, prefix, { pixels.size.width, pixels.size.height, std::move(pixels.rgba) }); });
        if (!saved)
        {
            log.warning("picture: {}", saved.error());
            return;
        }
        log.info("picture: {}", saved->string());
        if (park)
            rct3::simulation::post(park->park.messages, { .text_key = std::string { told }, .subject_name = saved->filename().string() });
    };
    // The menus' sound starts with the first front screen and goes on through language changes. The options it
    // reads are the front end's once there is one; until then, those in the file.
    std::optional<rct3::audio::AudioEngine> audio;
    std::optional<rct3::scene::MenuAudio> menu_audio;
    config::GameOptions audio_options;
    // The engine starts before the menus' sound where the startup logos have sound of their own.
    const auto create_audio = [&] {
        if (audio)
            return;
        auto engine = rct3::audio::AudioEngine::create();
        if (!engine)
        {
            log.warning("audio: {}", rct3::audio::to_string(engine.error()));
            return;
        }
        audio.emplace(std::move(*engine));
    };
    const auto start_audio = [&] {
        create_audio();
        if (!audio)
            return;
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
            if (sound && intro && !startup->movies.empty())
                create_audio();
        }
        else
        {
            log.warning("startup screen: {}", created.error());
        }
    }
    const auto load_widgets = [&](bool reload) {
        if (sound && !reload && !menu_audio)
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
        if (install)
            setup.game_saves = config::find_game_saves(env, install->root());
        const auto current = window->size();
        for (const auto& s : window->screen_sizes())
        {
            if (s == current)
                setup.screen_size = setup.screen_sizes.size();
            setup.screen_sizes.push_back({ s.width, s.height });
        }
        for (const std::uint32_t n : sample_levels)
            setup.anti_aliasing_levels.push_back(static_cast<int>(n));
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
    // What the options window asks of the window and the keys, from the front end or over a park.
    const auto apply_requests = [&](const rct3::scene::FrontEndRequests& requests) {
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
    };
    const auto follow_front_end = [&] {
        const auto requests = rct3::scene::take_front_end_requests(*front);
        apply_requests(requests);
        if (requests.park)
            park_to_load = std::move(requests.park);
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
    // The log says how long each stage took, and where a load that failed stopped.
    const auto load_park = [&](const std::filesystem::path& path) {
        const prof::Scope zone("LoadingScreen");
        auto shown = rct3::scene::ParkLoadStage::ReadFile; // The frame before loading began drew it
        const std::uint64_t log_from = park_log_from != 0 ? park_log_from : rct3::logging::global().latest_sequence();
        const double started = window->time();
        double stage_started = started;
        log.info("loading park {}", path.string());
        const auto report = [&](rct3::scene::ParkLoadStage stage) {
            window->poll_events();
            if (audio && menu_audio)
                rct3::scene::update_menu_audio(*audio, *menu_audio, front->live->options, { });
            if (stage == shown)
                return;
            const double now = window->time();
            log.info("park loading: {} took {:.2f} s", rct3::scene::park_load_name(shown), now - stage_started);
            stage_started = now;
            shown = stage;
            device->begin_frame();
            device->begin_pass({ .clear_color = kBackground });
            rct3::scene::draw_park_loading(*device, front->assets, front->list, stage, log_from);
            device->end_pass();
            device->end_frame();
        };
        auto loaded = rct3::scene::create_park_scene(*device, *install, *front->assets.strings, front->live->options, path, seed.value_or(rct3::random::fresh_seed()), ride_ratings, report);
        const double now = window->time();
        if (loaded)
        {
            log.info("park loading: {} took {:.2f} s; loaded in {:.2f} s", rct3::scene::park_load_name(shown), now - stage_started, now - started);
            park = std::move(*loaded);
            park->simulate = simulate;
            park->scale = scale;
        }
        else
            log.error("park loading failed during {} after {:.2f} s: {}: {}", rct3::scene::park_load_name(shown), now - started, path.string(), loaded.error());
    };
    if (install && !startup)
        load_widgets(false);

    int status = 0;
    std::unique_ptr<rct3::scene::ScriptHost> script;
    if (script_file)
    {
        script = rct3::scene::ScriptHost::start(*script_file, script_report);
        if (!script)
            return 1;
    }
    try
    {
        prof::register_thread("Main");
        std::signal(SIGINT, request_stop);
        std::signal(SIGTERM, request_stop);
        const double start_time = window->time();
        // The pointer from the corner of where the game draws, which the developer panels may have moved.
        const auto game_pointer = [&] {
            auto p = window->pointer();
            // A script plays in place of the real pointer once it has moved its own.
            if (const auto held = script ? script->pointer(*device) : std::nullopt)
            {
                p = { };
                p.x = held->x;
                p.y = held->y;
                p.left = held->down;
                p.inside = true;
                return p;
            }
            if (const auto& v = device->view())
            {
                p.x -= static_cast<float>(v->x);
                p.y -= static_cast<float>(v->y);
                p.inside = p.inside && p.x >= 0 && p.y >= 0 && p.x < static_cast<float>(v->size.width) && p.y < static_cast<float>(v->size.height);
            }
            return p;
        };
        double last_time = start_time;
        int frame = 0;
        for (; !window->should_close() && !quit && frame != max_frames && g_stop_requested == 0; ++frame)
        {
            if (max_seconds >= 0 && window->time() - start_time >= max_seconds)
                break;
            if (frame_limit > 0)
            {
                const double wait = last_time + 1 / frame_limit - window->time();
                if (wait > 0)
                    std::this_thread::sleep_for(std::chrono::duration<double>(wait));
            }
            window->poll_events();

            const double now = window->time();
            const double delta = now - last_time;
            prof::update(delta);
            last_time = now;

            prof::section("Frame", [&] {
                prof::section("Ui", [&] {
                    if (script)
                    {
                        script->tick(*device, phase == Phase::Running && park ? &*park : nullptr);
                        if (const auto wanted = script->take_park())
                        {
                            park_to_load = install ? install->find(*wanted) : std::nullopt;
                            if (!park_to_load)
                                script->fail(std::format("park.open: no {} in the game's folder", *wanted));
                        }
                        if (const auto code = script->quit())
                        {
                            quit = true;
                            status = *code;
                        }
                    }
                    device->ui_new_frame();
                    window->ui_new_frame();
                    ImGui::NewFrame();
                    if (phase == Phase::Intro)
                    {
                        startup->sequence.update(static_cast<float>(delta), skip_requested());
                        if (startup->sequence.finished())
                        {
                            if (audio)
                                rct3::scene::stop_startup_sound(*startup, *audio);
                            phase = Phase::Loading;
                        }
                    }
                    // The keys as the commands they are bound to, unless the interface is waiting for keys
                    // to bind: it then gets them as pressed, and they do nothing else.
                    rct3::ui::KeyboardState keys = window->keyboard();
                    std::vector<rct3::ui::PlayCommand> play_commands;
                    const bool binding = (front && rct3::scene::front_end_waiting_for_keys(*front)) || (park && rct3::scene::park_waiting_for_keys(*park));
                    if (!binding)
                    {
                        auto translated = key_map.translate(keys);
                        keys = std::move(translated.keyboard);
                        play_commands = std::move(translated.play);
                        screenshot = std::ranges::contains(play_commands, rct3::ui::PlayCommand::ScreenSnapshot);
                        if (const auto to = script ? script->take_screenshot() : std::nullopt)
                        {
                            screenshot = true;
                            script_shot = *to;
                        }
                        const auto toggles = std::ranges::count(translated.commands, rct3::ui::Command::DeveloperWindows);
                        developer_windows = developer_windows != (toggles % 2 == 1);
                        for (auto& k : keys.presses)
                        {
                            if (k.key == rct3::ui::Key::Paste)
                                k.text = window->clipboard();
                        }
                    }
                    // The developer windows dock around the game rather than cover it; the game draws in the
                    // space left in the middle.
                    device->set_view(std::nullopt);
                    if (phase == Phase::Running && developer_windows)
                    {
                        const prof::Scope zone("DeveloperWindows");
                        const render::ScreenView view = dock_developer_windows();
                        device->set_view(view);
                        developer_window("Profiler", [&] { draw_profiler(*device); });
                        developer_window("Log", draw_log);
                        if (park)
                            developer_window("Park", [&] { draw_park_debug(*park); });
                        if (const rct3::ui::Desktop* desktop = park ? park->desktop.get() : widgets ? &widgets->desktop : front ? &front->live->desktop : nullptr)
                        {
                            pick_widget(*desktop, view, window->pointer());
                            developer_window("Interface", draw_ui_inspector);
                            developer_window("Widgets", [&] { draw_widget_tree(*desktop); });
                            draw_widget_outline(view);
                        }
                    }
                    if (phase == Phase::Running && park && front)
                    {
                        const auto p = game_pointer();
                        rct3::scene::ParkInput input;
                        input.pointer = { { p.x, p.y }, p.left, !ImGui::GetIO().WantCaptureMouse, { p.wheel_x, p.wheel_y } };
                        input.right = p.right && input.pointer.present;
                        input.in_window = p.inside && input.pointer.present;
                        input.middle = p.middle && input.pointer.present;
                        if (!ImGui::GetIO().WantCaptureKeyboard && !rct3::scene::typing(*park))
                        {
                            // On the keys the player bound to the game's camera commands, unless they are being bound
                            // or typed into a box.
                            using P = rct3::ui::PlayCommand;
                            const auto held = [&](P a, std::optional<P> b = std::nullopt) {
                                const auto on = [&](P c) { return !binding && key_map.play_key(c) && window->held(*key_map.play_key(c)); };
                                return on(a) || (b && on(*b));
                            };
                            input.left_key = held(P::MoveLeft, P::MoveLeft2);
                            input.right_key = held(P::MoveRight, P::MoveRight2);
                            input.up_key = held(P::MoveForwards, P::MoveForwards2);
                            input.down_key = held(P::MoveBackwards, P::MoveBackwards2);
                            input.zoom_in_key = held(P::ZoomIn);
                            input.zoom_out_key = held(P::ZoomOut);
                            input.turn_left_key = held(P::RotateLeft);
                            input.turn_right_key = held(P::RotateRight);
                            input.tilt_up_key = held(P::RotateUp);
                            input.tilt_down_key = held(P::RotateDown);
                            input.commands = play_commands;
                        }
                        const rct3::ui::KeyboardState no_keys;
                        prof::section("Park", [&] { rct3::scene::update_park_scene(*device, *park, front->assets, front->live->options, input, ImGui::GetIO().WantCaptureKeyboard ? no_keys : keys, static_cast<float>(delta)); });
                        rct3::scene::update_park_windows(*device, *park, *front);
                        auto park_requests = rct3::scene::take_park_window_requests(*park, *front);
                        apply_requests(park_requests);
                        if (const auto copied = park->desktop->take_clipboard())
                            window->set_clipboard(*copied);
                        auto sounds = park->desktop->take_sounds();
                        if (rct3::scene::take_park_alert(*park))
                            sounds.push_back(rct3::ui::InterfaceSound::Message);
                        if (audio && menu_audio)
                            prof::section("Audio", [&] { rct3::scene::update_menu_audio(*audio, *menu_audio, front->live->options, sounds, true); });
                        // Leaving, or loading another park in its place: its loading screen shows next.
                        if (rct3::scene::take_park_leave(*park) || park_requests.park)
                        {
                            const prof::Scope zone("LeavePark");
                            rct3::scene::close_park_windows(*device, *park, *front);
                            rct3::scene::destroy_park_scene(*device, *park);
                            park.reset();
                            park_to_load = std::move(park_requests.park);
                        }
                    }
                    else if (phase == Phase::Running && (widgets || front))
                    {
                        // The developer windows come first: where they want the pointer, the game does not get it.
                        const auto p = game_pointer();
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
                    // The anti-aliasing option picks among the levels the device offers, after none.
                    if (front)
                    {
                        const auto level = static_cast<std::size_t>(front->live->options.choice(config::GameOption::AntiAliasing));
                        device->set_samples(level > 0 && level <= sample_levels.size() ? sample_levels[level - 1] : 1);
                    }
                    prof::section("BeginFrame", [&] { device->begin_frame(); });
                    if (phase == Phase::Intro)
                        rct3::scene::update_startup_movie(*device, *startup, audio ? &*audio : nullptr);
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
                        {
                            // Warnings from here on are this load's.
                            if (park_log_from == 0)
                                park_log_from = rct3::logging::global().latest_sequence();
                            rct3::scene::draw_park_loading(*device, front->assets, front->list, rct3::scene::ParkLoadStage::ReadFile, park_log_from);
                        }
                        else if (front)
                            rct3::scene::draw_front_end_scene(*device, *front);
                        else
                            rct3::scene::draw_ui_demo(*device, *ui_demo, now);
                    }
                    prof::section("DeveloperUi", [&] { device->draw_ui(); });
                    // A photo drawn this frame takes the one read the device has; a screenshot waits a frame.
                    const bool photo = park && park->photo_reading;
                    prof::section("Submit", [&] {
                        device->end_pass();
                        if (!photo && std::exchange(screenshot, false) && !device->read_screen())
                            log.warning("screenshot: the device cannot read the screen");
                        device->end_frame();
                    });
                    if (auto pixels = device->take_screen())
                    {
                        if (photo)
                            save_picture(std::move(*pixels), config::UserFolder::Photos, *park->photo_reading, "Rct3pp_PhotoSaved");
                        else if (!script_shot.empty())
                        {
                            if (auto saved = rct3::image::save_png(script_shot, { pixels->size.width, pixels->size.height, std::move(pixels->rgba) }); !saved)
                                log.warning("picture: {}", saved.error());
                            script_shot.clear();
                        }
                        else
                            save_picture(std::move(*pixels), config::UserFolder::Screenshots, "screenshot_", "Rct3pp_ScreenshotSaved");
                    }
                    if (photo)
                        park->photo_reading.reset();
                    if (park_to_load && front)
                    {
                        load_park(*std::exchange(park_to_load, std::nullopt));
                        park_log_from = 0;
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
        const double ran = window->time() - start_time;
        log.info("ran {} frames in {:.1f} s, {:.2f} ms a frame{}", frame, ran, frame > 0 ? 1000 * ran / frame : 0.0, g_stop_requested != 0 ? ", stopped by a signal" : "");
        if (device)
        {
            const rct3::render::FrameStats stats = device->stats();
            log.info("last frame: {} passes, {} draws, {} primitives", stats.passes, stats.draws, stats.primitives);
        }
        if (profile)
        {
            std::ofstream out(*profile);
            out << prof::tsv_snapshot();
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
    {
        if (audio)
            rct3::scene::stop_startup_sound(*startup, *audio);
        rct3::scene::destroy_startup_screen(*device, *startup);
    }
    if (widgets)
        rct3::scene::destroy_widget_demo(*device, *widgets);
    if (park)
    {
        if (front)
            rct3::scene::close_park_windows(*device, *park, *front);
        rct3::scene::destroy_park_scene(*device, *park);
    }
    if (front)
        rct3::scene::destroy_front_end_scene(*device, *front);
    rct3::scene::destroy_ui_demo(*device, *ui_demo);
    device->shutdown();
    return status;
}
