// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Occlusion, one step a draw (render::OcclusionStep): trace, blur, blend, apply. Positions come
// in clip space; the screen is read where the point falls. Clip Y points up but texture rows run down from the
// top, so the texture's v is flipped.
import common;

[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;
[[vk::binding(0, 2)]] Texture2D u_extra0;
[[vk::binding(0, 3)]] Texture2D u_extra1;
[[vk::binding(0, 4)]] Texture2D u_extra2;

// a: step, frame, radius, strength; b: the last frame's share, unused, then the depth terms.
struct Draw
{
    float4x4 transform;
    float4 a;
    float4 b;
}
[[vk::push_constant]] ConstantBuffer<Draw> u_draw;

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
}

[shader("vertex")]
Varyings vertex_main([[vk::location(0)]] float2 position)
{
    return { float4(position, 0.0, 1.0), float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5) };
}

static const float kPi = 3.14159265;
// Directions and steps per side traced, the most pixels a trace reaches, and how far depths
// may differ, as a share of the depth, and still count as one surface when blurring and blending.
static const int kSlices = 2;
static const int kSteps = 4;
static const float kMostPixels = 96.0;
static const float kSameSurface = 0.08;
// Each pixel's place in a 4 by 4 square, in sixteenths, in an order that spreads neighbours apart.
static const float kBayer[16] = { 0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0 };

float stored_depth(Texture2D t, float2 uv)
{
    return t.SampleLevel(u_sampler, uv, 0.0).r;
}

// Depth from the eye, from the depth buffer's value.
float distance_of(float stored)
{
    return u_draw.b.w / (stored - u_draw.b.z);
}

// Clip space x and y of texture place `uv`, and back: texture v runs down, clip Y up.
float2 clip_of(float2 uv)
{
    return float2(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0);
}

float2 uv_of(float2 clip)
{
    return float2(clip.x * 0.5 + 0.5, 0.5 - clip.y * 0.5);
}

// The point seen at `uv`, in eye space.
float3 eye_point(float2 uv, float stored)
{
    const float4 p = mul(u_draw.transform, float4(clip_of(uv), stored, 1.0));
    return p.xyz / p.w;
}

float3 eye_point_at(float2 uv)
{
    return eye_point(uv, stored_depth(u_texture, uv));
}

// Ground truth ambient occlusion (Jimenez et al. 2016): in each slice of the view round the pixel, the highest
// horizon either side within the radius, and the cosine-weighted share of the half circle between them that
// the surface's normal sees.
float trace(float2 at, float4 frag)
{
    const float2 size = size_of(u_texture);
    const float2 texel = 1.0 / size;
    const float stored = stored_depth(u_texture, at);
    if (stored >= 1.0)
        return 1.0; // The sky
    const float3 p = eye_point(at, stored);
    const float3 v = normalize(-p);
    // Every read is an even number of pixels away. An object crossing between levels of detail shows them in a
    // 4 by 4 pattern (lit.slang) whose pixels an even step apart rank close together, so they nearly always show
    // the same level; reading the other level, a little off this one's surface, would shade it as a crease.
    // The normal from the neighbours on whichever side is nearer in depth, so edges do not bend it.
    const float3 right = eye_point_at(at + float2(2.0 * texel.x, 0.0)) - p;
    const float3 left = p - eye_point_at(at - float2(2.0 * texel.x, 0.0));
    const float3 up = eye_point_at(at + float2(0.0, 2.0 * texel.y)) - p;
    const float3 down = p - eye_point_at(at - float2(0.0, 2.0 * texel.y));
    const float3 across = abs(right.z) < abs(left.z) ? right : left;
    const float3 along = abs(up.z) < abs(down.z) ? up : down;
    float3 n = normalize(cross(across, along));
    if (dot(n, v) < 0.0)
        n = -n;

    // How many pixels the radius spans here: the projection's scale on y is 1 over the inverse's.
    const float radius = u_draw.a.z;
    const float pixels = min(radius / u_draw.transform[1][1] * 0.5 * size.y / p.z, kMostPixels);
    if (pixels < 2.0)
        return 1.0;
    const int2 cell = int2(frag.xy) & 3;
    const float frame = u_draw.a.y;
    const float turn = frac(kBayer[cell.y * 4 + cell.x] / 16.0 + frame * 0.6180340);
    const float shift = frac(float((cell.x + 2 * cell.y) & 3) / 4.0 + frame * 0.3819660);

    float seen = 0.0;
    for (int s = 0; s < kSlices; ++s)
    {
        const float phi = (float(s) + turn) * kPi / float(kSlices);
        const float2 omega = float2(cos(phi), sin(phi));
        const float3 dir = float3(omega, 0.0);
        const float3 axis = normalize(cross(dir, v));
        const float3 ortho = cross(v, axis);
        const float3 projected = n - axis * dot(n, axis);
        const float projected_length = length(projected);
        if (projected_length < 1e-4)
            continue;
        const float cos_n = clamp(dot(projected, v) / projected_length, -1.0, 1.0);
        const float angle_n = sign(dot(ortho, projected)) * acos(cos_n);
        float2 horizon = float2(-1.0);
        for (int side = 0; side < 2; ++side)
        {
            // The slice runs along omega in eye space, so down the texture where it rises.
            const float2 way = float2(omega.x, -omega.y) * (side == 0 ? -1.0 : 1.0) * pixels;
            for (int j = 0; j < kSteps; ++j)
            {
                // Closer together near the pixel, where what is near counts most; at least two pixels out.
                const float t = (float(j) + shift) / float(kSteps);
                const float2 off = way * max(t * t, 2.0 / pixels);
                float2 even = 2.0 * round(off * 0.5);
                if (all(even == 0.0))
                    even = abs(off.x) > abs(off.y) ? float2(2.0 * sign(off.x), 0.0) : float2(0.0, 2.0 * sign(off.y));
                const float2 uv = at + even * texel;
                const float3 delta = eye_point_at(uv) - p;
                const float reach = length(delta);
                const float rises = dot(delta, v) / max(reach, 1e-5);
                // Fades out towards the radius, so far things do not shut the point in.
                const float near = clamp(1.0 - reach * reach / (radius * radius), 0.0, 1.0);
                horizon[side] = max(horizon[side], lerp(-1.0, rises, near));
            }
        }
        const float h0 = angle_n + max(-acos(horizon[0]) - angle_n, -kPi / 2.0);
        const float h1 = angle_n + min(acos(horizon[1]) - angle_n, kPi / 2.0);
        const float sin_n = sin(angle_n);
        seen += projected_length * 0.25 * ((-cos(2.0 * h0 - angle_n) + cos_n + 2.0 * h0 * sin_n) + (-cos(2.0 * h1 - angle_n) + cos_n + 2.0 * h1 * sin_n));
    }
    return clamp(seen / float(kSlices), 0.0, 1.0);
}

// The 4 by 4 pixels the turning repeats over, those on the same surface only.
float blur(float2 at)
{
    const float2 texel = 1.0 / size_of(u_texture);
    const float centre = distance_of(stored_depth(u_extra0, at));
    float sum = 0.0;
    float weight = 0.0;
    for (int y = -2; y < 2; ++y)
        for (int x = -2; x < 2; ++x)
        {
            const float2 uv = at + (float2(x, y) + 0.5) * texel;
            const float d = distance_of(stored_depth(u_extra0, uv));
            const float w = clamp(1.0 - abs(d - centre) / (kSameSurface * centre), 0.0, 1.0);
            sum += u_texture.SampleLevel(u_sampler, uv, 0.0).r * w;
            weight += w;
        }
    return weight > 0.0 ? sum / weight : u_texture.SampleLevel(u_sampler, at, 0.0).r;
}

// This frame's result, with the last frame's where the surface seen there is the one seen here, kept within
// what the pixels round here hold, so moving things do not leave trails.
float blend(float2 at)
{
    const float2 texel = 1.0 / size_of(u_texture);
    const float now = u_texture.SampleLevel(u_sampler, at, 0.0).r;
    const float history = u_draw.b.x;
    const float stored = stored_depth(u_extra1, at);
    if (history <= 0.0 || stored >= 1.0)
        return now;
    float4 then = mul(u_draw.transform, float4(clip_of(at), stored, 1.0));
    then /= then.w;
    const float2 uv = uv_of(then.xy);
    if (any(uv < 0.0) || any(uv > 1.0))
        return now;
    const float expected = distance_of(then.z);
    if (abs(distance_of(stored_depth(u_extra2, uv)) - expected) > kSameSurface * expected)
        return now;
    float low = now;
    float high = now;
    for (int i = 0; i < 4; ++i)
    {
        const float2 o = float2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * 2.0 * texel;
        const float n = u_texture.SampleLevel(u_sampler, at + o, 0.0).r;
        low = min(low, n);
        high = max(high, n);
    }
    const float before = clamp(u_extra0.SampleLevel(u_sampler, uv, 0.0).r, low - 0.05, high + 0.05);
    return lerp(now, before, history);
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    const int step = int(u_draw.a.x + 0.5);
    if (step == 0)
        return float4(float3(trace(v.uv, v.position)), 1.0);
    if (step == 1)
        return float4(float3(blur(v.uv)), 1.0);
    if (step == 2)
        return float4(float3(blend(v.uv)), 1.0);
    return float4(0.0, 0.0, 0.0, (1.0 - u_texture.SampleLevel(u_sampler, v.uv, 0.0).r) * u_draw.a.w);
}
