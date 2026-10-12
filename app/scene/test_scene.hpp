// SPDX-License-Identifier: GPL-3.0-or-later
//
// A placeholder scene that exercises the render API until real game content is drawn: a spinning triangle
// in front of an indexed quad, so vertex buffers, index buffers, transforms and depth testing all show up.
#pragma once

#include "render/device.hpp"

#include <expected>
#include <string>

namespace rct3::scene
{

    struct TestScene
    {
        render::PipelineHandle pipeline;
        render::BufferHandle triangle;
        render::BufferHandle quad_vertices;
        render::BufferHandle quad_indices;
    };

    [[nodiscard]] std::expected<TestScene, std::string> create_test_scene(render::Device& device);

    /// Records the scene's draws into the current pass. `seconds` drives the animation.
    void draw_test_scene(render::Device& device, const TestScene& scene, double seconds);

    void destroy_test_scene(render::Device& device, TestScene& scene);

} // namespace rct3::scene
