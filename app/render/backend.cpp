// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/backend.hpp"

#include "render/null/null_device.hpp"
#include "render/vulkan/vulkan_device.hpp"

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
                // Neither wants GLFW to create an OpenGL context. An OpenGL backend would ask for one here.
                glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
                break;
        }
    }

    std::unique_ptr<Device> make_device(Backend backend)
    {
        switch (backend)
        {
            case Backend::Vulkan:
                return std::make_unique<vulkan::VulkanDevice>();
            case Backend::Null:
                return std::make_unique<null::NullDevice>();
        }
        return nullptr;
    }

} // namespace rct3::render
