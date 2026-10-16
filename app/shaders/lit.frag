// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Lit, and LitCutout with the constant set: then pixels under half opacity are not drawn.
layout(constant_id = 0) const bool kCutout = false;

layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;
// render::SceneLight's shadow: the depth seen from the sun, and how it was seen (render/types.hpp).
layout(set = 2, binding = 0) uniform texture2D u_shadow;
layout(set = 3, binding = 0) uniform texture2D u_sun_view;

// render::SceneLight, packed. sun_direction.w is 1 when the shadow is given.
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
layout(location = 3) in vec3 v_world;
layout(location = 0) out vec4 out_color;

// How much of the 2 by 2 texels round `at` (in texels, from the map's corner) lies nearer the sun than `depth`,
// blended by where `at` falls between them, so edges move smoothly rather than a texel at a time.
float lit_at(vec2 at, vec2 size, float depth) {
  vec2 corner = floor(at - 0.5);
  vec4 stored = textureGather(sampler2D(u_shadow, u_sampler), (corner + 1.0) / size);
  vec4 lit = step(vec4(depth), stored); // Gathered as (0,1) (1,1) (1,0) (0,0)
  vec2 f = at - 0.5 - corner;
  return mix(mix(lit.w, lit.z, f.x), mix(lit.x, lit.y, f.x), f.y);
}

// 1 in full sunlight, 0 where something nearer the sun covers this point: an average over 3 by 3 texels, so the
// edge is soft over about one texel either side. Fades out towards the map's edge, beyond which all is lit.
float sunlit(vec3 n) {
  mat4 view = mat4(texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(0, 0), 0), texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(1, 0), 0),
                   texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(2, 0), 0), texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(3, 0), 0));
  vec4 extra = texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(4, 0), 0);
  // Pushed off the surface along its normal, and compared with more slack the more the surface leans away from
  // the sun, so it does not shadow itself: a leaning surface spans several depths within one texel.
  float facing = clamp(dot(n, u_draw.sun_direction.xyz), 0.05, 1.0);
  float lean = min(sqrt(1.0 - facing * facing) / facing, 6.0);
  vec4 clip = view * vec4(v_world + n * extra.x * (1.0 + lean), 1.0);
  vec3 p = clip.xyz / clip.w;
  float edge = max(abs(p.x), abs(p.y));
  if (edge >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
    return 1.0;
  vec2 size = vec2(textureSize(sampler2D(u_shadow, u_sampler), 0));
  vec2 at = vec2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * size;
  float depth = p.z - extra.y * (1.0 + 2.0 * lean);
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      sum += lit_at(at + vec2(x, y), size, depth);
  return mix(sum / 9.0, 1.0, smoothstep(0.85, 1.0, edge));
}

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
  // In shadow the sky still lights a surface, but less of it: part of the sky is behind what casts the shadow.
  if (u_draw.sun_direction.w > 0.5 && dot(direct, direct) > 0.0) {
    float lit = sunlit(n);
    direct *= lit;
    ambient *= mix(0.7, 1.0, lit);
  }
  out_color = vec4(color.rgb * (ambient + direct), color.a);
}
