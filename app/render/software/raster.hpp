// SPDX-License-Identifier: GPL-3.0-or-later
//
// The software backend's rasteriser. Draws are run through their vertex stage as they come and their triangles
// clipped (near and far planes, and a wide band round the screen), culled and set up into a batch. A batch is
// drawn when its pass ends, or sooner once it is big, on a few threads at once: the target is cut into tiles,
// each tile takes the triangles that touch it, in the order they were drawn, so the result is as one thread
// would draw it. Depth is tested before the fragment stage and written after it, which a discard skips.
#pragma once

#include "render/device.hpp"
#include "render/shader_inputs.hpp"
#include "render/software/texture.hpp"

#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <mutex>
#include <optional>
#include <span>
#include <thread>
#include <vector>

namespace rct3::render::software
{

    /// The most numbers a vertex stage hands its fragment stage (the Shape program's).
    inline constexpr std::size_t kMaxVaryings = 34;

    struct DrawState;

    /// One pixel a triangle covers, as the fragment stage sees it.
    struct Fragment
    {
        /// The pixel's middle (x, y, from the target's top-left corner), its depth, and 1 over the clip w.
        glm::vec4 position { 0.0f };
        std::span<const float> varyings;
        const float* planes = nullptr; ///< The triangle's: three a varying, for neighbours' values
        glm::vec2 origin { 0.0f };     ///< Where the planes are measured from

        /// Varying `i` one pixel over (`dx`, `dy`), as the GPU's derivatives take it.
        [[nodiscard]] float neighbour(std::size_t i, float dx, float dy) const
        {
            const glm::vec2 at = glm::vec2(position) + glm::vec2(dx, dy) - origin;
            // The 1 / w plane comes last.
            const float* w = planes + varyings.size() * 3;
            const float* p = planes + i * 3;
            return (p[0] + p[1] * at.x + p[2] * at.y) / (w[0] + w[1] * at.x + w[2] * at.y);
        }

        /// How varyings `i` to `i + N` change to the next pixel across (`dx` 1) or down (`dy` 1).
        template<std::size_t N> [[nodiscard]] glm::vec<N, float> change(std::size_t i, float dx, float dy) const
        {
            glm::vec<N, float> d { };
            for (std::size_t k = 0; k < N; ++k)
                d[static_cast<glm::length_t>(k)] = neighbour(i + k, dx, dy) - varyings[i + k];
            return d;
        }
    };

    /// A program's stages. The vertex stage reads attributes by location (missing ones read (0, 0, 0, 1)) and
    /// fills the clip position and `varyings` numbers; the fragment stage returns false to discard the pixel.
    using VertexStage = void (*)(const DrawState& draw, std::span<const glm::vec4> attributes, glm::vec4& clip, std::span<float> varyings);
    using FragmentStage = bool (*)(const DrawState& draw, const Fragment& fragment, glm::vec4& colour);

    struct Program
    {
        VertexStage vertex = nullptr; ///< None for programs this backend does not draw
        FragmentStage fragment = nullptr;
        std::uint32_t varyings = 0;
    };

    inline constexpr std::size_t kMaxAttributes = 10;

    /// Everything a draw's triangles need once its call has returned.
    struct DrawState
    {
        Program program;
        BlendMode blend = BlendMode::Opaque;
        bool depth_test = true;
        bool depth_write = true;
        bool colour_write = true;
        /// False when only which pixels are drawn matters, not their colour (Texture::colour_read).
        bool shade = true;
        float fade = 1;
        ShaderConstants constants;
        /// The draw's texture, then the extras in the order the program reads them (render/shader_inputs.hpp).
        std::optional<const Texture&> texture;
        std::array<std::optional<const Texture&>, kExtraTextures> extras { };
        Filter filter = Filter::Nearest;
        AddressMode address = AddressMode::Clamp;
        glm::vec4 eye { 0.0f }; ///< Where the transform's eye is, w 1; w 0 without perspective
        ScreenView viewport;    ///< Where clip space lands on the target
        ScreenView scissor;     ///< What may be drawn
        Extent target;          ///< The target's size
    };

    /// The triangles of draws not drawn yet, all into one target. Reused from batch to batch.
    struct Batch
    {
        struct Triangle
        {
            std::uint32_t draw = 0;
            std::uint32_t planes = 0; ///< Where its planes start in `planes`
            std::int32_t x0 = 0;      ///< The pixels it may cover, x0 <= x < x1
            std::int32_t y0 = 0;
            std::int32_t x1 = 0;
            std::int32_t y1 = 0;
            glm::vec2 origin { 0.0f };          ///< Its first corner; edges and planes are measured from it
            std::array<glm::vec3, 3> edges { }; ///< a, b, c of each: a x + b y + c, positive inside
            std::array<bool, 3> top_left { };   ///< Pixels exactly on the edge are its
            glm::vec3 depth { 0.0f };           ///< Value at the origin and change per pixel across and down
        };

        std::vector<DrawState> draws;
        std::vector<Triangle> triangles;
        /// Per triangle, per varying then 1 / w: the value over w at the origin and its changes across and down.
        std::vector<float> planes;
        std::vector<float> vertices; ///< Scratch: one draw's vertices after their stage
        std::vector<std::vector<std::uint32_t>> tiles;

        [[nodiscard]] bool empty() const
        {
            return triangles.empty();
        }
        void clear()
        {
            draws.clear();
            triangles.clear();
            planes.clear();
        }
    };

    /// A few threads that run one job over many items at once. The caller's thread takes items too.
    class WorkerPool
    {
    public:
        explicit WorkerPool(std::uint32_t threads);
        ~WorkerPool();
        WorkerPool(const WorkerPool&) = delete;
        WorkerPool& operator=(const WorkerPool&) = delete;

        /// Runs `job` once for each of `0..count`, in no set order, and returns when all are done.
        void run(std::uint32_t count, std::function_ref<void(std::uint32_t)> job);

    private:
        void work();
        void take_items();

        std::vector<std::jthread> threads_;
        std::mutex mutex_;
        std::condition_variable wake_;
        std::condition_variable done_;
        std::uint64_t round_ = 0;
        std::uint32_t busy_ = 0;
        bool stopping_ = false;
        std::optional<std::function_ref<void(std::uint32_t)>> job_;
        std::uint32_t count_ = 0;
        std::atomic<std::uint32_t> next_ { 0 };
    };

    /// Where one draw's vertices come from.
    struct VertexSource
    {
        std::span<const std::byte> vertices; ///< From the draw's offset on
        std::span<const std::byte> indices;  ///< From the draw's offset on; empty for a draw without
        IndexType index_type = IndexType::U16;
        std::uint32_t count = 0;
        std::uint32_t first = 0;
        std::int32_t vertex_offset = 0;
        VertexLayout layout;
    };

    /// Runs `draw`'s vertices through its vertex stage and adds its triangles to `batch`. Returns how many it
    /// added, after clipping and culling.
    std::uint32_t add_triangles(Batch& batch, const DrawState& draw, const VertexSource& source, CullMode cull);

    /// Draws `batch` into `target` and empties it.
    void draw_batch(Batch& batch, Texture& target, WorkerPool& pool);

} // namespace rct3::render::software
