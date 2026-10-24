// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Lit, and LitCutout with the constant set: then pixels under half opacity are not drawn.
// Positions and normals are in world space.
import common;

[[vk::constant_id(0)]] const bool kCutout = false;

[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;
// render::SceneLight's shadow: the depth seen from the sun, and how it was seen (render/types.hpp).
[[vk::binding(0, 2)]] Texture2D u_shadow;
[[vk::binding(0, 3)]] Texture2D u_sun_view;
// Tiling noise read over world x and z (park/ground_variation.hpp), when sky.w is 1.
[[vk::binding(0, 4)]] Texture2D u_variation;
// An older drawing of the shadow, mixed in as sun_direction.w says; the second row of u_sun_view says how.
[[vk::binding(0, 5)]] Texture2D u_shadow_before;
// render::SceneLight's lamps, when sky.w says they are given.
[[vk::binding(0, 6)]] Texture2D u_lamps;
// The texture's relief map (park/relief_maps.hpp), when sky.w says it is given.
[[vk::binding(0, 7)]] Texture2D u_relief;

// render::SceneLight, packed. sun_direction.w is 0 without a shadow, else 1 plus the older shadow's weight; sky.w
// is 1 when the variation is given, plus 2 when the relief map is, plus 4 times the surface detail (0 to 2),
// plus 16 when the lamps are, plus as its fraction how far the texture is moved along v (a lift's chain); sun.w is the haze rate and ground.w the haze colour, 8 bits a channel, red highest.
struct Draw
{
    float4x4 transform;
    float4 sun_direction;
    float4 sun;
    float4 sky;
    float4 ground;
}
[[vk::push_constant]] ConstantBuffer<Draw> u_draw;

[shader("vertex")]
LitVaryings vertex_main([[vk::location(0)]] float3 position, [[vk::location(1)]] float4 color, [[vk::location(2)]] float2 uv, [[vk::location(3)]] float3 normal)
{
    // Not crossing levels (lit_crossing.slang).
    return { mul(u_draw.transform, float4(position, 1.0)), color, uv + float2(0.0, frac(u_draw.sky.w)), normal, position, float2(0.0), eye_of(u_draw.transform) };
}

// How much of the 2 by 2 texels round `at` (in texels, from the map's corner) lies nearer the sun than `depth`,
// blended by where `at` falls between them, so edges move smoothly rather than a texel at a time.
float lit_at(Texture2D shadow, float2 at, float2 size, float depth)
{
    const float2 corner = floor(at - 0.5);
    const float4 stored = shadow.Gather(u_sampler, (corner + 1.0) / size);
    const float4 lit = step(float4(depth), stored); // Gathered as (0,1) (1,1) (1,0) (0,0)
    const float2 f = at - 0.5 - corner;
    return lerp(lerp(lit.w, lit.z, f.x), lerp(lit.x, lit.y, f.x), f.y);
}

float4 sun_view_texel(int column, int row)
{
    return u_sun_view.Load(int3(column, row, 0));
}

// 1 in full sunlight, 0 where something nearer the sun covers this point: an average over 3 by 3 texels, so the
// edge is soft over about one texel either side. Fades out towards the map's edge, beyond which all is lit.
float sunlit(Texture2D shadow, int row, float3 world, float3 n)
{
    const float4x4 view = from_columns(sun_view_texel(0, row), sun_view_texel(1, row), sun_view_texel(2, row), sun_view_texel(3, row));
    const float4 extra = sun_view_texel(4, row);
    // Pushed off the surface along its normal, and compared with more slack the more the surface leans away from
    // the sun, so it does not shadow itself: a leaning surface spans several depths within one texel.
    const float facing = clamp(dot(n, u_draw.sun_direction.xyz), 0.05, 1.0);
    const float lean = min(sqrt(1.0 - facing * facing) / facing, 6.0);
    const float4 clip = mul(view, float4(world + n * extra.x * (1.0 + lean), 1.0));
    const float3 p = clip.xyz / clip.w;
    const float edge = max(abs(p.x), abs(p.y));
    if (edge >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
        return 1.0;
    const float2 size = size_of(shadow);
    const float2 at = float2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * size;
    const float depth = p.z - extra.y * (1.0 + 2.0 * lean);
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += lit_at(shadow, at + float2(x, y), size, depth);
    return lerp(sum / 9.0, 1.0, smoothstep(0.85, 1.0, edge));
}

// park/lamp_grid.hpp: the texture's width, and the most lamps a cell lists.
static const int kLampTexelsAcross = 256;
static const int kLampsPerCell = 16;

float4 lamp_texel(int i)
{
    return u_lamps.Load(int3(i % kLampTexelsAcross, i / kLampTexelsAcross, 0));
}

// The light of the lamps whose reach covers this point: each fades smoothly to nothing at its reach, and lights
// a little round the side facing away, as light thrown back by what is round it would.
float3 lamplight(float3 world, float3 n)
{
    const float4 grid = lamp_texel(0);
    const float4 more = lamp_texel(1);
    const int2 cell = int2(floor((world.xz - grid.xy) / grid.z));
    if (any(cell < 0) || cell.x >= int(grid.w) || cell.y >= int(more.x))
        return float3(0.0);
    const int start = int(more.z) + (cell.y * int(grid.w) + cell.x) * (kLampsPerCell / 4);
    float3 sum = float3(0.0);
    for (int t = 0; t < kLampsPerCell / 4; ++t)
    {
        const float4 four = lamp_texel(start + t);
        for (int k = 0; k < 4; ++k)
        {
            if (four[k] < 0.0)
                return sum;
            const int lamp = int(four[k]);
            const float4 place = lamp_texel(2 + 2 * lamp);
            const float3 to = place.xyz - world;
            const float d2 = dot(to, to) / (place.w * place.w);
            if (d2 >= 1.0)
                continue;
            const float fall = (1.0 - d2) * (1.0 - d2);
            const float facing = clamp((dot(n, to * rsqrt(max(dot(to, to), 1e-6))) + 0.3) / 1.3, 0.0, 1.0);
            sum += lamp_texel(3 + 2 * lamp).rgb * fall * facing;
        }
    }
    return sum;
}

// How much lighter or darker the ground is here: three sizes of the noise, widest strongest, so no one size
// repeats visibly. Features about 240, 100 and 40 world units across.
static const float kVariation = 0.15;
float variation(float3 world)
{
    // One tile of the picture holds 8 features.
    float n = 0.5 * u_variation.Sample(u_sampler, world.xz / 1920.0).r;
    n += 0.3 * u_variation.Sample(u_sampler, world.xz / 800.0 + 0.37).r;
    n += 0.2 * u_variation.Sample(u_sampler, world.xz / 320.0 + 0.71).r;
    return 1.0 + kVariation * (2.0 * n - 1.0);
}

// How far the relief map's lean is followed, and how deep its lowest point lies under its highest,
// in texels, when the texture is shifted by depth.
static const float kRelief = 0.7;
static const float kDepth = 1.5;
// The relief is whole where one texel spans kReliefNear pixels or more, and gone where it spans
// kReliefFar or fewer.
static const float kReliefNear = 1.0;
static const float kReliefFar = 0.5;

// How the world point and the texture coordinate change to the next pixel across and up. Taken once at the
// top of fragment_main(): changes are only defined where all four pixels of a 2 by 2 block run the same code.
static float3 dp1;
static float3 dp2;
static float2 duv1;
static float2 duv2;

// The ways the texture's u and v run along the surface at this pixel, unit length and square to `n`, from how
// they change across the screen, so meshes need no tangents. False where the texture does not change.
bool texture_frame(float3 n, out float3 u, out float3 v)
{
    const float3 dp2perp = cross(dp2, n);
    const float3 dp1perp = cross(n, dp1);
    u = dp2perp * duv1.x + dp1perp * duv2.x;
    v = dp2perp * duv1.y + dp1perp * duv2.y;
    u -= n * dot(n, u);
    v -= n * dot(n, v);
    if (dot(u, u) < 1e-30 || dot(v, v) < 1e-30)
        return false;
    u = normalize(u);
    v = normalize(v);
    return true;
}

// Where along the texture the eye meets the relief: a raised point is seen nearer the eye than where the ray
// meets the flat surface, by its height over how steeply the surface is seen. One step, so steep views smear
// rather than show the relief's sides. Unchanged in a view without perspective.
float2 shifted(LitVaryings f, float3 n, float3 u, float3 v)
{
    if (f.eye.w < 0.5)
        return f.uv;
    const float3 to_eye = normalize(f.eye.xyz - f.world);
    const float facing = max(abs(dot(to_eye, n)), 0.3);
    const float2 size = size_of(u_relief);
    const float height = u_relief.SampleGrad(u_sampler, f.uv, duv1, duv2).b - 0.5;
    return f.uv + float2(dot(to_eye, u), dot(to_eye, v)) / facing * height * kDepth / size;
}

// How short and soft the relief's own shadows are, and the sun's glint on bumps.
static const float kReliefSteps[2] = { 1.5, 4.0 }; // Texels towards the sun the relief is looked at
static const float kReliefShadowEdge = 2.0;        // How quickly the shade grows as a bump rises over the sun's ray
static const float kSheen = 0.05;
static const float kGlint = 24.0;

// How much of the sun reaches `uv` past the relief round it: a few steps along the texture towards the sun,
// each height compared with how high the ray to the sun has climbed there.
float relief_sunlit(float2 uv, float3 n, float3 u, float3 v, float height)
{
    const float3 sun = u_draw.sun_direction.xyz;
    const float up = dot(sun, n);
    const float2 across = float2(dot(sun, u), dot(sun, v));
    const float run = length(across);
    if (up <= 0.0 || run < 1e-4)
        return 1.0;
    const float2 texel = across / run / size_of(u_relief);
    const float climb = up / run / kDepth; // In the map's height, per texel
    float over = 0.0;
    for (int i = 0; i < 2; ++i)
    {
        const float d = kReliefSteps[i];
        const float there = u_relief.SampleGrad(u_sampler, uv + texel * d, duv1, duv2).b;
        over = max(over, there - height - climb * d);
    }
    return 1.0 - clamp(over * kReliefShadowEdge, 0.0, 1.0);
}

// A number from 0 to 1 for each whole point of `p`.
float hash(float2 p)
{
    const uint2 q = uint2(int2(floor(p)));
    uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
    h = (h ^ (h >> 16)) * 2246822519u;
    return float(h ^ (h >> 13)) / 4294967295.0;
}

// Smooth noise from 0 to 1 with features a unit of `p` across.
float value_noise(float2 p)
{
    float2 f = frac(p);
    f = f * f * (3.0 - 2.0 * f);
    return lerp(lerp(hash(p), hash(p + float2(1.0, 0.0)), f.x), lerp(hash(p + float2(0.0, 1.0)), hash(p + float2(1.0, 1.0)), f.x), f.y);
}

// How much lighter or darker the grain makes the colour at most.
static const float kGrain = 0.08;

// Fine grain, three to a texel, where a texel spans several pixels and the texture alone would look blurred; it
// fades out as the grains shrink to a pixel, so it does not shimmer far away.
// shortcut: one kind of grain for every texture; noise kinds per material (fabric, rock, wood) later.
float grain(float2 uv)
{
    const float2 p = uv * size_of(u_texture) * 3.0;
    const float spread = max(length(ddx(p)), length(ddy(p))); // Grains across one pixel
    const float shown = 1.0 - smoothstep(0.4, 1.2, spread);
    const float n = 0.65 * value_noise(p) + 0.35 * value_noise(p * 2.3 + 17.0);
    return 1.0 + kGrain * shown * (2.0 * n - 1.0);
}

// A 4 by 4 Bayer pattern: each pixel's place, in sixteenths, in the order a square's pixels fill as a fade grows.
static const float kDither[16] = { 1.0, 9.0, 3.0, 11.0, 13.0, 5.0, 15.0, 7.0, 4.0, 12.0, 2.0, 10.0, 16.0, 8.0, 14.0, 6.0 };

// Whether a crossing object leaves this pixel to its other level: the coarser level draws the pixels whose place
// is within how far through the band it is, the finer the rest, so between them each pixel is drawn once.
bool crossed_out(LitVaryings f)
{
    if (f.cross.y == 0.0)
        return false;
    const int2 p = int2(f.position.xy) & 3;
    const bool coarser = kDither[p.y * 4 + p.x] / 16.0 <= f.cross.x;
    return coarser != (f.cross.y > 0.0);
}

// Light past what the screen can show eased off rather than clipped: unchanged up to kKnee, then bending
// towards 1 with no corner, all channels scaled alike so the hue holds. Lamps at night add to whatever light is
// already there, which would otherwise flatten into blots of one colour.
static const float kKnee = 0.9;
float3 eased_off(float3 c)
{
    const float top = max(max(c.r, c.g), c.b);
    if (top <= kKnee)
        return c;
    const float eased = kKnee + (1.0 - kKnee) * (1.0 - exp((kKnee - top) / (1.0 - kKnee)));
    return c * (eased / top);
}

[shader("fragment")]
float4 fragment_main(LitVaryings f) : SV_Target
{
    if (crossed_out(f))
        discard;
    // Lit as the normal says, from whichever side the surface is seen. Turning the normal to face the eye would
    // darken everything above the eye whose normal tilts up (roofs, the cards of trees, which are seen from below
    // there) along a hard line at the horizon. Without a normal, face up.
    float3 n = float3(0.0, 1.0, 0.0);
    if (dot(f.normal, f.normal) > 1e-12)
        n = normalize(f.normal);
    dp1 = ddx(f.world);
    dp2 = ddy(f.world);
    duv1 = ddx(f.uv);
    duv2 = ddy(f.uv);

    // The flags are small whole numbers, so their bits can be read straight.
    const uint flags = uint(u_draw.sky.w);
    const int detail = int((flags >> 2) & 3u);
    const bool variation_given = (flags & 1u) != 0u;
    const bool relief_given = (flags & 2u) != 0u;
    float3 along_u = float3(0.0);
    float3 along_v = float3(0.0);
    // The relief shows only near the eye, where a texel spans more than a pixel; it fades in over the band and
    // costs nothing beyond it, where most of the view is.
    float near = 0.0;
    if (detail >= 2 && relief_given)
    {
        const float2 size = size_of(u_texture);
        const float pixels = 1.0 / max(max(length(duv1 * size), length(duv2 * size)), 1e-6); // Pixels one texel spans
        near = smoothstep(kReliefFar, kReliefNear, pixels);
    }
    const bool framed = near > 0.0 && texture_frame(n, along_u, along_v);
    const float2 uv = framed ? lerp(f.uv, shifted(f, n, along_u, along_v), near) : f.uv;
    // Read with the unshifted coordinate's changes, so the shift does not change the mip level.
    float4 color = (detail >= 2 ? u_texture.SampleGrad(u_sampler, uv, duv1, duv2) : u_texture.Sample(u_sampler, f.uv)) * f.color;
    if (abs(f.cross.y) > 1.5)
        color.rgb = lerp(color.rgb, f.cross.y > 0.0 ? float3(1.0, 0.0, 0.0) : float3(0.0, 0.0, 1.0), 0.6);
    if (variation_given)
        color.rgb *= variation(f.world);
    if (detail >= 1)
        color.rgb *= grain(f.uv);
    if (kCutout)
    {
        if (color.a < 0.5)
            discard;
        color.a = 1.0;
    }

    // The shadow is looked up by the true normal below; only the light follows the relief.
    float3 bumped = n;
    float open = 1.0;       // How much of the sky reaches into the relief here
    float relief_lit = 1.0; // How much of the sun gets past the relief round here
    if (framed)
    {
        const float4 relief = u_relief.SampleGrad(u_sampler, uv, duv1, duv2);
        const float2 lean = (relief.rg * 2.0 - 1.0) * kRelief * near;
        bumped = normalize(n * sqrt(max(1.0 - dot(lean, lean), 0.0)) + along_u * lean.x + along_v * lean.y);
        open = lerp(1.0, relief.a, near);
        relief_lit = lerp(1.0, relief_sunlit(uv, n, along_u, along_v, relief.b), near);
    }
    float3 ambient = lerp(u_draw.ground.rgb, u_draw.sky.rgb, bumped.y * 0.5 + 0.5) * open;
    float3 direct = u_draw.sun.rgb * max(dot(bumped, u_draw.sun_direction.xyz), 0.0) * relief_lit;
    float sun_reaches = relief_lit;
    // In shadow the sky still lights a surface, but less of it: part of the sky is behind what casts the shadow.
    if (u_draw.sun_direction.w > 0.5 && dot(direct, direct) > 0.0)
    {
        const float before = u_draw.sun_direction.w - 1.0;
        float lit = sunlit(u_shadow, 0, f.world, n);
        if (before > 0.002)
            lit = lerp(lit, sunlit(u_shadow_before, 1, f.world, n), before);
        direct *= lit;
        ambient *= lerp(0.7, 1.0, lit);
        sun_reaches *= lit;
    }
    float3 shaded = color.rgb * (ambient + direct);
    // The sun's glint off the relief's slopes that face between it and the eye.
    if (framed && f.eye.w > 0.5)
    {
        const float3 half_way = normalize(u_draw.sun_direction.xyz + normalize(f.eye.xyz - f.world));
        shaded += u_draw.sun.rgb * kSheen * near * pow(max(dot(bumped, half_way), 0.0), kGlint) * sun_reaches * open;
    }
    if ((flags & 16u) != 0u)
        shaded += color.rgb * lamplight(f.world, n);
    shaded = eased_off(shaded);
    // Haze: beyond its start, far surfaces fade towards the haze colour. The distance is the depth from the eye,
    // which the perspective divide leaves in the position's w.
    if (u_draw.sun.w > 0.0)
    {
        static const float kHazeStart = 150.0;
        const float rate = u_draw.sun.w;
        const uint packed = uint(u_draw.ground.w);
        const float3 haze = float3((packed >> 16) & 255u, (packed >> 8) & 255u, packed & 255u) / 255.0;
        const float far = 1.0 - exp(-rate * max(1.0 / f.position.w - kHazeStart, 0.0));
        shaded = lerp(shaded, haze, far);
    }
    return float4(shaded, color.a);
}
