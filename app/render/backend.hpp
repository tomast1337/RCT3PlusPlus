// SPDX-License-Identifier: GPL-3.0-or-later
//
// Choosing a render backend: names for the command line, the window hints a backend needs before the window
// exists, and the factory. This is the only file that knows every backend.
#pragma once

#include "render/device.hpp"

#include <memory>
#include <optional>
#include <span>
#include <string_view>

namespace rct3::render
{

    /// Every backend this build supports, in order of preference.
    [[nodiscard]] std::span<const Backend> backends();

    [[nodiscard]] std::string_view backend_name(Backend backend);

    /// Case-sensitive, using the names from `backend_name`.
    [[nodiscard]] std::optional<Backend> parse_backend(std::string_view name);

    /// Sets the GLFW window hints the backend needs. Call after glfwInit and before glfwCreateWindow.
    void set_window_hints(Backend backend);

    /// Makes an uninitialised device; call `init` next.
    [[nodiscard]] std::unique_ptr<Device> make_device(Backend backend);

} // namespace rct3::render
