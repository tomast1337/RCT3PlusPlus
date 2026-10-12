// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer, first step: a window, a Vulkan triangle and a Dear ImGui overlay.
//
//   rct3pp [--frames N]    --frames N exits after N frames (smoke test)
#include <GLFW/glfw3.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <vector>

#include "imgui.h"
#include "logging/log.hpp"
#include "profiling/profiler.hpp"
#include "renderer.hpp"

namespace {

void draw_profiler_window() {
  ImGui::SetNextWindowPos({12, 12}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({460, 260}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Profiler");
  ImGui::Text("%.1f fps", static_cast<double>(ImGui::GetIO().Framerate));
  if (ImGui::BeginTable("scopes", 4, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("scope");
    ImGui::TableSetupColumn("last ms");
    ImGui::TableSetupColumn("avg ms");
    ImGui::TableSetupColumn("p95 ms");
    ImGui::TableHeadersRow();
    for (const auto& s : rct3::profiling::stats()) {
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
void draw_log_window() {
  namespace logging = rct3::logging;
  static std::vector<logging::Entry> shown;
  static std::uint64_t cursor = 0;
  static int minimum = static_cast<int>(logging::Level::Info);
  static bool follow = true;

  for (auto& e : logging::global().entries_since(cursor)) {
    cursor = e.sequence;
    shown.push_back(std::move(e));
  }
  constexpr std::size_t kKeep = 2000;
  if (shown.size() > kKeep) shown.erase(shown.begin(), shown.begin() + static_cast<std::ptrdiff_t>(shown.size() - kKeep));

  ImGui::SetNextWindowPos({12, 290}, ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowSize({760, 240}, ImGuiCond_FirstUseEver);
  ImGui::Begin("Log");
  static constexpr std::array<const char*, 5> kLevels = {"Debug", "Info", "Warning", "Error", "Critical"};
  ImGui::SetNextItemWidth(110);
  ImGui::Combo("Minimum", &minimum, kLevels.data(), static_cast<int>(kLevels.size()));
  ImGui::SameLine();
  ImGui::Checkbox("Follow", &follow);
  ImGui::SameLine();
  if (ImGui::Button("Clear")) {
    shown.clear();
    logging::global().clear();
  }
  ImGui::Separator();

  static constexpr std::array<ImVec4, 5> kColors = {
      ImVec4(0.55f, 0.55f, 0.55f, 1), ImVec4(0.40f, 0.85f, 0.40f, 1), ImVec4(0.95f, 0.80f, 0.25f, 1),
      ImVec4(0.95f, 0.35f, 0.35f, 1), ImVec4(1.00f, 0.55f, 0.55f, 1),
  };
  ImGui::BeginChild("lines", {0, 0}, ImGuiChildFlags_None, ImGuiWindowFlags_HorizontalScrollbar);
  for (const auto& e : shown) {
    const int level = static_cast<int>(e.level);
    if (level < minimum) continue;
    ImGui::PushStyleColor(ImGuiCol_Text, kColors[static_cast<std::size_t>(level)]);
    ImGui::Text("[%s]", std::string(logging::level_tag(e.level)).c_str());
    ImGui::PopStyleColor();
    ImGui::SameLine();
    ImGui::TextDisabled("%s:", e.category.c_str());
    ImGui::SameLine();
    ImGui::TextUnformatted(e.message.c_str());
  }
  if (follow && ImGui::GetScrollY() >= ImGui::GetScrollMaxY() - 1.0f) ImGui::SetScrollHereY(1.0f);
  ImGui::EndChild();
  ImGui::End();
}

}  // namespace

int main(int argc, char** argv) {
  namespace prof = rct3::profiling;
  const auto log = rct3::logging::logger("app");

  int max_frames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::atoi(argv[++i]);
    else {
      std::fprintf(stderr, "usage: rct3pp [--frames N]\n");  // command-line help, not a log message
      return 2;
    }
  }

  if (!glfwInit()) {
    log.critical("cannot initialise GLFW");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 720, "rct3++", nullptr, nullptr);
  if (window == nullptr) {
    log.critical("cannot create window");
    glfwTerminate();
    return 1;
  }

  int status = 0;
  try {
    prof::register_thread("Main");
    rct3::app::Renderer renderer(window);
    log.info("using {}", renderer.device_name());

    double last_time = glfwGetTime();
    for (int frame = 0; !glfwWindowShouldClose(window) && frame != max_frames; ++frame) {
      glfwPollEvents();

      const double now = glfwGetTime();
      prof::update(now - last_time);
      last_time = now;

      prof::section("Frame", [&] {
        prof::section("Ui", [&] {
          renderer.begin_frame();
          draw_profiler_window();
          draw_log_window();
          ImGui::ShowDemoWindow();
        });
        prof::section("Draw", [&] { renderer.draw_frame(); });
      });
      prof::capture_frame();
    }
  } catch (const std::exception& e) {
    log.critical("{}", e.what());
    status = 1;
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return status;
}
