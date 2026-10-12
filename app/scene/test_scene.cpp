// SPDX-License-Identifier: GPL-3.0-or-later
#include "scene/test_scene.hpp"

#include <array>
#include <cmath>
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

        // Screen-space vertex as 2D drawing will use it: pixels, packed colour, texture coordinate.
        struct OverlayVertex
        {
            glm::vec2 position;
            std::array<std::uint8_t, 4> color;
            glm::vec2 uv;
        };

        constexpr std::array<render::VertexAttribute, 3> kOverlayAttributes = { {
            { 0, render::VertexFormat::Float2, offsetof(OverlayVertex, position) },
            { 1, render::VertexFormat::UByte4Norm, offsetof(OverlayVertex, color) },
            { 2, render::VertexFormat::Float2, offsetof(OverlayVertex, uv) },
        } };

        // Two-colour checkerboard with a transparent border, so filtering, clamping and blending are visible.
        constexpr std::uint32_t kCheckerSize = 16;
        std::array<std::byte, render::texture_bytes(kCheckerSize, kCheckerSize, render::TextureFormat::Rgba8)> make_checker()
        {
            std::array<std::byte, render::texture_bytes(kCheckerSize, kCheckerSize, render::TextureFormat::Rgba8)> pixels { };
            for (std::uint32_t y = 0; y < kCheckerSize; ++y)
            {
                for (std::uint32_t x = 0; x < kCheckerSize; ++x)
                {
                    const bool border = x == 0 || y == 0 || x == kCheckerSize - 1 || y == kCheckerSize - 1;
                    const bool light = ((x / 4) + (y / 4)) % 2 == 0;
                    const std::uint8_t value = light ? 230 : 60;
                    std::byte* p = &pixels[(y * kCheckerSize + x) * 4];
                    p[0] = std::byte { value };
                    p[1] = std::byte { value };
                    p[2] = std::byte { static_cast<std::uint8_t>(light ? 120 : 200) };
                    p[3] = std::byte { static_cast<std::uint8_t>(border ? 0 : 255) };
                }
            }
            return pixels;
        }

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

        const render::PipelineDesc overlay {
            .shader = render::Shader::Textured,
            .layout = { sizeof(OverlayVertex), kOverlayAttributes },
            .blend = render::BlendMode::Alpha,
            .depth_test = false,
            .depth_write = false,
            .label = "test overlay",
        };
        auto created_overlay = device.create_pipeline(overlay);
        if (!created_overlay)
            return fail(created_overlay.error());
        scene.overlay_pipeline = *created_overlay;

        const auto pixels = make_checker();
        auto checker = device.create_texture({ kCheckerSize, kCheckerSize, render::TextureFormat::Rgba8, pixels, "checker" });
        if (!checker)
            return fail(checker.error());
        scene.checker = *checker;
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

        // A panel in the bottom-left corner whose width breathes, rebuilt from scratch each frame.
        const float width = 160.0f + 40.0f * std::sin(angle);
        const std::array<std::uint8_t, 4> white = { 255, 255, 255, 220 };
        const std::array<OverlayVertex, 4> panel = { {
            { { 16.0f, 16.0f }, white, { 0, 1 } },
            { { 16.0f, 176.0f }, white, { 0, 0 } },
            { { 16.0f + width, 176.0f }, white, { 1, 0 } },
            { { 16.0f + width, 16.0f }, white, { 1, 1 } },
        } };
        const auto vertices = device.allocate_transient(bytes_of(panel));
        const auto panel_indices = device.allocate_transient(bytes_of(kQuadIndices));
        if (!vertices || !panel_indices)
            return;

        const render::Extent extent = device.extent();
        render::DrawCommand overlay;
        overlay.pipeline = scene.overlay_pipeline;
        overlay.vertices = *vertices;
        overlay.indices = *panel_indices;
        overlay.count = static_cast<std::uint32_t>(kQuadIndices.size());
        overlay.texture = scene.checker;
        overlay.transform = glm::ortho(0.0f, static_cast<float>(extent.width), 0.0f, static_cast<float>(extent.height), -1.0f, 1.0f);
        device.draw(overlay);
    }

    void destroy_test_scene(render::Device& device, TestScene& scene)
    {
        device.destroy_texture(scene.checker);
        device.destroy_pipeline(scene.overlay_pipeline);
        device.destroy_buffer(scene.quad_indices);
        device.destroy_buffer(scene.quad_vertices);
        device.destroy_buffer(scene.triangle);
        device.destroy_pipeline(scene.pipeline);
        scene = { };
    }

} // namespace rct3::scene
