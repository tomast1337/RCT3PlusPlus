// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::LitCrossing and LitCrossingCutout: the Lit fragment stage (lit.slang) behind a vertex stage
// for an object drawn at two levels of detail while the eye's distance runs through its band. Positions and
// normals are in world space.
import common;

[[vk::push_constant]] ConstantBuffer<Transform> u_draw;

struct Corner
{
    [[vk::location(0)]] float3 position;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] float2 uv;
    [[vk::location(3)]] float3 normal;
    [[vk::location(4)]] float4 band; ///< The object's anchor, and the distance its crossing starts at
    /// The distance it ends at, and 1 on the coarser level, -1 on the finer; 2 and -2 tint them to show them.
    [[vk::location(5)]] float2 side;
}

[shader("vertex")]
LitVaryings vertex_main(Corner c)
{
    LitVaryings result = { mul(u_draw.transform, float4(c.position, 1.0)), c.color, c.uv, c.normal, c.position, float2(0.0), eye_of(u_draw.transform) };
    // A view without perspective draws both levels whole; side 0 draws the object whole.
    if (result.eye.w < 0.5)
        return result;
    const float through = clamp((distance(result.eye.xyz, c.band.xyz) - c.band.w) / max(c.side.x - c.band.w, 1e-3), 0.0, 1.0);
    result.cross = float2(through, c.side.y);
    return result;
}
