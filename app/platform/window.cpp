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

        // Installed before ImGui's own callbacks, which call it in turn.
        auto collected = std::make_unique<Collected>();
        glfwSetWindowUserPointer(window, collected.get());
        glfwSetScrollCallback(window, [](GLFWwindow* w, double, double y) { static_cast<Collected*>(glfwGetWindowUserPointer(w))->wheel += y; });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        // "Other" fits every backend that does not need GLFW to manage an OpenGL context.
        ImGui_ImplGlfw_InitForOther(window, true);
        // The game reads the button state once a frame; a click shorter than a frame must still show up.
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
        return Window(window, std::move(collected));
    }

    Window::Window(Window&& other) noexcept
        : window_(std::exchange(other.window_, nullptr))
        , collected_(std::move(other.collected_))
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
        collected_->wheel = 0;
        glfwPollEvents();
    }

    double Window::time() const
    {
        return glfwGetTime();
    }

    Window::Pointer Window::pointer() const
    {
        // GLFW reports the cursor in screen coordinates, which differ from framebuffer pixels on scaled
        // displays.
        double cx = 0;
        double cy = 0;
        glfwGetCursorPos(window_, &cx, &cy);
        int ww = 0;
        int wh = 0;
        int fw = 0;
        int fh = 0;
        glfwGetWindowSize(window_, &ww, &wh);
        glfwGetFramebufferSize(window_, &fw, &fh);
        Pointer p;
        if (ww > 0 && wh > 0)
        {
            p.x = static_cast<float>(cx * fw / ww);
            p.y = static_cast<float>(cy * fh / wh);
        }
        p.left = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_LEFT) == GLFW_PRESS;
        p.wheel = static_cast<float>(collected_->wheel);
        return p;
    }

    void Window::ui_new_frame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

} // namespace rct3::platform
