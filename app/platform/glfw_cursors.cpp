// SPDX-License-Identifier: GPL-3.0-or-later
#include "platform/glfw_cursors.hpp"

#include "logging/log.hpp"

#include <GLFW/glfw3.h>
#include <optional>
#include <string>
#include <utility>

// The four-way, diagonal and not-allowed shapes came with GLFW 3.4.
#if GLFW_VERSION_MAJOR > 3 || (GLFW_VERSION_MAJOR == 3 && GLFW_VERSION_MINOR >= 4)
    #define RCT3PP_GLFW_HAS_MORE_CURSORS 1
#else
    #define RCT3PP_GLFW_HAS_MORE_CURSORS 0
#endif

namespace rct3::platform
{

    namespace
    {

        // GLFW's shape for each standard cursor; none where this GLFW has no such shape or it is not a shape.
        std::optional<int> glfw_shape(ui::StandardCursor shape)
        {
            switch (shape)
            {
                case ui::StandardCursor::Arrow:
                    return GLFW_ARROW_CURSOR;
                case ui::StandardCursor::PointingHand:
                    return GLFW_HAND_CURSOR;
                case ui::StandardCursor::IBeam:
                    return GLFW_IBEAM_CURSOR;
                case ui::StandardCursor::Crosshair:
                    return GLFW_CROSSHAIR_CURSOR;
                case ui::StandardCursor::ResizeEW:
                    return GLFW_HRESIZE_CURSOR;
                case ui::StandardCursor::ResizeNS:
                    return GLFW_VRESIZE_CURSOR;
#if RCT3PP_GLFW_HAS_MORE_CURSORS
                case ui::StandardCursor::NotAllowed:
                    return GLFW_NOT_ALLOWED_CURSOR;
                case ui::StandardCursor::ResizeAll:
                    return GLFW_RESIZE_ALL_CURSOR;
                case ui::StandardCursor::ResizeNWSE:
                    return GLFW_RESIZE_NWSE_CURSOR;
                case ui::StandardCursor::ResizeNESW:
                    return GLFW_RESIZE_NESW_CURSOR;
#else
                case ui::StandardCursor::NotAllowed:
                case ui::StandardCursor::ResizeAll:
                case ui::StandardCursor::ResizeNWSE:
                case ui::StandardCursor::ResizeNESW:
#endif
                case ui::StandardCursor::Hidden:
                    break;
            }
            return std::nullopt;
        }

    } // namespace

    GlfwCursors::GlfwCursors(GLFWwindow* window)
        : window_(window)
        , available_(ui::required_cursors())
    {
        std::string missing;
        for (std::size_t i = 0; i < ui::kStandardCursors; ++i)
        {
            const auto shape = static_cast<ui::StandardCursor>(i);
            const std::optional<int> glfw = glfw_shape(shape);
            if (!glfw)
                continue;
            // Null when the platform or its cursor theme lacks the shape, or the GLFW library at run time is
            // older than the header. The arrow then stays GLFW's default cursor.
            cursors_.at(i) = glfwCreateStandardCursor(*glfw);
            if (cursors_.at(i) != nullptr)
                available_.set(i);
            else if (shape != ui::StandardCursor::Arrow)
                missing += std::string(missing.empty() ? "" : ", ") + std::string(ui::standard_cursor_name(shape));
        }
        if (!missing.empty())
            logging::logger("platform").info("standard cursors {} not available; their kinds fall back", missing);
    }

    GlfwCursors::GlfwCursors(GlfwCursors&& other) noexcept
        : window_(std::exchange(other.window_, nullptr))
        , cursors_(std::exchange(other.cursors_, { }))
        , available_(other.available_)
    {
    }

    GlfwCursors::~GlfwCursors()
    {
        // Destroying a cursor in use puts the window back to the default arrow, so no reset is needed.
        for (GLFWcursor* cursor : cursors_)
        {
            if (cursor != nullptr)
                glfwDestroyCursor(cursor);
        }
    }

    void GlfwCursors::show(ui::StandardCursor shape)
    {
        if (window_ == nullptr)
            return;
        if (shape == ui::StandardCursor::Hidden)
        {
            // Disabled rather than hidden: GLFW then reports unbounded motion and puts the pointer back where
            // it was when it is shown again.
            glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_DISABLED);
            return;
        }
        glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
        glfwSetCursor(window_, cursors_.at(static_cast<std::size_t>(shape)));
    }

} // namespace rct3::platform
