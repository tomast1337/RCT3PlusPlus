// SPDX-License-Identifier: GPL-3.0-or-later
//
// The single way to include GLM. The configuration macros change type layouts and conventions, so every
// translation unit must see the same ones: include this header, not <glm/glm.hpp>.
//
// Conventions:
//  - Left-handed, Y up, like the original game's Direct3D-style data.
//  - Clip space depth runs 0..1.
//  - GLM matrices are column-major and transform column vectors (M * v). The original data stores row-major
//    matrices that transform row vectors (v * M); the memory layout is identical, so copying the 16 floats
//    straight into a glm::mat4 yields the same transform in GLM's convention (translation in the last column).
#pragma once

#ifndef GLM_FORCE_LEFT_HANDED
    #define GLM_FORCE_LEFT_HANDED
#endif
#ifndef GLM_FORCE_DEPTH_ZERO_TO_ONE
    #define GLM_FORCE_DEPTH_ZERO_TO_ONE
#endif

#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/quaternion.hpp>
