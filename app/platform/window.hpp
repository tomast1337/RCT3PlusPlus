// SPDX-License-Identifier: GPL-3.0-or-later
//
// The platform layer: GLFW, the window and the platform half of Dear ImGui. Owns the ImGui context, so it is
// created before the render device and destroyed after it.
#pragma once

#include "render/device.hpp"
#include "ui/keyboard.hpp"

#include <expected>
#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

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
            bool right = false;
            bool middle = false;
            float wheel_x = 0;   ///< Notches turned sideways since the last poll_events, positive to the left
            float wheel_y = 0;   ///< Notches turned since the last poll_events, positive away from the user
            bool inside = false; ///< Over the window's content, with the window focused
        };
        [[nodiscard]] Pointer pointer() const;
        /// Whether `key` is held down now; for keys that act for as long as they are held.
        [[nodiscard]] bool held(ui::Key key) const;
        /// Keys pressed and text typed since the last poll_events, in order.
        [[nodiscard]] const ui::KeyboardState& keyboard() const
        {
            return collected_->keyboard;
        }

        /// What the system clipboard holds, if it is text.
        [[nodiscard]] std::u16string clipboard() const;
        /// Puts `text` on the system clipboard.
        void set_clipboard(std::u16string_view text);

        struct Size
        {
            int width = 0;
            int height = 0;

            friend bool operator==(const Size&, const Size&) = default;
        };
        /// The sizes the main screen can show, smallest first.
        [[nodiscard]] std::vector<Size> screen_sizes() const;
        /// The window's size in screen coordinates.
        [[nodiscard]] Size size() const;
        /// In full screen, also the screen's mode.
        void set_size(Size size);
        /// Full screen on the main screen at the window's size, or back in a window.
        void set_fullscreen(bool on);

        // Input GLFW only reports through callbacks, gathered between polls. On the heap so the window's
        // user pointer stays valid when the Window moves; public so the callbacks can reach it.
        struct Collected
        {
            double wheel_x = 0;
            double wheel_y = 0;
            ui::KeyboardState keyboard;
        };

        /// The platform half of starting an ImGui frame; call after the device's `ui_new_frame`.
        void ui_new_frame();

    private:
        Window(GLFWwindow* window, std::unique_ptr<Collected> collected)
            : window_(window)
            , collected_(std::move(collected))
        {
        }

        GLFWwindow* window_ = nullptr;
        std::unique_ptr<Collected> collected_;
    };

} // namespace rct3::platform
