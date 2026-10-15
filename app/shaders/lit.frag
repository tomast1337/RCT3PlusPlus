// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Lit, and LitCutout with the constant set: then pixels under half opacity are not drawn.
layout(constant_id = 0) const bool kCutout = false;

layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;

// render::SceneLight, packed.
layout(push_constant) uniform Draw {
  mat4 transform;
  vec4 sun_direction;
  vec4 sun;
  vec4 sky;
  vec4 ground;
} u_draw;

layout(location = 0) in vec4 v_color;
layout(location = 1) in vec2 v_uv;
layout(location = 2) in vec3 v_normal;
layout(location = 0) out vec4 out_color;

void main() {
  vec4 color = texture(sampler2D(u_texture, u_sampler), v_uv) * v_color;
  if (kCutout) {
    if (color.a < 0.5)
      discard;
    color.a = 1.0;
  }

  // Lit as the normal says, from whichever side the surface is seen. Turning the normal to face the eye
  // would darken everything above the eye whose normal tilts up (roofs, the cards of trees, which are
  // seen from below there) along a hard line at the horizon. Without a normal, face up.
  vec3 n = vec3(0.0, 1.0, 0.0);
  if (dot(v_normal, v_normal) > 1e-12)
    n = normalize(v_normal);
  vec3 ambient = mix(u_draw.ground.rgb, u_draw.sky.rgb, n.y * 0.5 + 0.5);
  vec3 direct = u_draw.sun.rgb * max(dot(n, u_draw.sun_direction.xyz), 0.0);
  out_color = vec4(color.rgb * (ambient + direct), color.a);
}
