// SPDX-License-Identifier: GPL-3.0-or-later
//
// What several shader programs share. Each program is one file whose entry points are `vertex_main` and
// `fragment_main`; this module has none. Matrices are column-major in memory, as render/ fills them, and
// `M[row][column]` reads them as written on paper.
module common;

/// The one transform most programs are drawn with.
public struct Transform
{
    public float4x4 transform;
}

public float2 size_of(Texture2D texture)
{
    uint width;
    uint height;
    texture.GetDimensions(width, height);
    return float2(width, height);
}

public int2 texels_of(Texture2D texture)
{
    return int2(size_of(texture));
}

/// Where the eye is, w 1: the point the transform sends to clip x = y = w = 0, where the planes of those three
/// rows meet. A view without perspective has no such point (its w row is flat): w 0.
public float4 eye_of(float4x4 m)
{
    const float4 x = m[0];
    const float4 y = m[1];
    const float4 w = m[3];
    const float det = dot(x.xyz, cross(y.xyz, w.xyz));
    if (abs(det) < 1e-30)
        return float4(0.0);
    return float4(-(x.w * cross(y.xyz, w.xyz) + y.w * cross(w.xyz, x.xyz) + w.w * cross(x.xyz, y.xyz)) / det, 1.0);
}

/// A matrix sent as four texels, one a column.
public float4x4 from_columns(float4 c0, float4 c1, float4 c2, float4 c3)
{
    return transpose(float4x4(c0, c1, c2, c3));
}

/// Between the Textured vertex stage and the fragment stages drawn with it.
public struct TexturedVaryings
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float4 color;
    [[vk::location(1)]] public float2 uv;
}

/// Between the Lit vertex stages and the Lit fragment stage.
public struct LitVaryings
{
    public float4 position : SV_Position;
    [[vk::location(0)]] public float4 color;
    [[vk::location(1)]] public float2 uv;
    [[vk::location(2)]] public float3 normal;
    [[vk::location(3)]] public float3 world;
    /// How far an object crossing between two levels of detail is through its band, and its side: 1 the coarser
    /// level, -1 the finer, 0 not crossing; 2 and -2 tint the coarser red and the finer blue (lit_crossing.slang).
    [[vk::location(4)]] public nointerpolation float2 cross;
    /// Where the eye is, w 1; w 0 in a view without perspective, which has no such point.
    [[vk::location(5)]] public nointerpolation float4 eye;
}
