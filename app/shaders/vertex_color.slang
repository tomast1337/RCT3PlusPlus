// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::VertexColor.
import common;

[[vk::push_constant]] ConstantBuffer<Transform> u_draw;

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 color;
}

[shader("vertex")]
Varyings vertex_main([[vk::location(0)]] float3 position, [[vk::location(1)]] float4 color)
{
    return { mul(u_draw.transform, float4(position, 1.0)), color };
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    return v.color;
}
