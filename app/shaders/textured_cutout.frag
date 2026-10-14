// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::TexturedCutout: as Textured, but pixels under half opacity are not drawn at all.
layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
  vec4 color = texture(sampler2D(u_texture, u_sampler), v_uv) * v_color;
  if (color.a < 0.5)
    discard;
  out_color = vec4(color.rgb, 1.0);
}
