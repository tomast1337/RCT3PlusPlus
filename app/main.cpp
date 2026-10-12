// SPDX-License-Identifier: GPL-3.0-or-later
//
// rct3++ viewer, first step: a window, a Vulkan triangle and a Dear ImGui overlay.
//
//   rct3pp [--frames N]    --frames N exits after N frames (smoke test)
#include <GLFW/glfw3.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <exception>

#include "imgui.h"
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

}  // namespace

int main(int argc, char** argv) {
  namespace prof = rct3::profiling;

  int max_frames = -1;
  for (int i = 1; i < argc; ++i) {
    if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) max_frames = std::atoi(argv[++i]);
    else {
      std::fprintf(stderr, "usage: rct3pp [--frames N]\n");
      return 2;
    }
  }

  if (!glfwInit()) {
    std::fprintf(stderr, "rct3pp: cannot initialise GLFW\n");
    return 1;
  }
  glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
  GLFWwindow* window = glfwCreateWindow(1280, 720, "rct3++", nullptr, nullptr);
  if (window == nullptr) {
    std::fprintf(stderr, "rct3pp: cannot create window\n");
    glfwTerminate();
    return 1;
  }

  int status = 0;
  try {
    prof::register_thread("Main");
    rct3::app::Renderer renderer(window);
    std::fprintf(stderr, "rct3pp: using %s\n", renderer.device_name());

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
          ImGui::ShowDemoWindow();
        });
        prof::section("Draw", [&] { renderer.draw_frame(); });
      });
      prof::capture_frame();
    }
  } catch (const std::exception& e) {
    std::fprintf(stderr, "rct3pp: %s\n", e.what());
    status = 1;
  }

  glfwDestroyWindow(window);
  glfwTerminate();
  return status;
}
