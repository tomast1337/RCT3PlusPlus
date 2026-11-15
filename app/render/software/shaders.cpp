// SPDX-License-Identifier: GPL-3.0-or-later
//
// Each program follows its Slang source in app/shaders, named after it; constants keep their Slang names and
// values. Varyings are numbers in a row, laid out as each program's comment says.
#include "render/software/shaders.hpp"

#include <algorithm>
#include <cmath>

namespace rct3::render::software
{

    namespace
    {

        using glm::vec2;
        using glm::vec3;
        using glm::vec4;

        vec4 row(const glm::mat4& m, int r)
        {
            return { m[0][r], m[1][r], m[2][r], m[3][r] };
        }

        vec3 at3(std::span<const float> v, std::size_t i)
        {
            return { v[i], v[i + 1], v[i + 2] };
        }

        vec4 at4(std::span<const float> v, std::size_t i)
        {
            return { v[i], v[i + 1], v[i + 2], v[i + 3] };
        }

        void put(std::span<float> v, std::size_t i, vec4 x)
        {
            v[i] = x.x;
            v[i + 1] = x.y;
            v[i + 2] = x.z;
            v[i + 3] = x.w;
        }

        void put(std::span<float> v, std::size_t i, vec3 x)
        {
            v[i] = x.x;
            v[i + 1] = x.y;
            v[i + 2] = x.z;
        }

        void put(std::span<float> v, std::size_t i, vec2 x)
        {
            v[i] = x.x;
            v[i + 1] = x.y;
        }

        float saturate(float x)
        {
            return std::clamp(x, 0.0f, 1.0f);
        }

        float smooth(float a, float b, float x)
        {
            return glm::smoothstep(a, b, x);
        }

        float frac(float x)
        {
            return x - std::floor(x);
        }

        // The draw's own texture and its extras: what binding 0 and the later sets read.
        const Texture& texture(const DrawState& d)
        {
            return *d.texture;
        }

        const Texture& extra(const DrawState& d, std::size_t i)
        {
            return d.extras[i] ? *d.extras[i] : *d.texture;
        }

        vec4 read(const DrawState& d, const Texture& t, vec2 uv)
        {
            return sample(t, uv, d.filter, d.address);
        }

        // The point on the target, 0..1 with y down, that clip position `clip` lands on.
        vec2 screen_uv(vec4 clip)
        {
            const vec2 ndc = vec2(clip) / clip.w;
            return { 0.5f + 0.5f * ndc.x, 0.5f - 0.5f * ndc.y };
        }

        // VertexColor (vertex_color.slang). Varyings: colour.
        void vertex_color_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            clip = d.constants.transform * vec4(vec3(a[0]), 1.0f);
            put(v, 0, a[1]);
        }

        bool vertex_color_fragment(const DrawState&, const Fragment& f, vec4& out)
        {
            out = at4(f.varyings, 0);
            return true;
        }

        // Textured (textured.slang), whose vertex stage TexturedCutout shares. Varyings: colour,
        // uv.
        void textured_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            clip = d.constants.transform * vec4(vec3(a[0]), 1.0f);
            put(v, 0, a[1]);
            put(v, 4, vec2(a[2]));
        }

        bool textured_fragment(const DrawState& d, const Fragment& f, vec4& out)
        {
            out = read(d, texture(d), vec2(f.varyings[4], f.varyings[5])) * at4(f.varyings, 0);
            return true;
        }

        bool textured_cutout_fragment(const DrawState& d, const Fragment& f, vec4& out)
        {
            const vec4 colour = read(d, texture(d), vec2(f.varyings[4], f.varyings[5])) * at4(f.varyings, 0);
            if (colour.a < 0.5f)
                return false;
            out = vec4(vec3(colour), 1.0f);
            return true;
        }

        // Lit (lit.slang) and LitCrossing (lit_crossing.slang). Varyings: colour, uv, normal, world, then for
        // LitCrossing how far through its band and its side. The eye is the draw's (DrawState::eye).
        constexpr std::size_t kLitVaryings = 12;
        constexpr std::size_t kLitCrossingVaryings = 14;

        void lit_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            clip = d.constants.transform * vec4(vec3(a[0]), 1.0f);
            put(v, 0, a[1]);
            put(v, 4, vec2(a[2]) + vec2(0.0f, frac(d.constants.c.w)));
            put(v, 6, vec3(a[3]));
            put(v, 9, vec3(a[0]));
        }

        void lit_crossing_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            lit_vertex(d, a, clip, v);
            put(v, 12, vec2(0.0f));
            if (d.eye.w < 0.5f)
                return;
            const vec4 band = a[4];
            const vec2 side { a[5] };
            const float through = saturate((glm::distance(vec3(d.eye), vec3(band)) - band.w) / std::max(side.x - band.w, 1e-3f));
            put(v, 12, vec2(through, side.y));
        }

        // How much of the sun reaches `world` by the shadow map in `shadow`, drawn as row `row` of the sun's view
        // says. shortcut: one blended 2 by 2 read where lit.slang averages 3 by 3 of them, so the shadow's edge is
        // harder; read more if the edges show stairs.
        float sunlit(const DrawState& d, const Texture& shadow, int row, vec3 world, vec3 n)
        {
            const Texture& view_texels = extra(d, 1);
            const glm::mat4 view(load(view_texels, 0, row), load(view_texels, 1, row), load(view_texels, 2, row), load(view_texels, 3, row));
            const vec4 more = load(view_texels, 4, row);
            const float facing = std::clamp(glm::dot(n, vec3(d.constants.a)), 0.05f, 1.0f);
            const float lean = std::min(std::sqrt(1.0f - facing * facing) / facing, 6.0f);
            const vec4 clip = view * vec4(world + n * more.x * (1.0f + lean), 1.0f);
            const vec3 p = vec3(clip) / clip.w;
            const float edge = std::max(std::abs(p.x), std::abs(p.y));
            if (edge >= 1.0f || p.z <= 0.0f || p.z >= 1.0f)
                return 1.0f;
            const vec2 size = size_of(shadow);
            const vec2 at = vec2(0.5f + 0.5f * p.x, 0.5f - 0.5f * p.y) * size;
            const float depth = p.z - more.y * (1.0f + 2.0f * lean);
            const vec2 corner = glm::floor(at - 0.5f);
            const vec4 stored = gather_red(shadow, (corner + 1.0f) / size, d.address);
            const vec4 lit = glm::step(vec4(depth), stored);
            const vec2 t = at - 0.5f - corner;
            const float sum = glm::mix(glm::mix(lit.w, lit.z, t.x), glm::mix(lit.x, lit.y, t.x), t.y);
            return glm::mix(sum, 1.0f, smooth(0.85f, 1.0f, edge));
        }

        constexpr int kLampTexelsAcross = 256;
        constexpr int kLampsPerCell = 16;

        vec3 lamplight(const DrawState& d, vec3 world, vec3 n)
        {
            const Texture& lamps = extra(d, 4);
            const auto texel = [&](int i) { return load(lamps, i % kLampTexelsAcross, i / kLampTexelsAcross); };
            const vec4 grid = texel(0);
            const vec4 more = texel(1);
            const glm::ivec2 cell { glm::floor((vec2(world.x, world.z) - vec2(grid)) / grid.z) };
            if (cell.x < 0 || cell.y < 0 || cell.x >= static_cast<int>(grid.w) || cell.y >= static_cast<int>(more.x))
                return vec3(0.0f);
            const int start = static_cast<int>(more.z) + (cell.y * static_cast<int>(grid.w) + cell.x) * (kLampsPerCell / 4);
            vec3 sum(0.0f);
            for (int t = 0; t < kLampsPerCell / 4; ++t)
            {
                const vec4 four = texel(start + t);
                for (int k = 0; k < 4; ++k)
                {
                    if (four[k] < 0.0f)
                        return sum;
                    const int lamp = static_cast<int>(four[k]);
                    const vec4 place = texel(2 + 2 * lamp);
                    const vec3 to = vec3(place) - world;
                    const float d2 = glm::dot(to, to) / (place.w * place.w);
                    if (d2 >= 1.0f)
                        continue;
                    const float fall = (1.0f - d2) * (1.0f - d2);
                    const float facing = saturate((glm::dot(n, to / std::sqrt(std::max(glm::dot(to, to), 1e-6f))) + 0.3f) / 1.3f);
                    sum += vec3(texel(3 + 2 * lamp)) * fall * facing;
                }
            }
            return sum;
        }

        float variation(const DrawState& d, vec3 world)
        {
            const Texture& noise = extra(d, 2);
            const vec2 xz { world.x, world.z };
            float n = 0.5f * read(d, noise, xz / 1920.0f).r;
            n += 0.3f * read(d, noise, xz / 800.0f + 0.37f).r;
            n += 0.2f * read(d, noise, xz / 320.0f + 0.71f).r;
            return 1.0f + 0.15f * (2.0f * n - 1.0f);
        }

        constexpr std::array<float, 16> kDither { 1, 9, 3, 11, 13, 5, 15, 7, 4, 12, 2, 10, 16, 8, 14, 6 };

        vec3 eased_off(vec3 c)
        {
            constexpr float kKnee = 0.9f;
            const float top = std::max(std::max(c.r, c.g), c.b);
            if (top <= kKnee)
                return c;
            const float eased = kKnee + (1.0f - kKnee) * (1.0f - std::exp((kKnee - top) / (1.0f - kKnee)));
            return c * (eased / top);
        }

        // kMode as lit.slang's: 1 cut out, 2 a second picture added, 4 chrome. shortcut: surface detail (the grain
        // and the relief map) is left out; add it when the software backend is used to compare detail.
        template<int kMode> bool lit_fragment(const DrawState& d, const Fragment& f, vec4& out)
        {
            const std::span<const float> v = f.varyings;
            const vec4 vertex_colour = at4(v, 0);
            const vec2 uv { v[4], v[5] };
            const vec3 normal = at3(v, 6);
            const vec3 world = at3(v, 9);
            const vec2 cross = v.size() >= kLitCrossingVaryings ? vec2(v[12], v[13]) : vec2(0.0f);
            const ShaderConstants& u = d.constants;
            if (cross.y != 0.0f)
            {
                const int px = static_cast<int>(f.position.x) & 3;
                const int py = static_cast<int>(f.position.y) & 3;
                const bool coarser = kDither[static_cast<std::size_t>(py * 4 + px)] / 16.0f <= cross.x;
                if (coarser != (cross.y > 0.0f))
                    return false;
            }
            if (!d.shade)
            {
                // Only whether the pixel is drawn: the discards below, by the picture's alpha.
                const float alpha = (kMode == 4 ? 1.0f : read(d, texture(d), uv).a) * vertex_colour.a;
                const auto flags = static_cast<std::uint32_t>(u.c.w);
                if (((flags & 64u) != 0u && alpha < 0.25f) || (kMode == 1 && alpha < 0.5f))
                    return false;
                out = vec4(0.0f, 0.0f, 0.0f, 1.0f);
                return true;
            }
            vec4 layer(0.0f);
            if constexpr (kMode == 2)
                layer = read(d, extra(d, 2), uv - vec2(0.0f, frac(u.c.w)));
            vec3 n(0.0f, 1.0f, 0.0f);
            if (glm::dot(normal, normal) > 1e-12f)
                n = glm::normalize(normal);
            const auto flags = static_cast<std::uint32_t>(u.c.w);
            const bool variation_given = kMode < 2 && (flags & 1u) != 0u;
            vec4 colour = read(d, texture(d), uv) * vertex_colour;
            const vec3 ahead = d.eye.w > 0.5f ? glm::normalize(world - vec3(d.eye)) : glm::normalize(vec3(0.0f, -1.0f, 1.0f));
            if constexpr (kMode == 4)
            {
                const vec3 right = glm::normalize(glm::cross(vec3(0.0f, 1.0f, 0.0f), ahead) + vec3(1e-6f, 0.0f, 0.0f));
                const vec3 up = glm::cross(ahead, right);
                const vec3 bounced = glm::reflect(ahead, n);
                const vec2 at { 0.5f + 0.5f * glm::dot(bounced, right), 0.5f - 0.5f * glm::dot(bounced, up) };
                colour = vec4(vec3(read(d, texture(d), at)), 1.0f) * vertex_colour;
            }
            if (std::abs(cross.y) > 1.5f)
                colour = vec4(glm::mix(vec3(colour), cross.y > 0.0f ? vec3(1.0f, 0.0f, 0.0f) : vec3(0.0f, 0.0f, 1.0f), 0.6f), colour.a);
            if (variation_given)
                colour = vec4(vec3(colour) * variation(d, world), colour.a);
            if ((flags & 64u) != 0u && colour.a < 0.25f)
                return false;
            if constexpr (kMode == 1)
            {
                if (colour.a < 0.5f)
                    return false;
                colour.a = 1.0f;
            }
            if ((flags & 32u) != 0u)
            {
                const vec3 view = d.eye.w > 0.5f ? glm::normalize(vec3(d.eye) - world) : n;
                const float edge = 1.0f - std::abs(glm::dot(n, view));
                out = vec4(eased_off(vec3(vertex_colour) * vec3(u.c)), colour.a * glm::mix(0.35f, 1.0f, edge));
                return true;
            }
            const vec3 sun_direction { u.a };
            vec3 ambient = glm::mix(vec3(u.d), vec3(u.c), n.y * 0.5f + 0.5f);
            vec3 direct = vec3(u.b) * std::max(glm::dot(n, sun_direction), 0.0f);
            float sun_reaches = 1.0f;
            if (u.a.w > 0.5f && glm::dot(direct, direct) > 0.0f)
            {
                const float before = u.a.w - 1.0f;
                float lit = sunlit(d, extra(d, 0), 0, world, n);
                if (before > 0.002f)
                    lit = glm::mix(lit, sunlit(d, extra(d, 3), 1, world, n), before);
                direct *= lit;
                ambient *= glm::mix(0.7f, 1.0f, lit);
                sun_reaches *= lit;
            }
            vec3 shaded = vec3(colour) * (ambient + direct);
            if constexpr (kMode == 4)
                shaded += vec3(u.b) * std::pow(std::max(glm::dot(n, glm::normalize(sun_direction - ahead)), 0.0f), 20.0f) * sun_reaches;
            if ((flags & 16u) != 0u)
                shaded += vec3(colour) * lamplight(d, world, n);
            if constexpr (kMode == 2)
                shaded += vec3(layer) * 0.6f * glm::min(ambient + direct, vec3(1.0f));
            shaded = eased_off(shaded);
            if (u.b.w > 0.0f)
            {
                const auto packed = static_cast<std::uint32_t>(u.d.w);
                const vec3 haze = vec3(static_cast<float>((packed >> 16) & 255u), static_cast<float>((packed >> 8) & 255u), static_cast<float>(packed & 255u)) / 255.0f;
                const float far = 1.0f - std::exp(-u.b.w * std::max(1.0f / f.position.w - 150.0f, 0.0f));
                shaded = glm::mix(shaded, haze, far);
            }
            out = vec4(shaded, colour.a);
            return true;
        }

        // Shape (shape.slang). Varyings: from the middle and half size, radii, edge, top, bottom, outline, light,
        // cut, fill.
        void shape_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            clip = d.constants.transform * vec4(a[0].x, a[0].y, 0.0f, 1.0f);
            for (std::size_t i = 0; i < 8; ++i)
                put(v, i * 4, a[i + 1]);
            put(v, 32, vec2(a[9]));
        }

        float corner_radius(vec4 radii, vec2 p)
        {
            if (p.y < 0.0f)
                return p.x < 0.0f ? radii.x : radii.y;
            return p.x < 0.0f ? radii.w : radii.z;
        }

        float box_distance(vec4 radii, vec2 p, vec2 half_size)
        {
            const float r = corner_radius(radii, p);
            const vec2 q = glm::abs(p) - half_size + r;
            return glm::length(glm::max(q, 0.0f)) + std::min(std::max(q.x, q.y), 0.0f) - r;
        }

        vec4 over(vec4 under, vec4 top, float amount)
        {
            const float a = top.a * amount;
            const float result = a + under.a * (1.0f - a);
            if (result <= 0.0f)
                return vec4(0.0f);
            return vec4((vec3(top) * a + vec3(under) * under.a * (1.0f - a)) / result, result);
        }

        bool shape_fragment(const DrawState&, const Fragment& f, vec4& out)
        {
            const std::span<const float> v = f.varyings;
            const vec2 p { v[0], v[1] };
            const vec2 half_size { v[2], v[3] };
            const vec4 radii = at4(v, 4);
            const vec4 edge = at4(v, 8);
            const vec4 cut = at4(v, 28);
            const vec2 fill { v[32], v[33] };
            float dist = box_distance(radii, p, half_size);
            const float ring = edge.w;
            if (ring > 0.0f)
                dist = std::abs(dist + ring / 2.0f) - ring / 2.0f;
            if (cut.z > 0.0f)
                dist = std::max(dist, cut.z - glm::length(p - vec2(cut)));
            const float t = saturate(0.5f - dist / std::max(edge.x, 1.0f));
            const float coverage = edge.x > 1.0f ? t * t * (3.0f - 2.0f * t) : t;
            if (coverage <= 0.0f)
                return false;
            const float share = half_size.y > 0.0f ? (p.y + half_size.y) / (2.0f * half_size.y) : 0.0f;
            const float span = fill.y - fill.x;
            const float along = saturate(span > 0.0f ? (share - fill.x) / span : (share < fill.x ? 0.0f : 1.0f));
            vec4 colour = glm::mix(at4(v, 12), at4(v, 16), along);
            const float inside = -dist;
            if (edge.z > 0.0f)
            {
                // facing_up in shape.slang
                const float r = corner_radius(radii, p);
                const vec2 q = glm::abs(p) - half_size + r;
                const float side = p.y < 0.0f ? -1.0f : 1.0f;
                float ny = 0.0f;
                if (q.x > 0.0f && q.y > 0.0f)
                    ny = side * q.y / glm::length(q);
                else if (q.y >= q.x)
                    ny = side;
                if (ring > 0.0f && box_distance(radii, p, half_size) < -ring / 2.0f)
                    ny = -ny;
                colour = over(colour, at4(v, 24), saturate(1.0f - inside / edge.z) * saturate(-ny));
            }
            if (edge.y > 0.0f)
                colour = over(colour, at4(v, 20), saturate(edge.y - inside + 0.5f));
            colour.a *= coverage;
            out = colour;
            return true;
        }

        // Water (water.slang). Constants: eye and time, towards the sun and the near plane, sunlight and the far
        // plane, skylight and flags. Extras: mirror, scene behind, its depth, ripples, shadow, sun's view.
        // Varyings: world, colour, depth, clip, uv, normal.
        constexpr std::uint32_t kReflection = 1;
        constexpr std::uint32_t kRefraction = 2;
        constexpr std::uint32_t kWaves = 4;
        constexpr std::uint32_t kFoam = 8;
        constexpr std::uint32_t kRipples = 16;
        constexpr std::uint32_t kUnderwater = 32;
        constexpr std::uint32_t kShadow = 64;
        constexpr std::uint32_t kShafts = 128;
        constexpr std::uint32_t kFlow = 256;

        constexpr float kFresnelBase = 0.02f;
        constexpr float kMirrorLeast = 0.42f;
        constexpr float kClearMirror = 0.12f;
        constexpr vec2 kClearToMurky { 0.3f, 0.6f };
        constexpr vec2 kMirrorTint { 0.45f, 1.8f };
        constexpr float kWaterIndex = 1.33f;
        constexpr vec3 kAbsorption { 0.80f, 0.30f, 0.12f };
        constexpr vec3 kMurk { 0.015f };
        constexpr float kInsideClarity = 0.2f;
        constexpr vec3 kDeepClarity { 0.05f, 0.10f, 0.16f };
        constexpr vec2 kWaveScale { 1.0f / 11.0f, 1.0f / 3.7f };
        constexpr vec2 kWaveDriftA { 0.010f, 0.006f };
        constexpr vec2 kWaveDriftB { -0.016f, 0.012f };
        constexpr float kWaveHeight = 0.08f;
        constexpr float kWaveCalm = 80.0f;
        constexpr float kReflectionBend = 0.6f;
        constexpr float kRefractionBend = 0.35f;
        constexpr float kShoreSoftness = 0.08f;
        constexpr float kFoamDepth = 0.35f;
        constexpr float kFoamMost = 0.28f;
        constexpr vec3 kFlowColour { 0.16f, 0.42f, 0.46f };
        constexpr float kCausticScale = 0.55f;
        constexpr float kCausticLine = 0.09f;
        constexpr float kCausticFade = 7.0f;
        constexpr float kCausticGain = 2.2f;
        constexpr float kCausticDim = 0.25f;
        constexpr float kShaftReach = 12.0f;
        constexpr float kShaftGain = 0.5f;
        constexpr float kCrestLow = 0.015f;
        constexpr float kCrestHigh = 0.050f;
        constexpr float kPoolSpeed = 3.0f;
        constexpr float kPoolReach = 8.0f;
        constexpr float kPoolCut = 14.0f;
        constexpr float kPoolSpread = 0.25f;
        constexpr float kFrontSoft = 0.75f;
        constexpr float kPoolRamp = 2.0f;
        constexpr float kLineSpeed = 3.5f;
        constexpr float kLineLength = 2.5f;
        constexpr float kLineWidth = 2.5f;
        constexpr float kLineFade = 4.0f;
        constexpr float kLineLife = 16.0f;
        constexpr float kWhirlLife = 10.0f;
        constexpr float kWhirlTurn = 3.0f;
        constexpr float kWhirlArms = 0.6f;
        constexpr float kWhirlWind = 3.0f;
        constexpr float kRingHeight = 0.14f;
        constexpr float kRingSpeed = 1.6f;
        constexpr float kRingLength = 0.7f;
        constexpr float kRingWidth = 0.6f;
        constexpr float kRingFade = 2.0f;
        constexpr float kRingSpread = 2.0f;
        constexpr float kRingLife = 5.0f;
        constexpr float kHeadroom = 1.5f;
        constexpr float kPoolEdge = 1.0f;
        constexpr float kRimRoom = 0.08f;
        constexpr float kRimRise = 0.3f;
        constexpr float kPoolFoam = 0.55f;
        constexpr float kPoolLight = 1.2f;
        constexpr float kFlowDepth = 0.25f;
        constexpr float kFlowLayer = 0.12f;
        constexpr float kFlowBump = 0.004f;

        bool has(const DrawState& d, std::uint32_t flag)
        {
            return (static_cast<std::uint32_t>(d.constants.d.w) & flag) != 0u;
        }

        // The ripples' slope in x and z, and how high the highest crest stands (ripples in water.slang): row 0 and 1
        // of the ripples' texture, one ring a column.
        vec3 ripples(const DrawState& d, vec2 at)
        {
            const Texture& rings = extra(d, 3);
            vec2 slope(0.0f);
            float crest = 0.0f;
            const int count = static_cast<int>(size_of(rings).x);
            for (int i = 0; i < count; ++i)
            {
                const vec4 ring = load(rings, i, 0);
                if (ring.w <= 0.0f)
                    continue;
                const vec4 shape = load(rings, i, 1);
                const vec2 off = at - vec2(ring);
                const float r = glm::length(off);
                const float x = r - ring.z;
                const float u = x / shape.y;
                if (std::abs(u) > 3.0f)
                    continue;
                const float envelope = std::exp(-u * u);
                const float c = std::cos(shape.x * x);
                const float s = std::sin(shape.x * x);
                const float dh = ring.w * envelope * (-2.0f * x / (shape.y * shape.y) * c - shape.x * s);
                slope += dh * off / std::max(r, 1e-3f);
                crest = std::max(crest, ring.w * envelope * c);
            }
            return { slope, crest };
        }

        // How high the pools' waves stand at `at` on water at `level`, and their foam (pool_wave_height in
        // water.slang): rows 2 to 5 of the ripples' texture, one wave a column.
        vec2 pool_wave_height(const DrawState& d, vec2 at, float level, float time)
        {
            const Texture& waves = extra(d, 3);
            const glm::ivec2 size { size_of(waves) };
            if (size.y < 6)
                return vec2(0.0f);
            float sum = 0.0f;
            float foam = 0.0f;
            for (int i = 0; i < size.x; ++i)
            {
                const vec4 shape = load(waves, i, 3);
                if (shape.x < 0.5f)
                    continue;
                const vec4 bounds = load(waves, i, 4);
                if (at.x < bounds.x || at.y < bounds.y || at.x > bounds.z || at.y > bounds.w)
                    continue;
                const vec4 place = load(waves, i, 2);
                const vec4 timing = load(waves, i, 5);
                const vec2 from = at - vec2(place);
                const float dist = glm::dot(from, vec2(place.z, place.w));
                const float age = time - shape.z;
                float h = 0.0f;
                float f = 0.0f;
                if (shape.x < 1.5f)
                {
                    const float left = time - dist / kPoolSpeed;
                    if (dist < 0.0f || dist > kPoolCut || left < 0.0f || timing.y <= 0.0f)
                        continue;
                    const float half_front = timing.w + kPoolSpread * dist;
                    const float out = std::abs(glm::dot(from, vec2(-place.w, place.z)));
                    const float across = saturate((half_front + kFrontSoft - out) / (2.0f * kFrontSoft)) * std::sqrt(timing.w / half_front);
                    const float phase = std::fmod(left, std::max(std::max(timing.z, timing.y), 1.0f));
                    const float running = saturate(phase / kPoolRamp) * saturate((timing.y - phase) / kPoolRamp);
                    const float lifted = running * std::exp(-dist / kPoolReach) * across * saturate(kPoolCut - dist);
                    const float wave = std::sin(timing.x * left);
                    h = shape.y * lifted * wave;
                    f = smooth(kPoolFoam, 1.0f, wave) * saturate(2.5f * lifted);
                }
                else if (shape.x < 2.5f)
                {
                    if (age < 0.0f || age > kLineLife)
                        continue;
                    const float x = dist - kLineSpeed * age;
                    h = shape.y * std::exp(-age / kLineFade) * std::exp(-(x / kLineWidth) * (x / kLineWidth)) * std::cos(6.2831853f / kLineLength * x);
                    f = smooth(kPoolFoam, 1.0f, h / shape.y * std::exp(age / kLineFade)) * std::exp(-age / kLineFade);
                }
                else if (shape.x > 3.5f)
                {
                    if (age < 0.0f || age > kRingLife || std::abs(level - shape.w) > kHeadroom)
                        continue;
                    const float x = glm::length(from) - kRingSpeed * age;
                    const float fall = std::exp(-age / kRingFade) / (1.0f + kRingSpeed * age / kRingSpread);
                    const float wave = std::exp(-(x / kRingWidth) * (x / kRingWidth)) * std::cos(6.2831853f / kRingLength * x);
                    h = shape.y * fall * wave;
                    f = smooth(kPoolFoam, 1.0f, wave) * std::exp(-2.0f * age) * shape.y / kRingHeight;
                }
                else if (age >= 0.0f && age <= kWhirlLife)
                {
                    const float strength = saturate(age) * saturate((kWhirlLife - age) / 3.0f);
                    const float r = glm::length(from) / timing.w;
                    const float arm = std::sin(3.0f * std::atan2(from.y, from.x) - shape.w * (kWhirlTurn * age - kWhirlWind * r));
                    h = -shape.y * strength * std::exp(-r * r) * (1.0f - kWhirlArms * r * arm);
                    f = strength * smooth(0.4f, 0.9f, arm) * smooth(0.2f, 0.6f, r) * std::exp(-0.5f * r * r);
                }
                sum += h;
                foam = std::max(foam, f);
            }
            return { sum, foam };
        }

        // The pools' waves' slope in x and z, height and foam (pool_waves in water.slang).
        vec4 pool_waves(const DrawState& d, vec3 at, float time)
        {
            constexpr float e = 0.05f;
            const vec2 xz { at.x, at.z };
            const vec2 h = pool_wave_height(d, xz, at.y, time);
            const float dx = pool_wave_height(d, xz + vec2(e, 0.0f), at.y, time).x - pool_wave_height(d, xz - vec2(e, 0.0f), at.y, time).x;
            const float dz = pool_wave_height(d, xz + vec2(0.0f, e), at.y, time).x - pool_wave_height(d, xz - vec2(0.0f, e), at.y, time).x;
            return { dx / (2.0f * e), dz / (2.0f * e), h.x, h.y };
        }

        void water_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            // A pool's waves move its surface; the second texture coordinate is how far its wall is.
            float lift = 0.0f;
            if (has(d, kRipples) && !has(d, kFlow) && !has(d, kUnderwater))
                lift = std::min(pool_wave_height(d, vec2(a[0].x, a[0].z), a[0].y, d.constants.a.w).x * saturate(a[2].y / kPoolEdge), kRimRoom + kRimRise * a[2].y);
            const vec3 position = vec3(a[0]) + vec3(0.0f, lift, 0.0f);
            clip = d.constants.transform * vec4(position, 1.0f);
            put(v, 0, position);
            put(v, 3, a[1]);
            v[7] = a[2].x + lift;
            put(v, 8, clip);
            put(v, 12, vec2(a[2]));
            put(v, 14, vec3(a[3]));
        }

        vec2 picture_slope(const DrawState& d, vec2 p)
        {
            const Texture& waves = texture(d);
            const vec2 size = size_of(waves);
            const vec2 step = 1.0f / size;
            const float h = read(d, waves, p).r;
            const float hx = read(d, waves, p + vec2(step.x, 0.0f)).r;
            const float hy = read(d, waves, p + vec2(0.0f, step.y)).r;
            return vec2(hx - h, hy - h) * size;
        }

        float view_distance(const DrawState& d, float depth)
        {
            const float near = d.constants.b.w;
            const float far = d.constants.c.w;
            return near * far / (far - depth * (far - near));
        }

        float scene_distance(const DrawState& d, vec2 uv)
        {
            const Texture& depth = extra(d, 2);
            const vec2 size = size_of(depth.depth_of ? *depth.depth_of : depth);
            const glm::ivec2 texel { glm::clamp(uv * size, vec2(0.0f), size - 1.0f) };
            return view_distance(d, load(depth, texel.x, texel.y).r);
        }

        // 1 where the sun reaches `at`, by the shadow map's texel there (water.slang averages 3 by 3).
        float water_sunlit(const DrawState& d, vec3 at)
        {
            const Texture& view_texels = extra(d, 5);
            const glm::mat4 view(load(view_texels, 0, 0), load(view_texels, 1, 0), load(view_texels, 2, 0), load(view_texels, 3, 0));
            const vec4 more = load(view_texels, 4, 0);
            const vec4 clip = view * vec4(at, 1.0f);
            const vec3 p = vec3(clip) / clip.w;
            const float edge = std::max(std::abs(p.x), std::abs(p.y));
            if (edge >= 1.0f || p.z <= 0.0f || p.z >= 1.0f)
                return 1.0f;
            const Texture& shadow = extra(d, 4);
            const vec2 size = size_of(shadow.depth_of ? *shadow.depth_of : shadow);
            const glm::ivec2 texel { vec2(0.5f + 0.5f * p.x, 0.5f - 0.5f * p.y) * size };
            return glm::mix(glm::step(p.z - more.y * 2.0f, load(shadow, texel.x, texel.y).r), 1.0f, smooth(0.85f, 1.0f, edge));
        }

        vec3 through_water(vec4 colour, vec3 seen, float along, float eye_depth, vec3 body_light)
        {
            const vec3 absorb = kAbsorption * (0.5f + colour.a) * kInsideClarity;
            const vec3 kept = glm::exp(-(absorb + kMurk) * along);
            const vec3 glow = vec3(colour) * body_light * glm::exp(-absorb * std::max(eye_depth, 0.0f) * 0.5f);
            return seen * kept + glow * (1.0f - kept);
        }

        // The net of sunlight the waves focus on the bottom (cell_point, cell_border, caustics and caustic_factor in
        // water.slang).
        vec2 cell_point(vec2 c, float time)
        {
            const glm::uvec2 u { glm::ivec2(c) };
            std::uint32_t h = (u.x * 668265261u) ^ (u.y * 374761393u);
            h = (h ^ (h >> 13u)) * 739982445u;
            h ^= h >> 16u;
            const vec2 r = vec2(static_cast<float>(h & 65535u), static_cast<float>(h >> 16u)) / 65535.0f;
            return c + 0.5f + 0.36f * glm::sin(time * (0.5f + 0.4f * r) + 6.2832f * vec2(r.y, r.x));
        }

        float cell_border(vec2 p, float time)
        {
            const vec2 base = glm::floor(p);
            float first = 9.0f;
            float second = 9.0f;
            for (int y = -1; y <= 1; ++y)
                for (int x = -1; x <= 1; ++x)
                {
                    const vec2 q = cell_point(base + vec2(x, y), time) - p;
                    const float d = glm::dot(q, q);
                    second = std::min(second, std::max(d, first));
                    first = std::min(first, d);
                }
            return std::sqrt(second) - std::sqrt(first);
        }

        float caustics(const DrawState& d, vec2 p, float time, float blur)
        {
            const vec2 warp { read(d, texture(d), p * 0.13f + vec2(0.011f, 0.004f) * time).r, read(d, texture(d), p * 0.11f + vec2(0.37f, 0.61f) - vec2(0.006f, 0.010f) * time).r };
            p += (warp - 0.5f) * 1.2f;
            const float width = kCausticLine + blur;
            const float a = 1.0f - smooth(0.0f, width, cell_border(p, time * 0.8f));
            const float b = 1.0f - smooth(0.0f, width * 1.2f, cell_border(p * 1.41f + vec2(4.7f, 1.9f), time * 1.1f + 2.0f));
            const float net = a * a + 0.55f * b * b + 0.8f * a * b;
            return net * (kCausticLine / width) * (1.0f - smooth(0.25f, 0.6f, blur));
        }

        float caustic_factor(const DrawState& d, vec3 at, float depth, float facing, vec3 to_sun, vec3 sun, float time, float pixel)
        {
            if (!has(d, kWaves) || to_sun.y <= 0.0f || depth <= 0.0f)
                return 1.0f;
            const vec3 down = glm::refract(-to_sun, vec3(0.0f, 1.0f, 0.0f), 1.0f / kWaterIndex);
            const vec2 entry = vec2(at.x, at.z) + vec2(down.x, down.z) / std::min(down.y, -0.2f) * depth;
            float strength = smooth(0.0f, 0.25f, depth) * std::exp(-depth / kCausticFade) * smooth(0.0f, 0.3f, to_sun.y);
            strength *= saturate(glm::dot(sun, vec3(1.0f / 3.0f))) * facing;
            const float light = caustics(d, entry * kCausticScale, time, pixel * kCausticScale + depth * 0.012f);
            return 1.0f + strength * (kCausticGain * light - kCausticDim);
        }

        // Sunlight the water scatters towards the eye along `dir` from `from` (water_shafts in water.slang).
        vec3 water_shafts(const DrawState& d, vec3 from, vec3 dir, float span, float level, vec3 absorb, vec3 to_sun, vec3 sun, float time, int steps, float blur, float spread, vec2 pixel)
        {
            if (!has(d, kShafts) || !has(d, kWaves) || to_sun.y <= 0.0f || span <= 0.0f)
                return vec3(0.0f);
            const vec3 down = glm::refract(-to_sun, vec3(0.0f, 1.0f, 0.0f), 1.0f / kWaterIndex);
            const float stride = std::min(span, kShaftReach) / static_cast<float>(steps);
            const float jitter = frac(52.9829189f * frac(glm::dot(pixel, vec2(0.06711056f, 0.00583715f))));
            vec3 sum(0.0f);
            for (int i = 0; i < steps; ++i)
            {
                const float t = (static_cast<float>(i) + jitter) * stride;
                const vec3 q = from + dir * t;
                const float depth = level - q.y;
                if (depth <= 0.0f)
                    continue;
                const vec2 entry = vec2(q.x, q.z) + vec2(down.x, down.z) / std::min(down.y, -0.2f) * depth;
                const float light = caustics(d, entry * kCausticScale, time, blur + spread * t + depth * 0.02f);
                sum += light * glm::exp(-absorb * (depth / std::max(-down.y, 0.2f) + t));
            }
            return sun * sum * stride * kShaftGain * smooth(0.0f, 0.3f, to_sun.y);
        }

        // How far apart in xz the world points of neighbouring pixels are (length(fwidth(world.xz))).
        float pixel_span(const Fragment& f)
        {
            const vec3 dx = f.change<3>(0, 1, 0);
            const vec3 dy = f.change<3>(0, 0, 1);
            return glm::length(glm::abs(vec2(dx.x, dx.z)) + glm::abs(vec2(dy.x, dy.z)));
        }

        bool water_fragment(const DrawState& d, const Fragment& f, vec4& out)
        {
            const std::span<const float> v = f.varyings;
            const ShaderConstants& u = d.constants;
            const vec3 world = at3(v, 0);
            const vec4 colour = at4(v, 3);
            const float water_depth = v[7];
            const vec4 clip = at4(v, 8);
            const vec3 sun = vec3(u.c) * (has(d, kShadow) ? water_sunlit(d, world) : 1.0f);
            const vec3 to_sun = glm::normalize(vec3(u.b));
            const vec3 sky { u.d };
            const vec3 body_light = sky + sun * std::max(to_sun.y, 0.0f) * 0.5f;
            const vec3 eye { u.a };
            if (has(d, kUnderwater))
            {
                const vec2 screen = screen_uv(clip);
                const vec3 ray = world - eye;
                const float ray_length = glm::length(ray);
                const float along = scene_distance(d, screen) * ray_length / clip.w;
                const vec3 at = eye + ray / ray_length * along;
                // shortcut: lit as if facing the sunlight square on, where water.slang takes the seen surface's normal.
                const vec3 seen = vec3(read(d, extra(d, 1), screen)) * caustic_factor(d, at, water_depth - at.y, 1.0f, to_sun, sun, u.a.w, pixel_span(f));
                const vec3 shafts = water_shafts(d, eye, ray / ray_length, along, water_depth, kAbsorption * (0.5f + colour.a) * kInsideClarity, to_sun, sun, u.a.w, 16, 0.12f, 0.012f, vec2(f.position));
                out = vec4(through_water(colour, seen, along, water_depth - eye.y, body_light) + shafts, 1.0f);
                return true;
            }
            const vec3 to_eye = eye - world;
            const float dist = glm::length(to_eye);
            const vec3 view = to_eye / std::max(dist, 1e-4f);
            const auto fresnel_of = [](float facing) { return kFresnelBase + (1.0f - kFresnelBase) * std::pow(1.0f - facing, 5.0f); };
            if (has(d, kFlow))
            {
                // flowing() in water.slang, its frame from how the world point and uv change to the next pixel.
                vec3 n0 = glm::normalize(at3(v, 14) + vec3(0.0f, 1e-6f, 0.0f));
                if (glm::dot(n0, view) < 0.0f)
                    n0 = -n0;
                const vec3 dp1 = f.change<3>(0, 1, 0);
                const vec3 dp2 = f.change<3>(0, 0, 1);
                const vec2 duv1 = f.change<2>(12, 1, 0);
                const vec2 duv2 = f.change<2>(12, 0, 1);
                const vec3 p2 = glm::cross(dp2, n0);
                const vec3 p1 = glm::cross(n0, dp1);
                vec3 across = p2 * duv1.x + p1 * duv2.x;
                vec3 along = p2 * duv1.y + p1 * duv2.y;
                const float scale = 1.0f / std::sqrt(std::max(std::max(glm::dot(across, across), glm::dot(along, along)), 1e-12f));
                across *= scale;
                along *= scale;
                const vec2 uv = vec2(v[12], v[13]) + vec2(0.0f, frac(u.d.w));
                const vec2 sideways = vec2(glm::dot(view, across), glm::dot(view, along)) / std::max(glm::dot(view, n0), 0.2f);
                const vec2 deep = uv + vec2(0.5f, 0.37f) - sideways * kFlowLayer;
                const vec4 top = read(d, texture(d), uv);
                const vec4 below = read(d, texture(d), deep);
                const vec2 slope = picture_slope(d, uv);
                const vec3 n = glm::normalize(n0 - (slope.x * across + slope.y * along) * kFlowBump);
                const float facing = saturate(glm::dot(n, view));
                const float fresnel = fresnel_of(facing);
                const float path = kFlowDepth / std::max(facing, 0.15f);
                const vec3 kept = glm::exp(-kAbsorption * path);
                const float kept_mean = glm::dot(kept, vec3(1.0f / 3.0f));
                const float caustic = caustic_factor(d, world - view * path, kFlowDepth, 1.0f, to_sun, sun, u.a.w, pixel_span(f));
                const vec3 sunlit = sun * std::max(to_sun.y, 0.0f) * 0.4f * (caustic - 1.0f) * kept;
                const vec3 scattered = kFlowColour * body_light;
                const float foam = glm::dot(vec3(top), vec3(1.0f / 3.0f)) * top.a;
                const float sunk = glm::dot(vec3(below), vec3(1.0f / 3.0f)) * below.a * kept_mean;
                const float shine = std::max(glm::dot(n, glm::normalize(to_sun + view)), 0.0f);
                const vec3 glint = sun * (std::pow(shine, 600.0f) + std::pow(shine, 48.0f) * 0.05f) * (to_sun.y >= 0.0f ? 1.0f : 0.0f);
                const float through = (1.0f - fresnel) * kept_mean * (1.0f - foam) * (1.0f - 0.5f * sunk);
                float alpha = 1.0f - through;
                const vec3 water = (1.0f - fresnel) * (scattered * (1.0f - kept_mean) + sunlit) + fresnel * sky * kFlowColour * 2.5f;
                const vec3 shown = (water * (1.0f - foam) + body_light * (foam + 0.5f * sunk * (1.0f - foam)) + glint) / std::max(alpha, 1e-3f);
                alpha *= (1.0f - smooth(0.6f, 0.95f, dist / u.c.w)) * colour.a;
                out = vec4(shown, alpha);
                return true;
            }
            const float time = u.a.w;
            vec2 slope(0.0f);
            if (has(d, kWaves))
            {
                const vec2 xz { world.x, world.z };
                const float calm = 1.0f / (1.0f + dist / kWaveCalm);
                const vec2 broad = picture_slope(d, xz * kWaveScale.x + time * kWaveDriftA) * kWaveScale.x;
                const vec2 fine = picture_slope(d, xz * kWaveScale.y + time * kWaveDriftB) * kWaveScale.y;
                slope += (broad + 0.5f * fine) * kWaveHeight * calm;
            }
            const vec3 rip = has(d, kRipples) ? ripples(d, vec2(world.x, world.z)) : vec3(0.0f);
            const vec4 pool = has(d, kRipples) ? pool_waves(d, world, time) * saturate(v[13] / kPoolEdge) : vec4(0.0f);
            slope += vec2(rip) + vec2(pool);
            const vec3 n = glm::normalize(vec3(-slope.x, 1.0f, -slope.y));
            const vec2 screen = screen_uv(clip);
            const vec2 bend = vec2(n.x, n.z) / (1.0f + 0.02f * dist);
            if (view.y < 0.0f)
            {
                // from_below() in water.slang.
                const vec3 out_dir = glm::refract(-view, -n, kWaterIndex);
                float reflected = 1.0f;
                vec3 above = sky;
                if (glm::length(out_dir) > 0.0f)
                {
                    reflected = fresnel_of(saturate(glm::dot(out_dir, n)));
                    if (has(d, kRefraction))
                        above = vec3(read(d, extra(d, 1), glm::clamp(screen + bend * kRefractionBend, vec2(0.001f), vec2(0.999f))));
                    above = above * 1.8f + sun * std::pow(std::max(glm::dot(out_dir, to_sun), 0.0f), 300.0f) * 2.0f;
                }
                const vec3 shown = glm::mix(above, vec3(colour) * body_light * 0.4f, reflected);
                out = vec4(through_water(colour, shown, dist, world.y - eye.y, body_light), smooth(0.0f, kShoreSoftness, water_depth));
                return true;
            }
            float depth = water_depth;
            float path = depth / std::max(view.y, 0.15f);
            vec3 below(0.0f);
            const bool see_below = has(d, kRefraction);
            if (see_below)
            {
                vec2 bent = screen + bend * kRefractionBend * saturate(depth);
                float behind = scene_distance(d, bent);
                if (behind < clip.w)
                {
                    bent = screen;
                    behind = scene_distance(d, screen);
                }
                path = std::max(behind - clip.w, 0.0f) * dist / std::max(clip.w, 1e-4f);
                depth = path * view.y;
                below = vec3(read(d, extra(d, 1), glm::clamp(bent, vec2(0.001f), vec2(0.999f))));
            }
            const float caustic = caustic_factor(d, world - view * path, depth, 1.0f, to_sun, sun, time, pixel_span(f));
            const vec3 shafts = water_shafts(d, world, -view, path, world.y, kAbsorption * (0.5f + colour.a), to_sun, sun, time, 8, pixel_span(f) * kCausticScale, 0.0f, vec2(f.position));
            const float facing = saturate(glm::dot(n, view));
            const float least = glm::mix(kClearMirror, kMirrorLeast, smooth(kClearToMurky.x, kClearToMurky.y, colour.a));
            const float fresnel = glm::mix(least, 1.0f, fresnel_of(facing));
            vec3 mirrored = sky;
            if (has(d, kReflection))
                mirrored = vec3(read(d, extra(d, 0), glm::clamp(screen + bend * kReflectionBend * 0.05f, vec2(0.001f), vec2(0.999f))));
            mirrored *= glm::mix(vec3(1.0f), vec3(colour) * kMirrorTint.y, kMirrorTint.x);
            const vec3 scattered = vec3(colour) * body_light;
            const vec3 kept = glm::mix(kDeepClarity, vec3(1.0f), glm::exp(-kAbsorption * (0.5f + colour.a) * path));
            const float kept_mean = glm::dot(kept, vec3(1.0f / 3.0f));
            const float shine = std::max(glm::dot(n, glm::normalize(to_sun + view)), 0.0f);
            const vec3 glint = sun * (std::pow(shine, 900.0f) * 1.2f + std::pow(shine, 64.0f) * 0.04f) * (to_sun.y >= 0.0f ? 1.0f : 0.0f);
            vec3 shown;
            float alpha = 1.0f;
            if (see_below)
            {
                shown = glm::mix(below * caustic * kept + scattered * (1.0f - kept) + shafts, mirrored, fresnel) + glint;
            }
            else
            {
                alpha = 1.0f - (1.0f - fresnel) * kept_mean;
                const vec3 sunlit = sun * std::max(to_sun.y, 0.0f) * 0.4f * (caustic - 1.0f) * kept;
                shown = ((1.0f - fresnel) * (scattered * (1.0f - kept_mean) + sunlit + shafts) + fresnel * mirrored + glint) / std::max(alpha, 1e-3f);
            }
            shown *= 1.0f + kPoolLight * std::clamp(pool.z, -0.5f, 0.5f);
            if (has(d, kFoam))
            {
                const vec2 xz { world.x, world.z };
                const float shallows = (1.0f - smooth(0.0f, kFoamDepth, depth)) * smooth(0.0f, 0.04f, depth);
                const float grain = read(d, texture(d), xz * 0.31f + vec2(0.020f, -0.013f) * time).r;
                const float grain2 = read(d, texture(d), xz * 0.77f - vec2(0.011f, 0.017f) * time).r;
                const float broken = smooth(0.55f, 0.9f, 0.6f * grain + 0.4f * grain2 + 0.3f * shallows);
                const float foam = std::max({ shallows * broken, 0.6f * smooth(kCrestLow, kCrestHigh, rip.z), pool.w * (0.5f + 0.5f * smooth(0.4f, 0.8f, grain)) }) * kFoamMost / (1.0f + dist / 150.0f);
                shown = glm::mix(shown, body_light * 0.95f, foam);
                alpha = glm::mix(alpha, 1.0f, foam);
            }
            alpha *= smooth(0.0f, kShoreSoftness, depth) * (1.0f - smooth(0.6f, 0.95f, dist / u.c.w));
            out = vec4(shown, alpha);
            return true;
        }

        // Waterfall (waterfall.slang). Constants and extras as Water's. Varyings: world, colour, clip, uv, normal,
        // mist.
        constexpr std::uint32_t kSheet = 0;
        constexpr std::uint32_t kMist = 2;
        constexpr std::uint32_t kMedium = 1;
        constexpr std::uint32_t kHigh = 2;
        constexpr std::uint32_t kUltra = 3;
        constexpr float kGravity = 9.81f;
        constexpr float kStrandAcross = 0.09f;
        constexpr vec2 kStreakAcross { 0.23f, 0.61f };
        constexpr vec2 kStreakAlong { 0.22f, 0.5f };
        constexpr float kWhiteAfter = 0.7f;
        constexpr vec2 kWhiteShare { 0.08f, 0.35f };
        constexpr vec2 kTallDrop { 25.0f, 65.0f };
        constexpr float kTallWhite = 0.45f;
        constexpr float kNoiseSpread = 0.55f;
        constexpr vec2 kStreakSoft { 0.09f, 0.035f };
        constexpr float kSheetAlpha = 0.93f;
        constexpr float kSheetGlow = 1.5f;
        constexpr float kGapMost = 0.8f;
        constexpr float kGapBend = 0.012f;
        constexpr float kWhiteAlbedo = 0.8f;
        constexpr float kWhiteSun = 0.85f;
        constexpr float kWhiteSunLeast = 0.4f;
        constexpr float kFoamWhite = 0.82f;
        constexpr float kFoamRingLength = 2.6f;
        constexpr float kFoamRingRate = 0.45f;
        constexpr float kFoamRingWidth = 0.16f;
        constexpr float kMistRate = 0.11f;
        constexpr float kMistRise = 0.9f;
        constexpr float kMistSoft = 1.5f;
        constexpr float kMistAlpha = 0.38f;
        constexpr float kMistAbove = 0.65f;
        constexpr float kMistBright = 1.15f;
        constexpr float kMistForward = 0.4f;
        constexpr float kDropletRest = 0.4f;
        constexpr float kDropletStretch = 0.12f;

        std::uint32_t fall_part(const DrawState& d)
        {
            return (static_cast<std::uint32_t>(d.constants.d.w) >> 9u) & 3u;
        }

        std::uint32_t fall_detail(const DrawState& d)
        {
            return (static_cast<std::uint32_t>(d.constants.d.w) >> 11u) & 3u;
        }

        void waterfall_vertex(const DrawState& d, std::span<const vec4> a, vec4& clip, std::span<float> v)
        {
            const vec3 position { a[0] };
            const vec4 color = a[1];
            const vec2 uv { a[2] };
            const vec3 normal { a[3] };
            if (fall_part(d) != kMist)
            {
                clip = d.constants.transform * vec4(position, 1.0f);
                put(v, 0, position);
                put(v, 3, color);
                put(v, 7, clip);
                put(v, 11, uv);
                put(v, 13, normal);
                put(v, 16, vec2(0.0f));
                return;
            }
            const float time = d.constants.a.w;
            const vec3 eye { d.constants.a };
            vec3 middle = position;
            vec3 right;
            vec3 up;
            vec2 size;
            vec4 colour;
            if (normal.z < 0.0f)
            {
                const vec3 speed { color };
                const float flight = 2.0f * std::max(speed.y, 0.1f) / kGravity;
                const float loop = flight * (1.0f + kDropletRest);
                const float t = frac(time / loop + normal.y) * loop;
                const float flying = t <= flight ? 1.0f : 0.0f;
                const vec3 moving = speed - vec3(0.0f, kGravity * t, 0.0f);
                middle = position + speed * t - vec3(0.0f, 0.5f * kGravity * t * t, 0.0f);
                const vec3 away = glm::normalize(middle - eye);
                const vec3 along = moving - away * glm::dot(moving, away);
                up = glm::dot(along, along) > 1e-6f ? glm::normalize(along) : vec3(0.0f, 1.0f, 0.0f);
                right = glm::normalize(glm::cross(up, away) + vec3(1e-6f, 0.0f, 0.0f));
                size = normal.x * vec2(1.0f, 1.0f + kDropletStretch * glm::length(moving)) * flying;
                colour = vec4(1.0f, 1.0f, 1.0f, flying * (1.0f - std::pow(t / flight, 4.0f)));
            }
            else
            {
                const float age = frac(time * kMistRate + normal.y);
                middle = position + vec3(0.0f, age * normal.x * kMistRise, 0.0f);
                const vec3 away = middle - eye;
                right = glm::normalize(glm::cross(away, vec3(0.0f, 1.0f, 0.0f)) + vec3(1e-6f, 0.0f, 0.0f));
                up = glm::normalize(glm::cross(right, away));
                size = vec2(normal.x * (0.55f + 0.9f * age));
                colour = vec4(vec3(color), normal.z * smooth(0.0f, 0.15f, age) * (1.0f - age * age));
            }
            const vec3 at = middle + right * uv.x * size.x + up * uv.y * size.y;
            clip = d.constants.transform * vec4(at, 1.0f);
            const float lit = has(d, kShadow) && fall_detail(d) >= kUltra ? water_sunlit(d, middle) : 1.0f;
            put(v, 0, at);
            put(v, 3, colour);
            put(v, 7, clip);
            put(v, 11, uv);
            put(v, 13, normal);
            put(v, 16, vec2(size.x, lit));
        }

        vec3 white_light(vec3 sky, vec3 sun, vec3 to_sun)
        {
            const vec3 light = kWhiteAlbedo * (sky + sun * (kWhiteSunLeast + (1.0f - kWhiteSunLeast) * std::max(to_sun.y, 0.0f)) * kWhiteSun);
            return glm::mix(light, vec3(glm::dot(light, vec3(1.0f / 3.0f))), 0.5f);
        }

        // shortcut: where waterfall.slang softens streak and ring edges by how fast they change across the pixel,
        // these keep their least softness, and the lake's rings round the foam at Ultra are left out.
        bool waterfall_fragment(const DrawState& d, const Fragment& f, vec4& out)
        {
            const std::span<const float> v = f.varyings;
            const ShaderConstants& u = d.constants;
            const vec3 world = at3(v, 0);
            const vec4 colour = at4(v, 3);
            const vec4 clip = at4(v, 7);
            const vec2 uv { v[11], v[12] };
            const vec3 normal = at3(v, 13);
            const vec2 mist { v[16], v[17] };
            const float time = u.a.w;
            const std::uint32_t level = fall_detail(d);
            const std::uint32_t drawn = fall_part(d);
            const float shadowed = has(d, kShadow) && drawn != kMist && level >= kMedium ? water_sunlit(d, world) : 1.0f;
            const vec3 sun = vec3(u.c) * shadowed;
            const vec3 to_sun = glm::normalize(vec3(u.b));
            const vec3 sky { u.d };
            const vec3 to_eye = vec3(u.a) - world;
            const float dist = glm::length(to_eye);
            const vec3 view = to_eye / std::max(dist, 1e-4f);
            const float far_fade = 1.0f - smooth(0.6f, 0.95f, dist / u.c.w);
            const Texture& waves = texture(d);
            if (drawn == kMist)
            {
                const float r2 = glm::dot(uv, uv);
                if (r2 >= 1.0f || colour.a <= 0.0f)
                    return false;
                const vec3 lit = white_light(sky, vec3(u.c) * mist.y, to_sun);
                if (normal.z < 0.0f)
                {
                    out = vec4(lit * 1.1f, (1.0f - r2) * colour.a * 0.85f);
                    return true;
                }
                float alpha = (1.0f - r2) * (1.0f - r2) * colour.a * kMistAlpha;
                if (level >= kHigh)
                    alpha *= 0.5f + read(d, waves, uv * 0.21f + vec2(normal.y * 7.3f, normal.y * 3.1f + time * 0.035f)).r;
                if (has(d, kRefraction))
                    alpha *= saturate((scene_distance(d, screen_uv(clip)) - clip.w) / std::min(0.15f * mist.x, kMistSoft));
                else
                    alpha *= 1.0f - r2;
                alpha *= saturate((clip.w - 2.0f) / 8.0f);
                alpha *= 1.0f - kMistAbove * smooth(0.5f, 0.95f, view.y);
                const float towards = saturate(glm::dot(-view, to_sun));
                out = vec4(lit * kMistBright + vec3(u.c) * mist.y * std::max(to_sun.y, 0.0f) * kMistForward * std::pow(towards, 4.0f), alpha);
                return true;
            }
            const vec3 body_light = sky + sun * std::max(to_sun.y, 0.0f) * 0.5f;
            const vec3 white = white_light(sky, sun, to_sun);
            if (drawn == kSheet)
            {
                const float half_width = normal.x;
                const float whole = std::max(normal.y, 1e-3f);
                const float tall = smooth(kTallDrop.x, kTallDrop.y, normal.z);
                const float since = uv.y;
                const float parcel = since - time;
                const float strand = read(d, waves, vec2(uv.x * kStrandAcross, 0.31f)).r;
                const float broad = read(d, waves, vec2(uv.x * kStreakAcross.x, parcel * kStreakAlong.x)).r;
                float fine = broad;
                float value = 0.75f * broad + 0.25f * strand;
                if (level >= kMedium)
                {
                    fine = read(d, waves, vec2(uv.x * kStreakAcross.y + 0.37f, parcel * kStreakAlong.y + 0.11f)).r;
                    value = 0.4f * broad + 0.3f * fine + 0.3f * strand;
                }
                const float broken = smooth(0.0f, kWhiteAfter, since);
                const float low = saturate(since / whole);
                const float share = saturate(glm::mix(kWhiteShare.x, kWhiteShare.y + kTallWhite * tall, broken));
                const float cut = 0.5f + (0.5f - share) * kNoiseSpread;
                const float soft = level >= kMedium ? kStreakSoft.y : kStreakSoft.x;
                float whiteness = smooth(cut - soft, cut + soft, value);
                whiteness = glm::mix((0.1f + 0.45f * tall) * broken, 1.0f, whiteness);
                whiteness = std::max(whiteness, smooth(0.85f, 1.0f, low) * 0.85f);
                const vec3 n = glm::normalize(glm::cross(f.change<3>(0, 1, 0), f.change<3>(0, 0, 1)) + vec3(0.0f, 1e-9f, 0.0f));
                const float facing = std::abs(glm::dot(n, view));
                const float fresnel = kFresnelBase + (1.0f - kFresnelBase) * std::pow(1.0f - facing, 5.0f);
                const vec3 mirror = sky * glm::mix(vec3(1.0f), vec3(colour) * kMirrorTint.y, kMirrorTint.x);
                vec3 water = glm::mix(vec3(colour) * body_light * kSheetGlow, mirror, glm::mix(kMirrorLeast, 1.0f, fresnel));
                water *= 1.1f - 0.25f * smooth(0.3f, 0.7f, value) * (1.0f - whiteness);
                if (level >= kMedium)
                    water += mirror * 0.5f * std::exp(-since * since / 0.02f);
                vec3 shown = glm::mix(water, white * (0.9f + 0.15f * fine), whiteness);
                float alpha = kSheetAlpha;
                if (level >= kHigh)
                {
                    const float tear = glm::mix(0.1f, 0.5f, low);
                    const float gap = (1.0f - smooth(tear - 0.12f, tear, 0.5f * fine + 0.5f * broad)) * smooth(0.15f, 0.6f, low) * (1.0f - whiteness);
                    if (level >= kUltra && has(d, kRefraction))
                    {
                        const vec2 bend = (vec2(fine, broad) - 0.5f) * kGapBend;
                        const vec3 behind { read(d, extra(d, 1), screen_uv(clip) + bend) };
                        shown = glm::mix(shown, behind * glm::mix(vec3(1.0f), vec3(colour) * 1.6f, 0.25f), kGapMost * gap);
                    }
                    else
                    {
                        alpha *= 1.0f - kGapMost * gap;
                    }
                }
                const float inside = half_width - std::abs(uv.x);
                alpha *= smooth(0.0f, 0.25f + 1.6f * broken * (1.0f - broad), inside);
                out = vec4(shown, alpha * colour.a * far_fade);
                return true;
            }
            // The foam on the lower lake.
            const float away = glm::length(vec2(std::max(std::abs(uv.x) - normal.x, 0.0f), uv.y));
            const float reach = std::max(normal.z, 1e-3f);
            if (away >= reach)
                return false;
            const vec2 xz { world.x, world.z };
            const float broad = read(d, waves, xz * 0.06f + vec2(0.006f, -0.004f) * time).r;
            const float outward = away / reach + 0.5f * (broad - 0.5f);
            float fine = broad;
            if (level >= kMedium)
                fine = read(d, waves, xz * 0.15f - vec2(0.009f, 0.007f) * time).r;
            const float grain = 0.6f * broad + 0.4f * fine;
            const float churn = 1.0f - smooth(-0.1f, 0.4f, outward);
            const float threshold = glm::mix(0.46f, 0.8f, std::sqrt(saturate(outward)));
            const float soft = level >= kMedium ? 0.05f : 0.12f;
            float cover = smooth(threshold, threshold + soft, grain + 0.22f * churn * churn);
            if (level >= kMedium)
            {
                const float phase = away / kFoamRingLength - time * kFoamRingRate + (broad - 0.5f) * 1.6f;
                const float ring = std::abs(frac(phase) - 0.5f) * 2.0f;
                const float width = kFoamRingWidth * (1.0f - 0.6f * outward);
                cover = std::max(cover, (1.0f - smooth(width, width + 0.03f, ring)) * smooth(0.42f, 0.5f, fine) * smooth(0.1f, 0.25f, outward));
            }
            cover *= (1.0f - smooth(0.6f, 1.0f, outward)) * glm::mix(1.0f, 0.75f + 0.25f * fine, churn);
            const vec3 shown = glm::mix(vec3(colour) * body_light * kSheetGlow, white, kFoamWhite) * glm::mix(0.82f, 1.08f, grain);
            out = vec4(shown, cover * 0.92f * far_fade);
            return true;
        }

    } // namespace

    glm::vec4 eye_of(const glm::mat4& m)
    {
        const vec4 x = row(m, 0);
        const vec4 y = row(m, 1);
        const vec4 w = row(m, 3);
        const float det = glm::dot(vec3(x), glm::cross(vec3(y), vec3(w)));
        if (std::abs(det) < 1e-30f)
            return vec4(0.0f);
        return vec4(-(x.w * glm::cross(vec3(y), vec3(w)) + y.w * glm::cross(vec3(w), vec3(x)) + w.w * glm::cross(vec3(x), vec3(y))) / det, 1.0f);
    }

    Program program(Shader shader)
    {
        switch (shader)
        {
            case Shader::VertexColor:
                return { vertex_color_vertex, vertex_color_fragment, 4 };
            case Shader::Textured:
                return { textured_vertex, textured_fragment, 6 };
            case Shader::TexturedCutout:
                return { textured_vertex, textured_cutout_fragment, 6 };
            case Shader::Lit:
                return { lit_vertex, lit_fragment<0>, kLitVaryings };
            case Shader::LitCutout:
                return { lit_vertex, lit_fragment<1>, kLitVaryings };
            case Shader::LitCrossing:
                return { lit_crossing_vertex, lit_fragment<0>, kLitCrossingVaryings };
            case Shader::LitCrossingCutout:
                return { lit_crossing_vertex, lit_fragment<1>, kLitCrossingVaryings };
            case Shader::LitLayerAdded:
                return { lit_vertex, lit_fragment<2>, kLitVaryings };
            case Shader::LitChrome:
                return { lit_vertex, lit_fragment<4>, kLitVaryings };
            case Shader::LitCrossingChrome:
                return { lit_crossing_vertex, lit_fragment<4>, kLitCrossingVaryings };
            case Shader::Water:
                return { water_vertex, water_fragment, 17 };
            case Shader::Waterfall:
                return { waterfall_vertex, waterfall_fragment, 18 };
            case Shader::Shape:
                return { shape_vertex, shape_fragment, 34 };
            case Shader::Glow:
            case Shader::LensDrops:
            case Shader::Occlusion:
            case Shader::Shafts:
                return { }; // Screen-wide effects the software backend leaves out (scene/park/scene.cpp)
        }
        return { };
    }

} // namespace rct3::render::software
