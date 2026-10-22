// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Glow, one step of the glow chain (app/scene/park_glow.hpp), drawn with the Textured vertex
// stage. With `color.a` at 1 it shrinks: thirteen filtered reads round the point, weighted so every source
// pixel counts once, and keeps the part above `color.g` with a soft knee. With `color.a` at 0 it grows: a three
// by three tent of reads `color.r` texels apart. Either way the result is times `color.b`. The weights follow
// the downsample and upsample filters Jorge Jimenez described for Call of Duty: Advanced Warfare (SIGGRAPH 2014).
import common;

[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;

float3 at(float2 uv, float2 offset)
{
    return u_texture.Sample(u_sampler, uv + offset / size_of(u_texture)).rgb;
}

float3 shrink(float2 uv)
{
    // Each read already averages four pixels: five overlapping boxes of four reads.
    const float3 inner = at(uv, float2(-1, -1)) + at(uv, float2(1, -1)) + at(uv, float2(-1, 1)) + at(uv, float2(1, 1));
    const float3 corners = at(uv, float2(-2, -2)) + at(uv, float2(2, -2)) + at(uv, float2(-2, 2)) + at(uv, float2(2, 2));
    const float3 sides = at(uv, float2(-2, 0)) + at(uv, float2(2, 0)) + at(uv, float2(0, -2)) + at(uv, float2(0, 2));
    return inner * 0.125 + corners * 0.03125 + sides * 0.0625 + at(uv, float2(0)) * 0.125;
}

float3 grow(float2 uv, float reach)
{
    float3 sum = at(uv, float2(0)) * 4.0;
    sum += (at(uv, float2(-reach, 0)) + at(uv, float2(reach, 0)) + at(uv, float2(0, -reach)) + at(uv, float2(0, reach))) * 2.0;
    sum += at(uv, float2(-reach, -reach)) + at(uv, float2(reach, -reach)) + at(uv, float2(-reach, reach)) + at(uv, float2(reach, reach));
    return sum / 16.0;
}

[shader("fragment")]
float4 fragment_main(TexturedVaryings v) : SV_Target
{
    float3 colour;
    if (v.color.a > 0.5)
    {
        colour = shrink(v.uv);
        const float threshold = v.color.g;
        if (threshold > 0.0)
        {
            // Above the threshold in full, eased in over a knee below it so the glow does not switch on hard.
            const float knee = 0.5 * threshold;
            const float bright = max(colour.r, max(colour.g, colour.b));
            float eased = clamp(bright - threshold + knee, 0.0, 2.0 * knee);
            eased = eased * eased / (4.0 * knee + 1e-4);
            colour *= max(eased, bright - threshold) / max(bright, 1e-4);
        }
    }
    else
    {
        colour = grow(v.uv, v.color.r);
    }
    return float4(colour * v.color.b, 1.0);
}
