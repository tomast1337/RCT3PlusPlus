// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/test_scene.hpp"

#include <array>
#include <cstdint>
#include <span>

namespace rct3::scene
{

    namespace
    {

        struct Vertex
        {
            glm::vec3 position;
            glm::vec4 color;
        };

        // Clockwise as seen from the camera, so these are front faces.
        constexpr std::array<Vertex, 3> kTriangle = { {
            { { 0.0f, 0.6f, 0.0f }, { 1.0f, 0.2f, 0.2f, 1.0f } },
            { { 0.52f, -0.3f, 0.0f }, { 0.2f, 1.0f, 0.2f, 1.0f } },
            { { -0.52f, -0.3f, 0.0f }, { 0.2f, 0.4f, 1.0f, 1.0f } },
        } };

        constexpr std::array<Vertex, 4> kQuad = { {
            { { -0.5f, 0.5f, 0.0f }, { 0.25f, 0.25f, 0.30f, 1.0f } },
            { { 0.5f, 0.5f, 0.0f }, { 0.25f, 0.25f, 0.30f, 1.0f } },
            { { 0.5f, -0.5f, 0.0f }, { 0.45f, 0.45f, 0.50f, 1.0f } },
            { { -0.5f, -0.5f, 0.0f }, { 0.45f, 0.45f, 0.50f, 1.0f } },
        } };
        constexpr std::array<std::uint16_t, 6> kQuadIndices = { 0, 1, 2, 0, 2, 3 };

        constexpr std::array<render::VertexAttribute, 2> kVertexAttributes = { {
            { 0, render::VertexFormat::Float3, offsetof(Vertex, position) },
            { 1, render::VertexFormat::Float4, offsetof(Vertex, color) },
        } };

        template<typename T, std::size_t N> std::span<const std::byte> bytes_of(const std::array<T, N>& values)
        {
            return std::as_bytes(std::span(values));
        }

        glm::mat4 projection(render::Extent extent)
        {
            const float aspect = extent.height == 0 ? 1.0f : static_cast<float>(extent.width) / static_cast<float>(extent.height);
            return glm::perspective(glm::radians(60.0f), aspect, 0.1f, 100.0f);
        }

    } // namespace

    std::expected<TestScene, std::string> create_test_scene(render::Device& device)
    {
        TestScene scene;
        auto fail = [&](std::string error) {
            destroy_test_scene(device, scene);
            return std::unexpected(std::move(error));
        };

        const render::PipelineDesc pipeline {
            .shader = render::Shader::VertexColor,
            .layout = { sizeof(Vertex), kVertexAttributes },
            .cull = render::CullMode::Back,
            .label = "test scene",
        };
        auto created_pipeline = device.create_pipeline(pipeline);
        if (!created_pipeline)
            return fail(created_pipeline.error());
        scene.pipeline = *created_pipeline;

        auto triangle = device.create_buffer({ render::BufferUsage::Vertex, bytes_of(kTriangle), "triangle" });
        if (!triangle)
            return fail(triangle.error());
        scene.triangle = *triangle;

        auto quad = device.create_buffer({ render::BufferUsage::Vertex, bytes_of(kQuad), "quad" });
        if (!quad)
            return fail(quad.error());
        scene.quad_vertices = *quad;

        auto indices = device.create_buffer({ render::BufferUsage::Index, bytes_of(kQuadIndices), "quad indices" });
        if (!indices)
            return fail(indices.error());
        scene.quad_indices = *indices;
        return scene;
    }

    void draw_test_scene(render::Device& device, const TestScene& scene, double seconds)
    {
        const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, -2.5f), glm::vec3(0, 0, 0), glm::vec3(0, 1, 0));
        const glm::mat4 view_projection = projection(device.extent()) * view;
        const auto angle = static_cast<float>(seconds);

        // The quad sits behind the triangle; depth testing keeps it there whatever the draw order.
        render::DrawCommand quad;
        quad.pipeline = scene.pipeline;
        quad.vertices = { scene.quad_vertices };
        quad.indices = { scene.quad_indices };
        quad.count = static_cast<std::uint32_t>(kQuadIndices.size());
        quad.transform = view_projection * glm::translate(glm::mat4(1.0f), { 0, 0, 0.5f }) * glm::scale(glm::mat4(1.0f), { 2.4f, 1.6f, 1.0f });

        render::DrawCommand triangle;
        triangle.pipeline = scene.pipeline;
        triangle.vertices = { scene.triangle };
        triangle.count = static_cast<std::uint32_t>(kTriangle.size());
        triangle.transform = view_projection * glm::rotate(glm::mat4(1.0f), angle, { 0, 0, 1 });

        device.draw(triangle);
        device.draw(quad);
    }

    void destroy_test_scene(render::Device& device, TestScene& scene)
    {
        device.destroy_buffer(scene.quad_indices);
        device.destroy_buffer(scene.quad_vertices);
        device.destroy_buffer(scene.triangle);
        device.destroy_pipeline(scene.pipeline);
        scene = { };
    }

} // namespace rct3::scene
