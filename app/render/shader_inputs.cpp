// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/shader_inputs.hpp"

#include <algorithm>

namespace rct3::render
{

    ShaderConstants shader_constants(const DrawCommand& command, Shader shader)
    {
        const SceneLight& l = command.light;
        if (shader == Shader::Occlusion)
        {
            // app/shaders/occlusion.slang
            const OcclusionStep& o = command.occlusion;
            return { command.transform, glm::vec4(static_cast<float>(o.kind), static_cast<float>(o.frame % 65536), o.radius, o.strength), glm::vec4(o.history, 0.0f, o.depth_terms), { }, { } };
        }
        if (shader != Shader::Water && shader != Shader::Waterfall)
        {
            // The block is full, so the haze colour goes as one number: its channels in 8 bits each.
            const glm::uvec3 h { glm::round(glm::clamp(l.haze, 0.0f, 1.0f) * 255.0f) };
            const auto haze = static_cast<float>((h.r << 16) | (h.g << 8) | h.b);
            // sky.w: 1 with the variation, 2 with the relief map, plus 4 times the surface detail, plus 16 with
            // lamps, plus 32 for a ghost, plus 64 to leave nearly clear pixels out, plus `v_shift` (lit.slang). sun_direction.w: 0 without a shadow, else 1 plus the older shadow's weight.
            const bool before = static_cast<bool>(l.shadow_map_before);
            const float shadow = l.shadow_map && l.shadow_view ? 1 + (before ? std::clamp(l.shadow_before, 0.0f, 1.0f) : 0.0f) : 0.0f;
            return { command.transform, glm::vec4(l.sun_direction, shadow), glm::vec4(l.sun, l.haze_rate), glm::vec4(l.sky, (command.extra_textures[0] ? 1.0f : 0.0f) + (command.extra_textures[1] ? 2.0f : 0.0f) + 4.0f * static_cast<float>(std::clamp(l.surface_detail, 0, 2)) + (l.lamps ? 16.0f : 0.0f) + (l.ghost ? 32.0f : 0.0f) + (l.cut_clear ? 64.0f : 0.0f) + command.v_shift.lengths), glm::vec4(l.ground, haze) };
        }
        // sky.w: the flags, plus `v_shift` (water.slang, waterfall.slang).
        const WaterView& w = command.water;
        const bool shadow = l.shadow_map && l.shadow_view;
        const auto flags = static_cast<float>((w.reflection ? 1 : 0) | (w.refraction ? 2 : 0) | (w.waves ? 4 : 0) | (w.foam ? 8 : 0) | (w.ripples ? 16 : 0) | (w.underwater ? 32 : 0) | (shadow ? 64 : 0) | (w.shafts ? 128 : 0) | (w.flow ? 256 : 0) | (std::clamp(w.fall_part, 0, 3) << 9) | (std::clamp(w.fall_detail, 0, 3) << 11)) + command.v_shift.lengths;
        return { command.transform, glm::vec4(w.eye, w.time), glm::vec4(l.sun_direction, w.near), glm::vec4(l.sun, w.far), glm::vec4(l.sky, flags) };
    }

    std::array<TextureHandle, kExtraTextures> bound_extra_textures(const DrawCommand& command, Shader shader)
    {
        // The Water and Waterfall shaders read the extra textures, the Lit ones the shadow.
        if (lit_shader(shader))
            return { command.light.shadow_map, command.light.shadow_view, command.extra_textures[0], command.light.shadow_map_before, command.light.lamps, command.extra_textures[1] };
        std::array<TextureHandle, kExtraTextures> extras = command.extra_textures;
        if (shader == Shader::Water || shader == Shader::Waterfall)
        {
            extras[4] = command.light.shadow_map;
            extras[5] = command.light.shadow_view;
        }
        return extras;
    }

} // namespace rct3::render
