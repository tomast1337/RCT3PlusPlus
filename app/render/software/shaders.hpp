// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software backend's shader programs: C++ versions of app/shaders, read from the same constants and
// textures (render/shader_inputs.hpp). slangc's CPU target cannot build them: it has no fragment stage, discard
// or screen-space derivatives. Each keeps its Slang source's look, some with less detail; the differences are
// noted where they are made.
#pragma once

#include "render/software/raster.hpp"

namespace rct3::render::software
{

    /// The stages for `shader`.
    [[nodiscard]] Program program(Shader shader);

    /// Where the transform's eye is, w 1; w 0 in a view without perspective (eye_of in app/shaders/common.slang).
    [[nodiscard]] glm::vec4 eye_of(const glm::mat4& m);

} // namespace rct3::render::software
