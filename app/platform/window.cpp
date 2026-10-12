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
        // The game reads the button state once a frame; a click shorter than a frame must still show up.
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
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
        return p;
    }

    void Window::ui_new_frame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

} // namespace rct3::platform
