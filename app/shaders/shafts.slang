// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Shafts, sunlight seen in the air (app/scene/park_glow.hpp). Each pixel steps along the line
// from the eye to what it shows and asks the sun's shadow map, at each step, whether that bit of air is in
// sunlight. The lit length of the line, scattered towards the eye, comes out as haze in the sun's colour, so the
// shadow of anything between the sun and the air leaves a dark gap in it: the shafts. This is the shadow map ray
// march of volumetric lighting (as in Toth and Umenhoffer, "Real-time volumetric lighting in participating media",
// Eurographics 2009), scattered with the Henyey-Greenstein phase function (1941).
import common;

// The draw's texture is not read; the shader reads only the extra textures.
[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;
[[vk::binding(0, 2)]] Texture2D u_depth;
[[vk::binding(0, 3)]] Texture2D u_shadow;
[[vk::binding(0, 4)]] Texture2D u_sun_view;

// render::SceneLight, packed as for the Lit shaders (lit.slang); only the sun is read here. The transform takes
// the screen's clip space to the world.
struct Draw
{
    float4x4 transform;
    float4 sun_direction;
    float4 sun;
    float4 sky;
    float4 ground;
}
[[vk::push_constant]] ConstantBuffer<Draw> u_draw;

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 uv;
    /// The strength, then how far along the line the air is read, in world units.
    [[vk::location(1)]] nointerpolation float4 setting;
}

[shader("vertex")]
Varyings vertex_main([[vk::location(0)]] float2 position, [[vk::location(1)]] float4 color)
{
    // Clip Y points up but texture rows run down from the top.
    return { float4(position, 0.0, 1.0), float2(position.x * 0.5 + 0.5, 0.5 - position.y * 0.5), color };
}

// Reads along each line, and how strongly the air scatters light forwards (0 the same every way,
// towards 1 all of it straight on), which brightens the shafts looking towards the sun.
static const int kSteps = 24;
static const float kForward = 0.6;
// Each pixel's place in a 4 by 4 square, in sixteenths, in an order that spreads neighbours apart.
static const float kBayer[16] = { 0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0 };

float3 world_at(float2 uv, float stored)
{
    const float4 p = mul(u_draw.transform, float4(uv.x * 2.0 - 1.0, 1.0 - uv.y * 2.0, stored, 1.0));
    return p.xyz / p.w;
}

// 1 where the sun reaches `world`, 0 where the shadow map holds something nearer the sun; lit beyond the map.
float sunlit(float3 world, float4x4 view, float bias)
{
    const float4 clip = mul(view, float4(world, 1.0));
    const float3 p = clip.xyz / clip.w;
    if (max(abs(p.x), abs(p.y)) >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
        return 1.0;
    const int2 at = int2(float2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * size_of(u_shadow));
    return u_shadow.Load(int3(at, 0)).r >= p.z - bias ? 1.0 : 0.0;
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    const float strength = v.setting.x;
    const float reach = v.setting.y;
    const float4x4 view = from_columns(u_sun_view.Load(int3(0, 0, 0)), u_sun_view.Load(int3(1, 0, 0)), u_sun_view.Load(int3(2, 0, 0)), u_sun_view.Load(int3(3, 0, 0)));
    const float bias = u_sun_view.Load(int3(4, 0, 0)).y;

    // From the near plane to what the pixel shows, or `reach` into the sky.
    const float stored = u_depth.Load(int3(int2(v.uv * size_of(u_depth)), 0)).r;
    const float3 start = world_at(v.uv, 0.0);
    const float3 ahead = normalize(world_at(v.uv, 0.5) - start);
    const float span = stored >= 1.0 ? reach : min(distance(world_at(v.uv, stored), start), reach);
    const float stride = span / float(kSteps);
    // A different start on each pixel of a 4 by 4 square, so the steps blur together instead of banding.
    const int2 cell = int2(v.position.xy) & 3;
    float t = stride * (kBayer[cell.y * 4 + cell.x] + 0.5) / 16.0;
    float lit = 0.0;
    for (int i = 0; i < kSteps; ++i)
    {
        lit += sunlit(start + ahead * t, view, bias);
        t += stride;
    }
    // The lit share of the air, thickening towards 1 over about `reach` of it.
    const float air = 1.0 - exp(-lit * stride / reach);
    // Henyey-Greenstein, times 4 pi so air scattering evenly every way would give 1.
    const float g = kForward;
    const float phase = (1.0 - g * g) / pow(1.0 + g * g - 2.0 * g * dot(ahead, normalize(u_draw.sun_direction.xyz)), 1.5);
    return float4(u_draw.sun.rgb * air * phase * strength, 1.0);
}
