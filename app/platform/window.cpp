// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/window.hpp"

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"
#include "render/backend.hpp"

#include <GLFW/glfw3.h>
#include <optional>
#include <utility>

namespace rct3::platform
{

    namespace
    {

        std::optional<ui::Key> interface_key(int key)
        {
            switch (key)
            {
                case GLFW_KEY_LEFT:
                    return ui::Key::Left;
                case GLFW_KEY_RIGHT:
                    return ui::Key::Right;
                case GLFW_KEY_UP:
                    return ui::Key::Up;
                case GLFW_KEY_DOWN:
                    return ui::Key::Down;
                case GLFW_KEY_PAGE_UP:
                    return ui::Key::PageUp;
                case GLFW_KEY_PAGE_DOWN:
                    return ui::Key::PageDown;
                case GLFW_KEY_HOME:
                    return ui::Key::Home;
                case GLFW_KEY_END:
                    return ui::Key::End;
                case GLFW_KEY_TAB:
                    return ui::Key::Tab;
                case GLFW_KEY_ESCAPE:
                    return ui::Key::Escape;
                case GLFW_KEY_ENTER:
                case GLFW_KEY_KP_ENTER:
                    return ui::Key::Enter;
                case GLFW_KEY_BACKSPACE:
                    return ui::Key::Backspace;
                case GLFW_KEY_DELETE:
                    return ui::Key::Delete;
                case GLFW_KEY_SPACE:
                    return ui::Key::Space;
                default:
                    return std::nullopt;
            }
        }

        void append_utf16(std::u16string& out, unsigned int c)
        {
            if (c < 0x10000)
            {
                out += static_cast<char16_t>(c);
                return;
            }
            c -= 0x10000;
            out += static_cast<char16_t>(0xD800 + (c >> 10));
            out += static_cast<char16_t>(0xDC00 + (c & 0x3FF));
        }

        Window::Collected& collected_of(GLFWwindow* w)
        {
            return *static_cast<Window::Collected*>(glfwGetWindowUserPointer(w));
        }

    } // namespace

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
        glfwSetScrollCallback(window, [](GLFWwindow* w, double x, double y) {
            Collected* collected = &collected_of(w);
            // Shift turns the wheel sideways, as in most programs.
            const bool shift = glfwGetKey(w, GLFW_KEY_LEFT_SHIFT) == GLFW_PRESS || glfwGetKey(w, GLFW_KEY_RIGHT_SHIFT) == GLFW_PRESS;
            collected->wheel_x += shift ? x + y : x;
            collected->wheel_y += shift ? 0 : y;
        });
        // Presses and the system's repeats, not releases.
        glfwSetKeyCallback(window, [](GLFWwindow* w, int key, int, int action, int mods) {
            const auto mapped = interface_key(key);
            if (action != GLFW_RELEASE && mapped)
                collected_of(w).keyboard.presses.push_back({ *mapped, (mods & GLFW_MOD_SHIFT) != 0, (mods & GLFW_MOD_CONTROL) != 0 });
        });
        glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int c) { append_utf16(collected_of(w).keyboard.text, c); });

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
        collected_->wheel_x = 0;
        collected_->wheel_y = 0;
        collected_->keyboard.presses.clear();
        collected_->keyboard.text.clear();
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
        p.wheel_x = static_cast<float>(collected_->wheel_x);
        p.wheel_y = static_cast<float>(collected_->wheel_y);
        return p;
    }

    void Window::ui_new_frame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

} // namespace rct3::platform
