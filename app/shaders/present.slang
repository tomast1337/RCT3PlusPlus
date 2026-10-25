// SPDX-License-Identifier: GPL-3.0-or-later
//
// The drawn screen stretched over the window as the frame ends (render scale), with the brightness curve. One
// triangle covers the window; it has no vertex input. The viewport is not flipped: clip y -1 is the top.

struct Present
{
    float4 curve; ///< x: the power each channel is raised to
};

[[vk::push_constant]] ConstantBuffer<Present> u_present;
[[vk::binding(0, 0)]] Texture2D u_screen;
[[vk::binding(0, 1)]] SamplerState u_sampler;

struct PresentVaryings
{
    float4 position : SV_Position;
    float2 uv;
};

[shader("vertex")]
PresentVaryings vertex_main(uint id: SV_VertexID)
{
    const float2 uv = float2((id << 1) & 2, id & 2);
    return { float4(uv * 2.0 - 1.0, 0.0, 1.0), uv };
}

[shader("fragment")]
float4 fragment_main(PresentVaryings v) : SV_Target
{
    return float4(pow(u_screen.Sample(u_sampler, v.uv).rgb, u_present.curve.x), 1.0);
}
