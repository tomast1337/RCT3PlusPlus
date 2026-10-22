// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Textured. A Float2 position arrives with z = 0. Texture and sampler are separate sets so one
// texture can be drawn with any sampler.
import common;

[[vk::push_constant]] ConstantBuffer<Transform> u_draw;
[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;

[shader("vertex")]
TexturedVaryings vertex_main([[vk::location(0)]] float3 position, [[vk::location(1)]] float4 color, [[vk::location(2)]] float2 uv)
{
    return { mul(u_draw.transform, float4(position, 1.0)), color, uv };
}

[shader("fragment")]
float4 fragment_main(TexturedVaryings v) : SV_Target
{
    return u_texture.Sample(u_sampler, v.uv) * v.color;
}
