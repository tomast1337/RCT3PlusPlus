// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::LensDrops, water on the camera's lens (app/scene/park_lens.hpp), drawn with the Textured vertex
// stage over a copy of the screen. Each drop is a quad whose uv runs -1..1 across it; colour red is how much of
// it shows, green 1 for a sheet of water, and blue and alpha the drop's middle on screen (0..1, y down). A drop is
// a little lens: it shows the scene round it shrunk and upside down, out of focus, darker at its rim and with a
// glint of the sky near its top. A sheet only bends and blurs what is behind it a little.
import common;

[[vk::binding(0, 0)]] Texture2D u_screen;
[[vk::binding(0, 1)]] SamplerState u_sampler;

float3 blurred(float2 uv, float reach)
{
    const float2 step = reach / size_of(u_screen);
    float3 sum = u_screen.Sample(u_sampler, uv + float2(-step.x, -step.y)).rgb;
    sum += u_screen.Sample(u_sampler, uv + float2(step.x, -step.y)).rgb;
    sum += u_screen.Sample(u_sampler, uv + float2(-step.x, step.y)).rgb;
    sum += u_screen.Sample(u_sampler, uv + float2(step.x, step.y)).rgb;
    return sum * 0.25;
}

[shader("fragment")]
float4 fragment_main(TexturedVaryings v) : SV_Target
{
    const float d = length(v.uv);
    if (d >= 1.0)
        discard;
    const float2 here = v.position.xy / size_of(u_screen);
    const float2 middle = v.color.ba;
    if (v.color.g > 0.5)
    {
        // A sheet: a soft-edged film that bends the scene a little towards its middle.
        const float2 bent = here + (middle - here) * 0.04 * (1.0 - d);
        const float3 colour = blurred(bent, 3.0) * 1.03;
        return float4(colour, v.color.r * 0.55 * (1.0 - smoothstep(0.35, 1.0, d)));
    }
    // A drop: the dome's height over the lens, and from it how far the light bends.
    const float dome = sqrt(1.0 - d * d);
    const float2 seen = middle - (here - middle) * 2.5;
    float3 colour = blurred(seen, 1.5);
    colour *= lerp(0.55, 1.0, dome);
    const float glint = smoothstep(0.75, 1.0, 1.0 - length(v.uv - float2(-0.3, -0.4)) * 2.2);
    colour += glint * 0.45;
    return float4(colour, v.color.r * (1.0 - smoothstep(0.85, 1.0, d)));
}
