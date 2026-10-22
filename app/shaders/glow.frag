// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Glow, one step of the glow chain (app/scene/park_glow.hpp). With `v_color.a` at 1 it shrinks:
// thirteen filtered reads round the point, weighted so every source pixel counts once, and keeps the part above
// `v_color.g` with a soft knee. With `v_color.a` at 0 it grows: a three by three tent of reads `v_color.r`
// texels apart. Either way the result is times `v_color.b`. The weights follow the downsample and upsample
// filters Jorge Jimenez described for Call of Duty: Advanced Warfare (SIGGRAPH 2014).
layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

vec3 at(vec2 offset) {
  const vec2 texel = 1.0 / vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
  return texture(sampler2D(u_texture, u_sampler), v_uv + offset * texel).rgb;
}

vec3 shrink() {
  // Each read already averages four pixels: five overlapping boxes of four reads.
  vec3 inner = at(vec2(-1, -1)) + at(vec2(1, -1)) + at(vec2(-1, 1)) + at(vec2(1, 1));
  vec3 corners = at(vec2(-2, -2)) + at(vec2(2, -2)) + at(vec2(-2, 2)) + at(vec2(2, 2));
  vec3 sides = at(vec2(-2, 0)) + at(vec2(2, 0)) + at(vec2(0, -2)) + at(vec2(0, 2));
  return inner * 0.125 + corners * 0.03125 + sides * 0.0625 + at(vec2(0)) * 0.125;
}

vec3 grow(float reach) {
  vec3 sum = at(vec2(0)) * 4.0;
  sum += (at(vec2(-reach, 0)) + at(vec2(reach, 0)) + at(vec2(0, -reach)) + at(vec2(0, reach))) * 2.0;
  sum += at(vec2(-reach, -reach)) + at(vec2(reach, -reach)) + at(vec2(-reach, reach)) + at(vec2(reach, reach));
  return sum / 16.0;
}

void main() {
  vec3 colour;
  if (v_color.a > 0.5) {
    colour = shrink();
    if (v_color.g > 0.0) {
      // Above the threshold in full, eased in over a knee below it so the glow does not switch on hard.
      const float knee = 0.5 * v_color.g;
      const float bright = max(colour.r, max(colour.g, colour.b));
      float eased = clamp(bright - v_color.g + knee, 0.0, 2.0 * knee);
      eased = eased * eased / (4.0 * knee + 1e-4);
      colour *= max(eased, bright - v_color.g) / max(bright, 1e-4);
    }
  } else {
    colour = grow(v_color.r);
  }
  out_color = vec4(colour * v_color.b, 1.0);
}
