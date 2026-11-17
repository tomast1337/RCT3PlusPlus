// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/raster.hpp"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <limits>

namespace rct3::render::software
{

    namespace
    {

        constexpr std::int32_t kTileSize = 64; ///< Pixels along a tile's side
        constexpr std::int32_t kBlock = 8;     ///< Pixels a side of the blocks a triangle's box is tested in
        /// Clip space is cut this many times the screen's half width out from its middle, so far corners keep
        /// enough of a float's precision.
        constexpr float kGuardBand = 4.0f;
        /// The most corners a triangle has once cut by the six planes.
        constexpr std::size_t kMaxCorners = 9;
        constexpr std::size_t kMaxStride = 4 + kMaxVaryings;

        glm::vec4 read_attribute(const std::byte* at, VertexFormat format)
        {
            std::array<float, 4> f { 0.0f, 0.0f, 0.0f, 1.0f };
            switch (format)
            {
                case VertexFormat::Float2:
                    std::memcpy(f.data(), at, 8);
                    break;
                case VertexFormat::Float3:
                    std::memcpy(f.data(), at, 12);
                    break;
                case VertexFormat::Float4:
                    std::memcpy(f.data(), at, 16);
                    break;
                case VertexFormat::UByte4Norm:
                    for (std::size_t i = 0; i < 4; ++i)
                        f[i] = static_cast<float>(std::to_integer<std::uint8_t>(at[i])) / 255.0f;
                    break;
            }
            return { f[0], f[1], f[2], f[3] };
        }

        std::size_t attribute_bytes(VertexFormat format)
        {
            switch (format)
            {
                case VertexFormat::Float2:
                    return 8;
                case VertexFormat::Float3:
                    return 12;
                case VertexFormat::Float4:
                    return 16;
                case VertexFormat::UByte4Norm:
                    return 4;
            }
            return 16;
        }

        using Corner = std::array<float, kMaxStride>;

        // How far `c` is inside plane `p` (0 to 5: near, far, left, right, top, bottom); negative outside.
        float inside(const float* c, std::int32_t p)
        {
            const float x = c[0];
            const float y = c[1];
            const float z = c[2];
            const float w = c[3];
            switch (p)
            {
                case 0:
                    return z;
                case 1:
                    return w - z;
                case 2:
                    return x + kGuardBand * w;
                case 3:
                    return kGuardBand * w - x;
                case 4:
                    return y + kGuardBand * w;
                default:
                    return kGuardBand * w - y;
            }
        }

        // Cuts the polygon in `corners` by the planes it crosses (Sutherland-Hodgman). Returns the corners left.
        std::size_t clip(std::array<Corner, kMaxCorners>& corners, std::size_t count, std::size_t stride)
        {
            std::array<Corner, kMaxCorners> out;
            for (std::int32_t p = 0; p < 6 && count >= 3; ++p)
            {
                std::size_t kept = 0;
                for (std::size_t i = 0; i < count; ++i)
                {
                    const Corner& a = corners[i];
                    const Corner& b = corners[(i + 1) % count];
                    const float da = inside(a.data(), p);
                    const float db = inside(b.data(), p);
                    if (da >= 0.0f && kept < kMaxCorners)
                        out[kept++] = a;
                    if ((da >= 0.0f) != (db >= 0.0f) && kept < kMaxCorners)
                    {
                        const float t = da / (da - db);
                        Corner& c = out[kept++];
                        for (std::size_t k = 0; k < stride; ++k)
                            c[k] = a[k] + (b[k] - a[k]) * t;
                    }
                }
                std::copy_n(out.begin(), kept, corners.begin());
                count = kept;
            }
            return count;
        }

        struct ScreenCorner
        {
            float x = 0;
            float y = 0;
            float z = 0;
            float iw = 0; ///< 1 over clip w
            const float* varyings = nullptr;
        };

        // Sets up the triangle a, b, c (as on screen) into `batch`. False when it covers no pixel or is culled.
        bool set_up(Batch& batch, std::uint32_t draw_index, const DrawState& draw, ScreenCorner a, ScreenCorner b, ScreenCorner c, CullMode cull)
        {
            float area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
            if (!(area != 0.0f) || !std::isfinite(area))
                return false;
            // Front faces wind clockwise as seen on screen, which with y down has a positive area.
            const bool front = area > 0.0f;
            if ((cull == CullMode::Back && !front) || (cull == CullMode::Front && front))
                return false;
            if (!front)
            {
                std::swap(b, c);
                area = -area;
            }
            const ScreenView& s = draw.scissor;
            const std::int32_t x0 = std::max(static_cast<std::int32_t>(s.x), static_cast<std::int32_t>(std::ceil(std::min({ a.x, b.x, c.x }) - 0.5f)));
            const std::int32_t y0 = std::max(static_cast<std::int32_t>(s.y), static_cast<std::int32_t>(std::ceil(std::min({ a.y, b.y, c.y }) - 0.5f)));
            const std::int32_t x1 = std::min(static_cast<std::int32_t>(s.x + s.size.width), static_cast<std::int32_t>(std::floor(std::max({ a.x, b.x, c.x }) - 0.5f)) + 1);
            const std::int32_t y1 = std::min(static_cast<std::int32_t>(s.y + s.size.height), static_cast<std::int32_t>(std::floor(std::max({ a.y, b.y, c.y }) - 0.5f)) + 1);
            if (x0 >= x1 || y0 >= y1)
                return false;

            Batch::Triangle t { .draw = draw_index, .planes = static_cast<std::uint32_t>(batch.planes.size()), .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .origin = { a.x, a.y } };
            const std::array<const ScreenCorner*, 3> corner { &a, &b, &c };
            for (std::size_t e = 0; e < 3; ++e)
            {
                // The edge opposite corner e, from p to q: positive on the inside.
                const ScreenCorner& p = *corner[(e + 1) % 3];
                const ScreenCorner& q = *corner[(e + 2) % 3];
                const float dx = q.x - p.x;
                const float dy = q.y - p.y;
                t.edges[e] = { -dy, dx, dy * (p.x - a.x) - dx * (p.y - a.y) };
                // With y down, a top edge runs right and a left edge runs up.
                t.top_left[e] = dy < 0.0f || (dy == 0.0f && dx > 0.0f);
            }
            const float ex1 = b.x - a.x;
            const float ey1 = b.y - a.y;
            const float ex2 = c.x - a.x;
            const float ey2 = c.y - a.y;
            const auto plane = [&](float fa, float fb, float fc) { return glm::vec3(fa, ((fb - fa) * ey2 - (fc - fa) * ey1) / area, ((fc - fa) * ex1 - (fb - fa) * ex2) / area); };
            t.depth = plane(a.z, b.z, c.z);
            const std::uint32_t n = draw.program.varyings;
            for (std::uint32_t k = 0; k < n; ++k)
            {
                const glm::vec3 p = plane(a.varyings[k] * a.iw, b.varyings[k] * b.iw, c.varyings[k] * c.iw);
                batch.planes.insert(batch.planes.end(), { p.x, p.y, p.z });
            }
            const glm::vec3 w = plane(a.iw, b.iw, c.iw);
            batch.planes.insert(batch.planes.end(), { w.x, w.y, w.z });
            batch.triangles.push_back(t);
            return true;
        }

        glm::vec4 blend(BlendMode mode, float fade, glm::vec4 src, glm::vec4 dst)
        {
            switch (mode)
            {
                case BlendMode::Opaque:
                    return src;
                case BlendMode::Alpha:
                    return { glm::vec3(src) * src.a + glm::vec3(dst) * (1.0f - src.a), src.a + dst.a * (1.0f - src.a) };
                case BlendMode::Add:
                    return { glm::vec3(src) + glm::vec3(dst), dst.a };
                case BlendMode::Faded:
                    return { glm::vec3(src) * fade + glm::vec3(dst) * (1.0f - fade), dst.a };
            }
            return src;
        }

        void draw_tile(const Batch& batch, Texture& target, std::int32_t tile)
        {
            const std::int32_t across = (static_cast<std::int32_t>(target.extent.width) + kTileSize - 1) / kTileSize;
            const std::int32_t tx0 = (tile % across) * kTileSize;
            const std::int32_t ty0 = (tile / across) * kTileSize;
            const std::int32_t tx1 = std::min(tx0 + kTileSize, static_cast<std::int32_t>(target.extent.width));
            const std::int32_t ty1 = std::min(ty0 + kTileSize, static_cast<std::int32_t>(target.extent.height));
            const bool has_depth = !target.depth.empty();
            std::array<float, kMaxVaryings> varyings;
            for (const std::uint32_t index : batch.tiles[static_cast<std::size_t>(tile)])
            {
                const Batch::Triangle& t = batch.triangles[index];
                const DrawState& d = batch.draws[t.draw];
                const std::size_t n = d.program.varyings;
                const float* planes = batch.planes.data() + t.planes;
                const float* w = planes + n * 3;
                const std::int32_t x0 = std::max(t.x0, tx0);
                const std::int32_t x1 = std::min(t.x1, tx1);
                const std::int32_t y0 = std::max(t.y0, ty0);
                const std::int32_t y1 = std::min(t.y1, ty1);
                // Blocks of pixels wholly outside an edge are skipped: an edge's value is lowest at a corner.
                const auto edge_at = [&](std::size_t e, std::int32_t x, std::int32_t y) { return t.edges[e].x * (static_cast<float>(x) + 0.5f - t.origin.x) + t.edges[e].y * (static_cast<float>(y) + 0.5f - t.origin.y) + t.edges[e].z; };
                for (std::int32_t by = y0; by < y1; by += kBlock)
                    for (std::int32_t bx = x0; bx < x1; bx += kBlock)
                    {
                        const std::int32_t bx1 = std::min(bx + kBlock, x1);
                        const std::int32_t by1 = std::min(by + kBlock, y1);
                        bool outside = false;
                        for (std::size_t e = 0; e < 3 && !outside; ++e)
                            outside = std::max({ edge_at(e, bx, by), edge_at(e, bx1 - 1, by), edge_at(e, bx, by1 - 1), edge_at(e, bx1 - 1, by1 - 1) }) < 0.0f;
                        if (outside)
                            continue;
                        for (std::int32_t y = by; y < by1; ++y)
                        {
                            const float py = static_cast<float>(y) + 0.5f - t.origin.y;
                            for (std::int32_t x = bx; x < bx1; ++x)
                            {
                                const float px = static_cast<float>(x) + 0.5f - t.origin.x;
                                bool in = true;
                                for (std::size_t e = 0; e < 3 && in; ++e)
                                {
                                    const float v = t.edges[e].x * px + t.edges[e].y * py + t.edges[e].z;
                                    in = v > 0.0f || (v == 0.0f && t.top_left[e]);
                                }
                                if (!in)
                                    continue;
                                const std::size_t i = static_cast<std::size_t>(y) * target.extent.width + static_cast<std::size_t>(x);
                                const float z = t.depth.x + t.depth.y * px + t.depth.z * py;
                                if (d.depth_test && has_depth && !(z <= target.depth[i]))
                                    continue;
                                const float iw = w[0] + w[1] * px + w[2] * py;
                                const float inv = 1.0f / iw;
                                for (std::size_t k = 0; k < n; ++k)
                                    varyings[k] = (planes[k * 3] + planes[k * 3 + 1] * px + planes[k * 3 + 2] * py) * inv;
                                const Fragment fragment { .position = { static_cast<float>(x) + 0.5f, static_cast<float>(y) + 0.5f, z, iw }, .varyings = std::span<const float>(varyings.data(), n), .planes = planes, .origin = t.origin };
                                glm::vec4 colour;
                                if (!d.program.fragment(d, fragment, colour))
                                    continue;
                                // As on the GPU, depth is only written where it is tested.
                                if (d.depth_test && d.depth_write && has_depth)
                                    target.depth[i] = z;
                                if (d.colour_write)
                                    target.texels[i] = pack(blend(d.blend, d.fade, colour, d.blend == BlendMode::Opaque ? glm::vec4(0.0f) : unpack(target.texels[i])));
                            }
                        }
                    }
            }
        }

    } // namespace

    WorkerPool::WorkerPool(std::uint32_t threads)
    {
        for (std::uint32_t i = 0; i < threads; ++i)
            threads_.emplace_back([this] { work(); });
    }

    WorkerPool::~WorkerPool()
    {
        {
            const std::scoped_lock lock(mutex_);
            stopping_ = true;
        }
        wake_.notify_all();
    }

    void WorkerPool::run(std::uint32_t count, std::function_ref<void(std::uint32_t)> job)
    {
        {
            const std::scoped_lock lock(mutex_);
            job_.emplace(job);
            count_ = count;
            next_ = 0;
            busy_ = static_cast<std::uint32_t>(threads_.size());
            ++round_;
        }
        wake_.notify_all();
        take_items();
        std::unique_lock lock(mutex_);
        done_.wait(lock, [this] { return busy_ == 0; });
        job_.reset();
    }

    void WorkerPool::work()
    {
        std::uint64_t seen = 0;
        std::unique_lock lock(mutex_);
        for (;;)
        {
            wake_.wait(lock, [&] { return stopping_ || round_ != seen; });
            if (stopping_)
                return;
            seen = round_;
            lock.unlock();
            take_items();
            lock.lock();
            if (--busy_ == 0)
                done_.notify_one();
        }
    }

    void WorkerPool::take_items()
    {
        for (std::uint32_t i = next_++; i < count_; i = next_++)
            (*job_)(i);
    }

    std::uint32_t add_triangles(Batch& batch, const DrawState& draw, const VertexSource& source, CullMode cull)
    {
        const std::size_t n = draw.program.varyings;
        const std::size_t stride = 4 + n;
        const bool indexed = !source.indices.empty();
        const std::size_t index_size = source.index_type == IndexType::U16 ? 2 : 4;
        if (indexed && (std::size_t { source.first } + source.count) * index_size > source.indices.size())
        {
            assert(false && "draw reads past its index buffer");
            return 0;
        }
        const auto vertex_at = [&](std::uint32_t i) -> std::int64_t {
            if (!indexed)
                return std::int64_t { source.first } + i;
            const std::byte* at = source.indices.data() + (std::size_t { source.first } + i) * index_size;
            if (index_size == 2)
            {
                std::uint16_t v = 0;
                std::memcpy(&v, at, 2);
                return std::int64_t { v } + source.vertex_offset;
            }
            std::uint32_t v = 0;
            std::memcpy(&v, at, 4);
            return std::int64_t { v } + source.vertex_offset;
        };

        // The draw's vertices are run once each when they lie close together, as most do; else once a corner.
        std::int64_t lowest = std::numeric_limits<std::int64_t>::max();
        std::int64_t highest = -1;
        for (std::uint32_t i = 0; i < source.count; ++i)
        {
            const std::int64_t v = vertex_at(i);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        std::size_t attributes_end = 0;
        for (const VertexAttribute& a : source.layout.attributes)
            attributes_end = std::max(attributes_end, a.offset + attribute_bytes(a.format));
        if (source.count < 3 || lowest < 0 || static_cast<std::size_t>(highest) * source.layout.stride + attributes_end > source.vertices.size())
        {
            assert((source.count < 3 || lowest >= 0) && "draw reads past its vertex buffer");
            return 0;
        }
        const auto shade = [&](std::int64_t vertex, float* out) {
            std::array<glm::vec4, kMaxAttributes> attributes;
            attributes.fill(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const std::byte* at = source.vertices.data() + static_cast<std::size_t>(vertex) * source.layout.stride;
            for (const VertexAttribute& a : source.layout.attributes)
                if (a.location < kMaxAttributes)
                    attributes[a.location] = read_attribute(at + a.offset, a.format);
            glm::vec4 clip;
            draw.program.vertex(draw, attributes, clip, std::span<float>(out + 4, n));
            std::memcpy(out, &clip, sizeof clip);
        };
        const bool shared = static_cast<std::uint64_t>(highest - lowest) < std::uint64_t { source.count } * 2 + 64;
        if (shared)
        {
            batch.vertices.resize(static_cast<std::size_t>(highest - lowest + 1) * stride);
            for (std::int64_t v = lowest; v <= highest; ++v)
                shade(v, batch.vertices.data() + static_cast<std::size_t>(v - lowest) * stride);
        }

        const auto draw_index = static_cast<std::uint32_t>(batch.draws.size());
        batch.draws.push_back(draw);
        const ScreenView& view = draw.viewport;
        const auto to_screen = [&](const float* c) {
            const float iw = 1.0f / c[3];
            return ScreenCorner { .x = static_cast<float>(view.x) + (c[0] * iw * 0.5f + 0.5f) * static_cast<float>(view.size.width), .y = static_cast<float>(view.y) + (0.5f - c[1] * iw * 0.5f) * static_cast<float>(view.size.height), .z = c[2] * iw, .iw = iw, .varyings = c + 4 };
        };
        std::array<Corner, kMaxCorners> corners;
        std::uint32_t added = 0;
        for (std::uint32_t i = 0; i + 2 < source.count; i += 3)
        {
            std::array<const float*, 3> v { };
            for (std::uint32_t k = 0; k < 3; ++k)
            {
                const std::int64_t vertex = vertex_at(i + k);
                if (shared)
                {
                    v[k] = batch.vertices.data() + static_cast<std::size_t>(vertex - lowest) * stride;
                }
                else
                {
                    shade(vertex, corners[k].data());
                    v[k] = corners[k].data();
                }
            }
            bool all_inside = true;
            for (std::int32_t p = 0; p < 6 && all_inside; ++p)
                for (const float* c : v)
                    all_inside = all_inside && inside(c, p) >= 0.0f;
            if (all_inside)
            {
                added += set_up(batch, draw_index, draw, to_screen(v[0]), to_screen(v[1]), to_screen(v[2]), cull) ? 1 : 0;
                continue;
            }
            if (shared)
                for (std::uint32_t k = 0; k < 3; ++k)
                    std::copy_n(v[k], stride, corners[k].begin());
            const std::size_t count = clip(corners, 3, stride);
            for (std::size_t k = 1; k + 1 < count; ++k)
                added += set_up(batch, draw_index, draw, to_screen(corners[0].data()), to_screen(corners[k].data()), to_screen(corners[k + 1].data()), cull) ? 1 : 0;
        }
        if (added == 0)
            batch.draws.pop_back();
        return added;
    }

    void draw_batch(Batch& batch, Texture& target, WorkerPool& pool)
    {
        const std::int32_t across = (static_cast<std::int32_t>(target.extent.width) + kTileSize - 1) / kTileSize;
        const std::int32_t down = (static_cast<std::int32_t>(target.extent.height) + kTileSize - 1) / kTileSize;
        const auto tiles = static_cast<std::size_t>(across * down);
        if (batch.tiles.size() < tiles)
            batch.tiles.resize(tiles);
        for (std::size_t i = 0; i < tiles; ++i)
            batch.tiles[i].clear();
        for (std::uint32_t i = 0; i < batch.triangles.size(); ++i)
        {
            const Batch::Triangle& t = batch.triangles[i];
            for (std::int32_t ty = t.y0 / kTileSize; ty <= (t.y1 - 1) / kTileSize && ty < down; ++ty)
                for (std::int32_t tx = t.x0 / kTileSize; tx <= (t.x1 - 1) / kTileSize && tx < across; ++tx)
                    batch.tiles[static_cast<std::size_t>(ty * across + tx)].push_back(i);
        }
        pool.run(static_cast<std::uint32_t>(tiles), [&](std::uint32_t tile) { draw_tile(batch, target, static_cast<std::int32_t>(tile)); });
        batch.clear();
    }

} // namespace rct3::render::software
