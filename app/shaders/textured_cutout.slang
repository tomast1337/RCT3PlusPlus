// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::TexturedCutout: as Textured (whose vertex stage it is drawn with), but pixels under half
// opacity are not drawn at all.
import common;

[[vk::binding(0, 0)]] Texture2D u_texture;
[[vk::binding(0, 1)]] SamplerState u_sampler;

[shader("fragment")]
float4 fragment_main(TexturedVaryings v) : SV_Target
{
    const float4 color = u_texture.Sample(u_sampler, v.uv) * v.color;
    if (color.a < 0.5)
        discard;
    return float4(color.rgb, 1.0);
}
