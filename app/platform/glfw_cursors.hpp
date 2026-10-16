// SPDX-License-Identifier: GPL-3.0-or-later
//
// The cursor backend for GLFW (see ui/cursor.hpp): the system's standard cursors, made once for a window.
// GLFW 3.4 added the four-way, diagonal and not-allowed shapes; built against an older GLFW, or on a
// system whose cursor theme lacks one, the shape is simply not available and its kinds fall back.
// A backend for another platform library provides the same two members, available() and show().
#pragma once

#include "ui/cursor.hpp"

#include <array>

struct GLFWwindow;
struct GLFWcursor;

namespace rct3::platform
{

    class GlfwCursors
    {
    public:
        /// Makes the standard cursors GLFW has for `window`, which must outlive this.
        explicit GlfwCursors(GLFWwindow* window);
        GlfwCursors(GlfwCursors&& other) noexcept;
        GlfwCursors& operator=(GlfwCursors&&) = delete;
        GlfwCursors(const GlfwCursors&) = delete;
        GlfwCursors& operator=(const GlfwCursors&) = delete;
        ~GlfwCursors();

        [[nodiscard]] ui::StandardCursorSet available() const
        {
            return available_;
        }
        /// Shows `shape` over the window; Hidden also holds the pointer for mouse look.
        void show(ui::StandardCursor shape);

    private:
        GLFWwindow* window_ = nullptr;
        std::array<GLFWcursor*, ui::kStandardCursors> cursors_ { };
        ui::StandardCursorSet available_;
    };
    static_assert(ui::CursorBackend<GlfwCursors>);

} // namespace rct3::platform
