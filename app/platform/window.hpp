// SPDX-License-Identifier: GPL-3.0-or-later
//
// The platform layer: GLFW, the window and the platform half of Dear ImGui. Owns the ImGui context, so it is
// created before the render device and destroyed after it.
#pragma once

#include "render/device.hpp"

#include <expected>
#include <string>

struct GLFWwindow;

namespace rct3::platform
{

    struct WindowConfig
    {
        int width = 1280;
        int height = 720;
        const char* title = "rct3++";
    };

    class Window
    {
    public:
        /// Initialises GLFW, opens a window with the hints `backend` needs and sets up ImGui for it.
        [[nodiscard]] static std::expected<Window, std::string> open(const WindowConfig& config, render::Backend backend);

        Window(Window&& other) noexcept;
        Window& operator=(Window&&) = delete;
        Window(const Window&) = delete;
        Window& operator=(const Window&) = delete;
        ~Window();

        [[nodiscard]] GLFWwindow* handle() const
        {
            return window_;
        }
        [[nodiscard]] bool should_close() const;
        void poll_events();
        /// Seconds since GLFW was initialised.
        [[nodiscard]] double time() const;

        struct Pointer
        {
            float x = 0; ///< Framebuffer pixels from the top-left corner, the units the device draws in
            float y = 0;
            bool left = false;
        };
        [[nodiscard]] Pointer pointer() const;

        /// The platform half of starting an ImGui frame; call after the device's `ui_new_frame`.
        void ui_new_frame();

    private:
        explicit Window(GLFWwindow* window)
            : window_(window)
        {
        }

        GLFWwindow* window_ = nullptr;
    };

} // namespace rct3::platform
