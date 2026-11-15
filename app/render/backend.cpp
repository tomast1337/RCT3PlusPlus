// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/backend.hpp"

#include "render/null/null_device.hpp"
#include "render/opengl/device.hpp"
#include "render/software/device.hpp"
#include "render/vulkan/device.hpp"

#include <GLFW/glfw3.h>
#include <array>

namespace rct3::render
{

    namespace
    {
        struct BackendInfo
        {
            Backend backend;
            std::string_view name;
        };

        constexpr std::array kBackends = {
            BackendInfo { Backend::Vulkan, "vulkan" },
            BackendInfo { Backend::OpenGl, "opengl" },
            BackendInfo { Backend::Software, "software" },
            BackendInfo { Backend::Null, "null" },
        };

        constexpr auto kBackendIds = [] {
            std::array<Backend, kBackends.size()> ids { };
            for (std::size_t i = 0; i < kBackends.size(); ++i)
                ids[i] = kBackends[i].backend;
            return ids;
        }();
    } // namespace

    std::span<const Backend> backends()
    {
        return kBackendIds;
    }

    std::string_view backend_name(Backend backend)
    {
        for (const auto& info : kBackends)
        {
            if (info.backend == backend)
                return info.name;
        }
        return "unknown";
    }

    std::optional<Backend> parse_backend(std::string_view name)
    {
        for (const auto& info : kBackends)
        {
            if (info.name == name)
                return info.backend;
        }
        return std::nullopt;
    }

    void set_window_hints(Backend backend)
    {
        switch (backend)
        {
            case Backend::Vulkan:
            case Backend::Null:
                // Neither wants GLFW to create an OpenGL context.
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                break;
            case Backend::OpenGl:
                glfwWindowHint(GLFW_CLIENT_API, GLFW_OPENGL_API);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MAJOR, 4);
                glfwWindowHint(GLFW_CONTEXT_VERSION_MINOR, 5);
                glfwWindowHint(GLFW_OPENGL_PROFILE, GLFW_OPENGL_CORE_PROFILE);
                glfwWindowHint(GLFW_OPENGL_FORWARD_COMPAT, GLFW_TRUE);
                // The window only shows the stretched screen, which has its own depth.
                glfwWindowHint(GLFW_DEPTH_BITS, 0);
                glfwWindowHint(GLFW_STENCIL_BITS, 0);
                // Without a display (--headless) only EGL makes a context.
                if (glfwGetPlatform() == GLFW_PLATFORM_NULL)
                    glfwWindowHint(GLFW_CONTEXT_CREATION_API, GLFW_EGL_CONTEXT_API);
                break;
            case Backend::Software:
                // A window shows its frames through any OpenGL context; without a display, no context and no
                // graphics driver is needed at all.
                glfwWindowHint(GLFW_CLIENT_API, glfwGetPlatform() == GLFW_PLATFORM_NULL ? GLFW_NO_API : GLFW_OPENGL_API);
                glfwWindowHint(GLFW_DEPTH_BITS, 0);
                glfwWindowHint(GLFW_STENCIL_BITS, 0);
                break;
        }
    }

    std::unique_ptr<Device> make_device(Backend backend)
    {
        switch (backend)
        {
            case Backend::Vulkan:
                return std::make_unique<vulkan::VulkanDevice>();
            case Backend::OpenGl:
                return std::make_unique<opengl::OpenGlDevice>();
            case Backend::Software:
                return std::make_unique<software::SoftwareDevice>();
            case Backend::Null:
                return std::make_unique<null::NullDevice>();
        }
        return nullptr;
    }

} // namespace rct3::render
