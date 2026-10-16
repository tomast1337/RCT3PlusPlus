// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/window.hpp"

#include "backends/imgui_impl_glfw.h"
#include "imgui.h"
#include "logging/log.hpp"
#include "render/backend.hpp"
#include "strings/unicode.hpp"

#include <GLFW/glfw3.h>
#include <algorithm>
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
                case GLFW_KEY_INSERT:
                    return ui::Key::Insert;
                default:
                    if (key >= GLFW_KEY_F1 && key <= GLFW_KEY_F12)
                        return static_cast<ui::Key>(static_cast<int>(ui::Key::F1) + (key - GLFW_KEY_F1));
                    return std::nullopt;
            }
        }

        Window::Collected& collected_of(GLFWwindow* w)
        {
            return *static_cast<Window::Collected*>(glfwGetWindowUserPointer(w));
        }

    } // namespace

    std::expected<Window, std::string> Window::open(const WindowConfig& config, render::Backend backend)
    {
        glfwInitHint(GLFW_PLATFORM, config.headless ? GLFW_PLATFORM_NULL : GLFW_ANY_PLATFORM);
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
            if (action == GLFW_RELEASE)
                return;
            const bool shift = (mods & GLFW_MOD_SHIFT) != 0;
            const bool control = (mods & GLFW_MOD_CONTROL) != 0;
            const bool alt = (mods & GLFW_MOD_ALT) != 0;
            auto& presses = collected_of(w).keyboard.presses;
            // Control+letter types nothing, so it comes as a shortcut; without Control the letter comes as
            // well as the text it types, for keys bound to the game. GLFW names keys by the US layout; the
            // layout's own letter would need the key name, which is a later refinement.
            const ui::Key letter = control ? ui::Key::Shortcut : ui::Key::Letter;
            if (const auto mapped = interface_key(key))
                presses.push_back({ *mapped, shift, control, 0, { }, alt });
            else if (key >= GLFW_KEY_A && key <= GLFW_KEY_Z)
                presses.push_back({ letter, shift, control, static_cast<char32_t>(U'a' + (key - GLFW_KEY_A)), { }, alt });
            else if (key >= GLFW_KEY_0 && key <= GLFW_KEY_9)
                presses.push_back({ letter, shift, control, static_cast<char32_t>(U'0' + (key - GLFW_KEY_0)), { }, alt });
        });
        // Text in the order it was typed, between the key presses. Input method text arrives here once
        // confirmed; GLFW does not report what is still being composed, so the composition stays empty.
        glfwSetCharCallback(window, [](GLFWwindow* w, unsigned int c) { collected_of(w).keyboard.presses.push_back({ ui::Key::Character, false, false, static_cast<char32_t>(c) }); });

        IMGUI_CHECKVERSION();
        ImGui::CreateContext();
        ImGui::StyleColorsDark();
        // "Other" fits every backend that does not need GLFW to manage an OpenGL context.
        ImGui_ImplGlfw_InitForOther(window, true);
        // The game sets the cursor itself (set_cursor), from what the developer windows ask for among the rest.
        ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
        // The game reads the button state once a frame; a click shorter than a frame must still show up.
        glfwSetInputMode(window, GLFW_STICKY_MOUSE_BUTTONS, GLFW_TRUE);
        return Window(window, std::move(collected));
    }

    Window::Window(Window&& other) noexcept
        : window_(std::exchange(other.window_, nullptr))
        , collected_(std::move(other.collected_))
        , cursors_(std::move(other.cursors_))
        , cursor_(other.cursor_)
    {
    }

    Window::~Window()
    {
        if (window_ == nullptr)
            return;
        cursors_.reset();
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
        glfwPollEvents();
    }

    std::u16string Window::clipboard() const
    {
        const char* text = glfwGetClipboardString(window_);
        return text != nullptr ? strings::to_utf16(text) : std::u16string { };
    }

    void Window::set_clipboard(std::u16string_view text)
    {
        glfwSetClipboardString(window_, strings::to_utf8(text).c_str());
    }

    std::vector<Window::Size> Window::screen_sizes() const
    {
        std::vector<Size> sizes;
        GLFWmonitor* monitor = glfwGetPrimaryMonitor();
        int count = 0;
        const GLFWvidmode* modes = monitor != nullptr ? glfwGetVideoModes(monitor, &count) : nullptr;
        for (int i = 0; i < count; ++i)
        {
            const Size s { modes[i].width, modes[i].height };
            if (std::ranges::find(sizes, s) == sizes.end())
                sizes.push_back(s);
        }
        std::ranges::sort(sizes, [](Size a, Size b) { return std::pair { a.width, a.height } < std::pair { b.width, b.height }; });
        return sizes;
    }

    Window::Size Window::size() const
    {
        Size s;
        glfwGetWindowSize(window_, &s.width, &s.height);
        return s;
    }

    void Window::set_size(Size size)
    {
        if (GLFWmonitor* monitor = glfwGetWindowMonitor(window_))
            glfwSetWindowMonitor(window_, monitor, 0, 0, size.width, size.height, GLFW_DONT_CARE);
        else
            glfwSetWindowSize(window_, size.width, size.height);
    }

    void Window::set_fullscreen(bool on)
    {
        if ((glfwGetWindowMonitor(window_) != nullptr) == on)
            return;
        const Size s = size();
        if (on)
        {
            GLFWmonitor* monitor = glfwGetPrimaryMonitor();
            if (monitor != nullptr)
                glfwSetWindowMonitor(window_, monitor, 0, 0, s.width, s.height, GLFW_DONT_CARE);
        }
        else
        {
            constexpr int kWindowedPosition = 64; // Clear of the screen's corner and its title bar
            glfwSetWindowMonitor(window_, nullptr, kWindowedPosition, kWindowedPosition, s.width, s.height, GLFW_DONT_CARE);
        }
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
        p.right = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
        p.middle = glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_MIDDLE) == GLFW_PRESS;
        p.wheel_x = static_cast<float>(collected_->wheel_x);
        p.wheel_y = static_cast<float>(collected_->wheel_y);
        p.inside = glfwGetWindowAttrib(window_, GLFW_HOVERED) != 0 && glfwGetWindowAttrib(window_, GLFW_FOCUSED) != 0;
        return p;
    }

    bool Window::held(ui::Key key) const
    {
        for (const int k : { GLFW_KEY_LEFT, GLFW_KEY_RIGHT, GLFW_KEY_UP, GLFW_KEY_DOWN, GLFW_KEY_PAGE_UP, GLFW_KEY_PAGE_DOWN })
        {
            if (interface_key(k) == key)
                return glfwGetKey(window_, k) == GLFW_PRESS;
        }
        return false;
    }

    bool Window::held(const ui::KeyCombo& combo) const
    {
        const auto down = [&](int k) { return glfwGetKey(window_, k) == GLFW_PRESS; };
        const bool control = down(GLFW_KEY_LEFT_CONTROL) || down(GLFW_KEY_RIGHT_CONTROL);
        const bool shift = down(GLFW_KEY_LEFT_SHIFT) || down(GLFW_KEY_RIGHT_SHIFT);
        const bool alt = down(GLFW_KEY_LEFT_ALT) || down(GLFW_KEY_RIGHT_ALT);
        if (control != combo.control || shift != combo.shift || alt != combo.alt)
            return false;
        if (combo.key == ui::Key::Letter || combo.key == ui::Key::Shortcut)
        {
            // By the US layout, as presses are named (see the key callback).
            const char32_t c = combo.character;
            if (c >= U'a' && c <= U'z')
                return down(GLFW_KEY_A + static_cast<int>(c - U'a'));
            if (c >= U'0' && c <= U'9')
                return down(GLFW_KEY_0 + static_cast<int>(c - U'0'));
            return false;
        }
        for (int k = GLFW_KEY_SPACE; k <= GLFW_KEY_LAST; ++k)
        {
            if (interface_key(k) == combo.key && down(k))
                return true;
        }
        return false;
    }

    void Window::ui_new_frame()
    {
        ImGui_ImplGlfw_NewFrame();
    }

    void Window::set_cursor(ui::CursorKind kind)
    {
        if (!cursors_)
            return;
        if (const auto shown = cursor_.set(*cursors_, kind))
            logging::logger("platform").debug("pointer: {} as {}", ui::cursor_name(kind), ui::standard_cursor_name(*shown));
    }

    ui::CursorKind Window::developer_cursor()
    {
        switch (ImGui::GetMouseCursor())
        {
            case ImGuiMouseCursor_TextInput:
                return ui::CursorKind::Text;
            case ImGuiMouseCursor_ResizeAll:
                return ui::CursorKind::Move;
            case ImGuiMouseCursor_ResizeNS:
                return ui::CursorKind::ResizeVertical;
            case ImGuiMouseCursor_ResizeEW:
                return ui::CursorKind::ResizeHorizontal;
            case ImGuiMouseCursor_ResizeNESW:
                return ui::CursorKind::ResizeDiagonalUp;
            case ImGuiMouseCursor_ResizeNWSE:
                return ui::CursorKind::ResizeDiagonalDown;
            case ImGuiMouseCursor_Hand:
                return ui::CursorKind::Hand;
            case ImGuiMouseCursor_NotAllowed:
                return ui::CursorKind::NotAllowed;
            default:
                return ui::CursorKind::Arrow;
        }
    }

} // namespace rct3::platform
