// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Lit, and LitCutout with the constant set: then pixels under half opacity are not drawn.
layout(constant_id = 0) const bool kCutout = false;

layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;
// render::SceneLight's shadow: the depth seen from the sun, and how it was seen (render/types.hpp).
layout(set = 2, binding = 0) uniform texture2D u_shadow;
layout(set = 3, binding = 0) uniform texture2D u_sun_view;
// Tiling noise read over world x and z (park/ground_variation.hpp), when sky.w is 1.
layout(set = 4, binding = 0) uniform texture2D u_variation;
// An older drawing of the shadow, mixed in as sun_direction.w says.
layout(set = 5, binding = 0) uniform texture2D u_shadow_before;
layout(set = 6, binding = 0) uniform texture2D u_sun_view_before;

// render::SceneLight, packed. sun_direction.w is 0 without a shadow, else 1 plus the older shadow's weight; sky.w
// is 1 when the variation is given;
// sun.w is the haze rate and ground.w the haze colour, 8 bits a channel, red highest.
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
// How far an object crossing between two levels of detail is through its band, and its side: 1 the coarser
// level, -1 the finer, 0 not crossing; 2 and -2 tint the coarser red and the finer blue (lit_crossing.vert).
layout(location = 4) flat in vec2 v_cross;
layout(location = 0) out vec4 out_color;

// How much of the 2 by 2 texels round `at` (in texels, from the map's corner) lies nearer the sun than `depth`,
// blended by where `at` falls between them, so edges move smoothly rather than a texel at a time.
float lit_at(texture2D shadow, vec2 at, vec2 size, float depth) {
  vec2 corner = floor(at - 0.5);
  vec4 stored = textureGather(sampler2D(shadow, u_sampler), (corner + 1.0) / size);
  vec4 lit = step(vec4(depth), stored); // Gathered as (0,1) (1,1) (1,0) (0,0)
  vec2 f = at - 0.5 - corner;
  return mix(mix(lit.w, lit.z, f.x), mix(lit.x, lit.y, f.x), f.y);
}

// 1 in full sunlight, 0 where something nearer the sun covers this point: an average over 3 by 3 texels, so the
// edge is soft over about one texel either side. Fades out towards the map's edge, beyond which all is lit.
float sunlit(texture2D shadow, texture2D sun_view, vec3 n) {
  mat4 view = mat4(texelFetch(sampler2D(sun_view, u_sampler), ivec2(0, 0), 0), texelFetch(sampler2D(sun_view, u_sampler), ivec2(1, 0), 0),
                   texelFetch(sampler2D(sun_view, u_sampler), ivec2(2, 0), 0), texelFetch(sampler2D(sun_view, u_sampler), ivec2(3, 0), 0));
  vec4 extra = texelFetch(sampler2D(sun_view, u_sampler), ivec2(4, 0), 0);
  // Pushed off the surface along its normal, and compared with more slack the more the surface leans away from
  // the sun, so it does not shadow itself: a leaning surface spans several depths within one texel.
  float facing = clamp(dot(n, u_draw.sun_direction.xyz), 0.05, 1.0);
  float lean = min(sqrt(1.0 - facing * facing) / facing, 6.0);
  vec4 clip = view * vec4(v_world + n * extra.x * (1.0 + lean), 1.0);
  vec3 p = clip.xyz / clip.w;
  float edge = max(abs(p.x), abs(p.y));
  if (edge >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
    return 1.0;
  vec2 size = vec2(textureSize(sampler2D(shadow, u_sampler), 0));
  vec2 at = vec2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * size;
  float depth = p.z - extra.y * (1.0 + 2.0 * lean);
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      sum += lit_at(shadow, at + vec2(x, y), size, depth);
  return mix(sum / 9.0, 1.0, smoothstep(0.85, 1.0, edge));
}

// How much lighter or darker the ground is here: three sizes of the noise, widest strongest, so no one size
// repeats visibly. Features about 240, 100 and 40 world units across.
const float kVariation = 0.15;
float variation() {
  // One tile of the picture holds 8 features.
  float n = 0.5 * texture(sampler2D(u_variation, u_sampler), v_world.xz / 1920.0).r;
  n += 0.3 * texture(sampler2D(u_variation, u_sampler), v_world.xz / 800.0 + 0.37).r;
  n += 0.2 * texture(sampler2D(u_variation, u_sampler), v_world.xz / 320.0 + 0.71).r;
  return 1.0 + kVariation * (2.0 * n - 1.0);
}

// A 4 by 4 Bayer pattern: each pixel's place, in sixteenths, in the order a square's pixels fill as a fade grows.
const float kDither[16] = float[16](1.0, 9.0, 3.0, 11.0, 13.0, 5.0, 15.0, 7.0, 4.0, 12.0, 2.0, 10.0, 16.0, 8.0, 14.0, 6.0);

// Whether a crossing object leaves this pixel to its other level: the coarser level draws the pixels whose place
// is within how far through the band it is, the finer the rest, so between them each pixel is drawn once.
bool crossed_out() {
  if (v_cross.y == 0.0)
    return false;
  ivec2 p = ivec2(gl_FragCoord.xy) & 3;
  bool coarser = kDither[p.y * 4 + p.x] / 16.0 <= v_cross.x;
  return coarser != (v_cross.y > 0.0);
}

void main() {
  if (crossed_out())
    discard;
  vec4 color = texture(sampler2D(u_texture, u_sampler), v_uv) * v_color;
  if (abs(v_cross.y) > 1.5)
    color.rgb = mix(color.rgb, v_cross.y > 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0), 0.6);
  if (u_draw.sky.w > 0.5)
    color.rgb *= variation();
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
    float before = u_draw.sun_direction.w - 1.0;
    // Looked up off the side that is seen: a shell drawn from both sides, such as a slide's open trough, is
    // seen from behind inside, and pushed the normal's way it would shadow itself in stripes.
    vec3 seen = gl_FrontFacing ? n : -n;
    float lit = sunlit(u_shadow, u_sun_view, seen);
    if (before > 0.002)
      lit = mix(lit, sunlit(u_shadow_before, u_sun_view_before, seen), before);
    direct *= lit;
    ambient *= mix(0.7, 1.0, lit);
  }
  vec3 shaded = color.rgb * (ambient + direct);
  // Haze: beyond its start, far surfaces fade towards the haze colour. The distance is the depth from the eye,
  // which the perspective divide leaves in gl_FragCoord.w.
  if (u_draw.sun.w > 0.0) {
    const float kHazeStart = 150.0;
    float rate = u_draw.sun.w;
    float packed = u_draw.ground.w;
    vec3 haze = vec3(floor(packed / 65536.0), mod(floor(packed / 256.0), 256.0), mod(packed, 256.0)) / 255.0;
    float far = 1.0 - exp(-rate * max(1.0 / gl_FragCoord.w - kHazeStart, 0.0));
    shaded = mix(shaded, haze, far);
  }
  out_color = vec4(shaded, color.a);
}
