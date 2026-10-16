// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Glow. Four filtered reads on the diagonals round the point, `v_color.r` texels out; the
// average's part above `v_color.g` is kept, times `v_color.b`.
layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

void main() {
  const vec2 step = v_color.r / vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
  vec3 sum = texture(sampler2D(u_texture, u_sampler), v_uv + vec2(-step.x, -step.y)).rgb;
  sum += texture(sampler2D(u_texture, u_sampler), v_uv + vec2(step.x, -step.y)).rgb;
  sum += texture(sampler2D(u_texture, u_sampler), v_uv + vec2(-step.x, step.y)).rgb;
  sum += texture(sampler2D(u_texture, u_sampler), v_uv + vec2(step.x, step.y)).rgb;
  out_color = vec4(max(sum / 4.0 - v_color.g, 0.0) * v_color.b, 1.0);
}
