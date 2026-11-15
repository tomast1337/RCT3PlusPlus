// SPDX-License-Identifier: GPL-3.0-or-later
//
// What the shader programs in app/shaders read for a draw besides its vertices, for every backend built from
// them: the block of constants (Vulkan's push constants, OpenGL's uniform block 0) and the textures bound after
// the draw's own.
#pragma once

#include "render/types.hpp"

#include <array>

namespace rct3::render
{

    /// The draw's transform, then the light of the Lit shaders, or what the Water shader needs
    /// (app/shaders/water.slang). 128 bytes is all a Vulkan device must offer for push constants.
    struct ShaderConstants
    {
        glm::mat4 transform { 1.0f };
        glm::vec4 a { };
        glm::vec4 b { };
        glm::vec4 c { };
        glm::vec4 d { };
    };
    static_assert(sizeof(ShaderConstants) == 128);

    [[nodiscard]] ShaderConstants shader_constants(const DrawCommand& command, Shader shader);

    /// The textures bound after `command.texture`, in the order the program for `shader` reads them (Vulkan
    /// descriptor set 2 onward, OpenGL texture unit 2 onward). Missing ones read as `command.texture`.
    [[nodiscard]] std::array<TextureHandle, kExtraTextures> bound_extra_textures(const DrawCommand& command, Shader shader);

} // namespace rct3::render
