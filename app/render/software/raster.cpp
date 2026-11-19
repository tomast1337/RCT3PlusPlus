// SPDX-License-Identifier: GPL-3.0-or-later
#include "render/software/raster.hpp"

#include "core/types.hpp"

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

        constexpr i32 kTileSize = 64; ///< Pixels along a tile's side
        constexpr i32 kBlock = 8;     ///< Pixels a side of the blocks a triangle's box is tested in
        /// Clip space is cut this many times the screen's half width out from its middle, so far corners keep
        /// enough of a float's precision.
        constexpr f32 kGuardBand = 4.0f;
        /// The most corners a triangle has once cut by the six planes.
        constexpr usize kMaxCorners = 9;
        constexpr usize kMaxStride = 4 + kMaxVaryings;

        glm::vec4 read_attribute(const std::byte* at, VertexFormat format)
        {
            std::array<f32, 4> f { 0.0f, 0.0f, 0.0f, 1.0f };
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
                    for (usize i = 0; i < 4; ++i)
                        f[i] = static_cast<f32>(std::to_integer<u8>(at[i])) / 255.0f;
                    break;
            }
            return { f[0], f[1], f[2], f[3] };
        }

        usize attribute_bytes(VertexFormat format)
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

        using Corner = std::array<f32, kMaxStride>;

        // How far `c` is inside plane `p` (0 to 5: near, far, left, right, top, bottom); negative outside.
        f32 inside(const f32* c, i32 p)
        {
            const f32 x = c[0];
            const f32 y = c[1];
            const f32 z = c[2];
            const f32 w = c[3];
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
        usize clip(std::array<Corner, kMaxCorners>& corners, usize count, usize stride)
        {
            std::array<Corner, kMaxCorners> out;
            for (i32 p = 0; p < 6 && count >= 3; ++p)
            {
                usize kept = 0;
                for (usize i = 0; i < count; ++i)
                {
                    const Corner& a = corners[i];
                    const Corner& b = corners[(i + 1) % count];
                    const f32 da = inside(a.data(), p);
                    const f32 db = inside(b.data(), p);
                    if (da >= 0.0f && kept < kMaxCorners)
                        out[kept++] = a;
                    if ((da >= 0.0f) != (db >= 0.0f) && kept < kMaxCorners)
                    {
                        const f32 t = da / (da - db);
                        Corner& c = out[kept++];
                        for (usize k = 0; k < stride; ++k)
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
            f32 x = 0;
            f32 y = 0;
            f32 z = 0;
            f32 iw = 0; ///< 1 over clip w
            const f32* varyings = nullptr;
        };

        // Sets up the triangle a, b, c (as on screen) into `batch`. False when it covers no pixel or is culled.
        bool set_up(Batch& batch, u32 draw_index, const DrawState& draw, ScreenCorner a, ScreenCorner b, ScreenCorner c, CullMode cull)
        {
            f32 area = (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y);
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
            const i32 x0 = std::max(static_cast<i32>(s.x), static_cast<i32>(std::ceil(std::min({ a.x, b.x, c.x }) - 0.5f)));
            const i32 y0 = std::max(static_cast<i32>(s.y), static_cast<i32>(std::ceil(std::min({ a.y, b.y, c.y }) - 0.5f)));
            const i32 x1 = std::min(static_cast<i32>(s.x + s.size.width), static_cast<i32>(std::floor(std::max({ a.x, b.x, c.x }) - 0.5f)) + 1);
            const i32 y1 = std::min(static_cast<i32>(s.y + s.size.height), static_cast<i32>(std::floor(std::max({ a.y, b.y, c.y }) - 0.5f)) + 1);
            if (x0 >= x1 || y0 >= y1)
                return false;

            Batch::Triangle t { .draw = draw_index, .planes = static_cast<u32>(batch.planes.size()), .x0 = x0, .y0 = y0, .x1 = x1, .y1 = y1, .origin = { a.x, a.y } };
            const std::array<const ScreenCorner*, 3> corner { &a, &b, &c };
            for (usize e = 0; e < 3; ++e)
            {
                // The edge opposite corner e, from p to q: positive on the inside.
                const ScreenCorner& p = *corner[(e + 1) % 3];
                const ScreenCorner& q = *corner[(e + 2) % 3];
                const f32 dx = q.x - p.x;
                const f32 dy = q.y - p.y;
                t.edges[e] = { -dy, dx, dy * (p.x - a.x) - dx * (p.y - a.y) };
                // With y down, a top edge runs right and a left edge runs up.
                t.top_left[e] = dy < 0.0f || (dy == 0.0f && dx > 0.0f);
            }
            const f32 ex1 = b.x - a.x;
            const f32 ey1 = b.y - a.y;
            const f32 ex2 = c.x - a.x;
            const f32 ey2 = c.y - a.y;
            const auto plane = [&](f32 fa, f32 fb, f32 fc) { return glm::vec3(fa, ((fb - fa) * ey2 - (fc - fa) * ey1) / area, ((fc - fa) * ex1 - (fb - fa) * ex2) / area); };
            t.depth = plane(a.z, b.z, c.z);
            const u32 n = draw.program.varyings;
            for (u32 k = 0; k < n; ++k)
            {
                const glm::vec3 p = plane(a.varyings[k] * a.iw, b.varyings[k] * b.iw, c.varyings[k] * c.iw);
                batch.planes.insert(batch.planes.end(), { p.x, p.y, p.z });
            }
            const glm::vec3 w = plane(a.iw, b.iw, c.iw);
            batch.planes.insert(batch.planes.end(), { w.x, w.y, w.z });
            batch.triangles.push_back(t);
            return true;
        }

        glm::vec4 blend(BlendMode mode, f32 fade, glm::vec4 src, glm::vec4 dst)
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

        void draw_tile(const Batch& batch, Texture& target, i32 tile)
        {
            const i32 across = (static_cast<i32>(target.extent.width) + kTileSize - 1) / kTileSize;
            const i32 tx0 = (tile % across) * kTileSize;
            const i32 ty0 = (tile / across) * kTileSize;
            const i32 tx1 = std::min(tx0 + kTileSize, static_cast<i32>(target.extent.width));
            const i32 ty1 = std::min(ty0 + kTileSize, static_cast<i32>(target.extent.height));
            const bool has_depth = !target.depth.empty();
            std::array<f32, kMaxVaryings> varyings;
            for (const u32 index : batch.tiles[static_cast<usize>(tile)])
            {
                const Batch::Triangle& t = batch.triangles[index];
                const DrawState& d = batch.draws[t.draw];
                const usize n = d.program.varyings;
                const f32* planes = batch.planes.data() + t.planes;
                const f32* w = planes + n * 3;
                const i32 x0 = std::max(t.x0, tx0);
                const i32 x1 = std::min(t.x1, tx1);
                const i32 y0 = std::max(t.y0, ty0);
                const i32 y1 = std::min(t.y1, ty1);
                // Blocks of pixels wholly outside an edge are skipped: an edge's value is lowest at a corner.
                const auto edge_at = [&](usize e, i32 x, i32 y) { return t.edges[e].x * (static_cast<f32>(x) + 0.5f - t.origin.x) + t.edges[e].y * (static_cast<f32>(y) + 0.5f - t.origin.y) + t.edges[e].z; };
                for (i32 by = y0; by < y1; by += kBlock)
                    for (i32 bx = x0; bx < x1; bx += kBlock)
                    {
                        const i32 bx1 = std::min(bx + kBlock, x1);
                        const i32 by1 = std::min(by + kBlock, y1);
                        bool outside = false;
                        for (usize e = 0; e < 3 && !outside; ++e)
                            outside = std::max({ edge_at(e, bx, by), edge_at(e, bx1 - 1, by), edge_at(e, bx, by1 - 1), edge_at(e, bx1 - 1, by1 - 1) }) < 0.0f;
                        if (outside)
                            continue;
                        for (i32 y = by; y < by1; ++y)
                        {
                            const f32 py = static_cast<f32>(y) + 0.5f - t.origin.y;
                            for (i32 x = bx; x < bx1; ++x)
                            {
                                const f32 px = static_cast<f32>(x) + 0.5f - t.origin.x;
                                bool in = true;
                                for (usize e = 0; e < 3 && in; ++e)
                                {
                                    const f32 v = t.edges[e].x * px + t.edges[e].y * py + t.edges[e].z;
                                    in = v > 0.0f || (v == 0.0f && t.top_left[e]);
                                }
                                if (!in)
                                    continue;
                                const usize i = static_cast<usize>(y) * target.extent.width + static_cast<usize>(x);
                                const f32 z = t.depth.x + t.depth.y * px + t.depth.z * py;
                                if (d.depth_test && has_depth && !(z <= target.depth[i]))
                                    continue;
                                const f32 iw = w[0] + w[1] * px + w[2] * py;
                                const f32 inv = 1.0f / iw;
                                for (usize k = 0; k < n; ++k)
                                    varyings[k] = (planes[k * 3] + planes[k * 3 + 1] * px + planes[k * 3 + 2] * py) * inv;
                                const Fragment fragment { .position = { static_cast<f32>(x) + 0.5f, static_cast<f32>(y) + 0.5f, z, iw }, .varyings = std::span<const f32>(varyings.data(), n), .planes = planes, .origin = t.origin };
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

    WorkerPool::WorkerPool(u32 threads)
    {
        for (u32 i = 0; i < threads; ++i)
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

    void WorkerPool::run(u32 count, std::function_ref<void(u32)> job)
    {
        {
            const std::scoped_lock lock(mutex_);
            job_.emplace(job);
            count_ = count;
            next_ = 0;
            busy_ = static_cast<u32>(threads_.size());
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
        u64 seen = 0;
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
        for (u32 i = next_++; i < count_; i = next_++)
            (*job_)(i);
    }

    u32 add_triangles(Batch& batch, const DrawState& draw, const VertexSource& source, CullMode cull)
    {
        const usize n = draw.program.varyings;
        const usize stride = 4 + n;
        const bool indexed = !source.indices.empty();
        const usize index_size = source.index_type == IndexType::U16 ? 2 : 4;
        if (indexed && (usize { source.first } + source.count) * index_size > source.indices.size())
        {
            assert(false && "draw reads past its index buffer");
            return 0;
        }
        const auto vertex_at = [&](u32 i) -> i64 {
            if (!indexed)
                return i64 { source.first } + i;
            const std::byte* at = source.indices.data() + (usize { source.first } + i) * index_size;
            if (index_size == 2)
            {
                u16 v = 0;
                std::memcpy(&v, at, 2);
                return i64 { v } + source.vertex_offset;
            }
            u32 v = 0;
            std::memcpy(&v, at, 4);
            return i64 { v } + source.vertex_offset;
        };

        // The draw's vertices are run once each when they lie close together, as most do; else once a corner.
        i64 lowest = std::numeric_limits<i64>::max();
        i64 highest = -1;
        for (u32 i = 0; i < source.count; ++i)
        {
            const i64 v = vertex_at(i);
            lowest = std::min(lowest, v);
            highest = std::max(highest, v);
        }
        usize attributes_end = 0;
        for (const VertexAttribute& a : source.layout.attributes)
            attributes_end = std::max(attributes_end, a.offset + attribute_bytes(a.format));
        if (source.count < 3 || lowest < 0 || static_cast<usize>(highest) * source.layout.stride + attributes_end > source.vertices.size())
        {
            assert((source.count < 3 || lowest >= 0) && "draw reads past its vertex buffer");
            return 0;
        }
        const auto shade = [&](i64 vertex, f32* out) {
            std::array<glm::vec4, kMaxAttributes> attributes;
            attributes.fill(glm::vec4(0.0f, 0.0f, 0.0f, 1.0f));
            const std::byte* at = source.vertices.data() + static_cast<usize>(vertex) * source.layout.stride;
            for (const VertexAttribute& a : source.layout.attributes)
                if (a.location < kMaxAttributes)
                    attributes[a.location] = read_attribute(at + a.offset, a.format);
            glm::vec4 clip;
            draw.program.vertex(draw, attributes, clip, std::span<f32>(out + 4, n));
            std::memcpy(out, &clip, sizeof clip);
        };
        const bool shared = static_cast<u64>(highest - lowest) < u64 { source.count } * 2 + 64;
        if (shared)
        {
            batch.vertices.resize(static_cast<usize>(highest - lowest + 1) * stride);
            for (i64 v = lowest; v <= highest; ++v)
                shade(v, batch.vertices.data() + static_cast<usize>(v - lowest) * stride);
        }

        const auto draw_index = static_cast<u32>(batch.draws.size());
        batch.draws.push_back(draw);
        const ScreenView& view = draw.viewport;
        const auto to_screen = [&](const f32* c) {
            const f32 iw = 1.0f / c[3];
            return ScreenCorner { .x = static_cast<f32>(view.x) + (c[0] * iw * 0.5f + 0.5f) * static_cast<f32>(view.size.width), .y = static_cast<f32>(view.y) + (0.5f - c[1] * iw * 0.5f) * static_cast<f32>(view.size.height), .z = c[2] * iw, .iw = iw, .varyings = c + 4 };
        };
        std::array<Corner, kMaxCorners> corners;
        u32 added = 0;
        for (u32 i = 0; i + 2 < source.count; i += 3)
        {
            std::array<const f32*, 3> v { };
            for (u32 k = 0; k < 3; ++k)
            {
                const i64 vertex = vertex_at(i + k);
                if (shared)
                {
                    v[k] = batch.vertices.data() + static_cast<usize>(vertex - lowest) * stride;
                }
                else
                {
                    shade(vertex, corners[k].data());
                    v[k] = corners[k].data();
                }
            }
            bool all_inside = true;
            for (i32 p = 0; p < 6 && all_inside; ++p)
                for (const f32* c : v)
                    all_inside = all_inside && inside(c, p) >= 0.0f;
            if (all_inside)
            {
                added += set_up(batch, draw_index, draw, to_screen(v[0]), to_screen(v[1]), to_screen(v[2]), cull) ? 1 : 0;
                continue;
            }
            if (shared)
                for (u32 k = 0; k < 3; ++k)
                    std::copy_n(v[k], stride, corners[k].begin());
            const usize count = clip(corners, 3, stride);
            for (usize k = 1; k + 1 < count; ++k)
                added += set_up(batch, draw_index, draw, to_screen(corners[0].data()), to_screen(corners[k].data()), to_screen(corners[k + 1].data()), cull) ? 1 : 0;
        }
        if (added == 0)
            batch.draws.pop_back();
        return added;
    }

    void draw_batch(Batch& batch, Texture& target, WorkerPool& pool)
    {
        const i32 across = (static_cast<i32>(target.extent.width) + kTileSize - 1) / kTileSize;
        const i32 down = (static_cast<i32>(target.extent.height) + kTileSize - 1) / kTileSize;
        const auto tiles = static_cast<usize>(across * down);
        if (batch.tiles.size() < tiles)
            batch.tiles.resize(tiles);
        for (usize i = 0; i < tiles; ++i)
            batch.tiles[i].clear();
        for (u32 i = 0; i < batch.triangles.size(); ++i)
        {
            const Batch::Triangle& t = batch.triangles[i];
            for (i32 ty = t.y0 / kTileSize; ty <= (t.y1 - 1) / kTileSize && ty < down; ++ty)
                for (i32 tx = t.x0 / kTileSize; tx <= (t.x1 - 1) / kTileSize && tx < across; ++tx)
                    batch.tiles[static_cast<usize>(ty * across + tx)].push_back(i);
        }
        pool.run(static_cast<u32>(tiles), [&](u32 tile) { draw_tile(batch, target, static_cast<i32>(tile)); });
        batch.clear();
    }

} // namespace rct3::render::software
