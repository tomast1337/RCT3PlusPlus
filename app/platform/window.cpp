// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/window.hpp"

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"
#include "render/backend.hpp"

#include <GLFW/glfw3.h>
#include <utility>

namespace rct3::platform
{

    std::expected<Window, std::string> Window::open(const WindowConfig& config, render::Backend backend)
    {
        if (!glfwInit())
            return std::unexpected("cannot initialise GLFW");
        render::set_window_hints(backend);
        GLFWwindow* window = glfwCreateWindow(config.width, config.height, config.title, nullptr, nullptr);
        if (window == nullptr)
        {
            glfwTerminate();
            return std::unexpected("cannot create window");
        }

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        // "Other" fits every backend that does not need GLFW to manage an OpenGL context.
        ImGui_ImplGlfw_InitForOther(window, true);
        return Window(window);
    }

    Window::Window(Window&& other) noexcept
        : window_(std::exchange(other.window_, nullptr))
    {
    }

    Window::~Window()
    {
        if (window_ == nullptr)
            return;
        ImGui_ImplGlfw_Shutdown();
        ImGui::DestroyContext();
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    bool Window::should_close() const
    {
        return glfwWindowShouldClose(window_) != 0;
    }

    void Window::poll_events()
    {
        glfwPollEvents();
    }

    double Window::time() const
    {
        return glfwGetTime();
    }

    void Window::ui_new_frame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

} // namespace rct3::platform
