// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Shape: a quad round one shape of ui/shape.hpp, its measures on every corner. The fragment
// stage follows shade_shape() in ui/shape.cpp step for step; change both together.
import common;

[[vk::push_constant]] ConstantBuffer<Transform> u_draw;

struct Corner
{
    [[vk::location(0)]] float2 position;
    [[vk::location(1)]] float4 local; ///< From the shape's middle; then its half width and half height
    [[vk::location(2)]] float4 radii;
    [[vk::location(3)]] float4 edge; ///< Softness, outline width, light width, ring width
    [[vk::location(4)]] float4 top;
    [[vk::location(5)]] float4 bottom;
    [[vk::location(6)]] float4 outline;
    [[vk::location(7)]] float4 light;
    [[vk::location(8)]] float4 cut;  ///< Centre of the disc cut out, from the middle; its radius
    [[vk::location(9)]] float2 fill; ///< Shares of the height where the fill starts and stops changing
}

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float4 local;
    [[vk::location(1)]] nointerpolation float4 radii;
    [[vk::location(2)]] nointerpolation float4 edge;
    [[vk::location(3)]] nointerpolation float4 top;
    [[vk::location(4)]] nointerpolation float4 bottom;
    [[vk::location(5)]] nointerpolation float4 outline;
    [[vk::location(6)]] nointerpolation float4 light;
    [[vk::location(7)]] nointerpolation float4 cut;
    [[vk::location(8)]] nointerpolation float2 fill;
}

[shader("vertex")]
Varyings vertex_main(Corner c)
{
    return { mul(u_draw.transform, float4(c.position, 0.0, 1.0)), c.local, c.radii, c.edge, c.top, c.bottom, c.outline, c.light, c.cut, c.fill };
}

float side(float v)
{
    return v < 0.0 ? -1.0 : 1.0;
}

float corner_radius(float4 radii, float2 p)
{
    if (p.y < 0.0)
        return p.x < 0.0 ? radii.x : radii.y;
    return p.x < 0.0 ? radii.w : radii.z;
}

float box_distance(float4 radii, float2 p, float2 half_size)
{
    const float r = corner_radius(radii, p);
    const float2 q = abs(p) - half_size + r;
    return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float facing_up(float4 radii, float2 p, float2 half_size, float ring)
{
    const float r = corner_radius(radii, p);
    const float2 q = abs(p) - half_size + r;
    float ny = 0.0;
    if (q.x > 0.0 && q.y > 0.0)
        ny = side(p.y) * q.y / length(q);
    else if (q.y >= q.x)
        ny = side(p.y);
    if (ring > 0.0 && box_distance(radii, p, half_size) < -ring / 2.0)
        ny = -ny;
    return clamp(-ny, 0.0, 1.0);
}

float4 over(float4 under, float4 top, float amount)
{
    const float a = top.a * amount;
    const float result = a + under.a * (1.0 - a);
    if (result <= 0.0)
        return float4(0.0);
    return float4((top.rgb * a + under.rgb * under.a * (1.0 - a)) / result, result);
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    const float2 p = v.local.xy;
    const float2 half_size = v.local.zw;
    const float softness = v.edge.x;
    const float outline_width = v.edge.y;
    const float light_width = v.edge.z;
    const float ring = v.edge.w;

    float d = box_distance(v.radii, p, half_size);
    if (ring > 0.0)
        d = abs(d + ring / 2.0) - ring / 2.0;
    if (v.cut.z > 0.0)
        d = max(d, v.cut.z - length(p - v.cut.xy));
    const float t = clamp(0.5 - d / max(softness, 1.0), 0.0, 1.0);
    const float coverage = softness > 1.0 ? t * t * (3.0 - 2.0 * t) : t;
    if (coverage <= 0.0)
        discard;

    const float share = half_size.y > 0.0 ? (p.y + half_size.y) / (2.0 * half_size.y) : 0.0;
    const float span = v.fill.y - v.fill.x;
    const float along = clamp(span > 0.0 ? (share - v.fill.x) / span : (share < v.fill.x ? 0.0 : 1.0), 0.0, 1.0);
    float4 color = lerp(v.top, v.bottom, along);
    const float inside = -d;
    if (light_width > 0.0)
        color = over(color, v.light, clamp(1.0 - inside / light_width, 0.0, 1.0) * facing_up(v.radii, p, half_size, ring));
    if (outline_width > 0.0)
        color = over(color, v.outline, clamp(outline_width - inside + 0.5, 0.0, 1.0));
    color.a *= coverage;
    return color;
}
