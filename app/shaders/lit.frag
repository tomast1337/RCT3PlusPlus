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
// The texture's relief map (park/relief_maps.hpp), when sky.w says it is given.
layout(set = 7, binding = 0) uniform texture2D u_relief;

// render::SceneLight, packed. sun_direction.w is 0 without a shadow, else 1 plus the older shadow's weight; sky.w
// is 1 when the variation is given, plus 2 when the relief map is, plus 4 times the surface detail (0 to 2);
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
// Where the eye is, w 1; w 0 in a view without perspective, which has no such point.
layout(location = 5) flat in vec4 v_eye;
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

// Our own values: how far the relief map's lean is followed, and how deep its lowest point lies under its highest,
// in texels, when the texture is shifted by depth.
const float kRelief = 0.7;
const float kDepth = 1.5;
// Our own values: the relief is whole where one texel spans kReliefNear pixels or more, and gone where it spans
// kReliefFar or fewer.
const float kReliefNear = 1.0;
const float kReliefFar = 0.5;

// How the world point and the texture coordinate change to the next pixel across and up. Taken once at the
// top of main(): changes are only defined where all four pixels of a 2 by 2 block run the same code.
vec3 dp1;
vec3 dp2;
vec2 duv1;
vec2 duv2;

// The ways the texture's u and v run along the surface at this pixel, unit length and square to `n`, from how
// they change across the screen, so meshes need no tangents. False where the texture does not change.
bool texture_frame(vec3 n, out vec3 u, out vec3 v) {
  vec3 dp2perp = cross(dp2, n);
  vec3 dp1perp = cross(n, dp1);
  u = dp2perp * duv1.x + dp1perp * duv2.x;
  v = dp2perp * duv1.y + dp1perp * duv2.y;
  u -= n * dot(n, u);
  v -= n * dot(n, v);
  if (dot(u, u) < 1e-30 || dot(v, v) < 1e-30)
    return false;
  u = normalize(u);
  v = normalize(v);
  return true;
}

// Where along the texture the eye meets the relief: a raised point is seen nearer the eye
// than where the ray meets the flat surface, by its height over how steeply the surface is seen. One step,
// so steep views smear rather than show the relief's sides. Unchanged in a view without perspective.
vec2 shifted(vec3 n, vec3 u, vec3 v) {
  if (v_eye.w < 0.5)
    return v_uv;
  vec3 to_eye = normalize(v_eye.xyz - v_world);
  float facing = max(abs(dot(to_eye, n)), 0.3);
  vec2 size = vec2(textureSize(sampler2D(u_relief, u_sampler), 0));
  float height = textureGrad(sampler2D(u_relief, u_sampler), v_uv, duv1, duv2).b - 0.5;
  return v_uv + vec2(dot(to_eye, u), dot(to_eye, v)) / facing * height * kDepth / size;
}

// Our own values: how short and soft the relief's own shadows are, and the sun's glint on bumps.
const float kReliefSteps[2] = float[2](1.5, 4.0); // Texels towards the sun the relief is looked at
const float kReliefShadowEdge = 2.0; // How quickly the shade grows as a bump rises over the sun's ray
const float kSheen = 0.05;
const float kGlint = 24.0;

// How much of the sun reaches `uv` past the relief round it: a few steps along the
// texture towards the sun, each height compared with how high the ray to the sun has climbed there.
float relief_sunlit(vec2 uv, vec3 n, vec3 u, vec3 v, float height) {
  vec3 sun = u_draw.sun_direction.xyz;
  float up = dot(sun, n);
  vec2 across = vec2(dot(sun, u), dot(sun, v));
  float run = length(across);
  if (up <= 0.0 || run < 1e-4)
    return 1.0;
  vec2 size = vec2(textureSize(sampler2D(u_relief, u_sampler), 0));
  vec2 texel = across / run / size;
  float climb = up / run / kDepth; // In the map's height, per texel
  float over = 0.0;
  for (int i = 0; i < 2; ++i) {
    float d = kReliefSteps[i];
    float there = textureGrad(sampler2D(u_relief, u_sampler), uv + texel * d, duv1, duv2).b;
    over = max(over, there - height - climb * d);
  }
  return 1.0 - clamp(over * kReliefShadowEdge, 0.0, 1.0);
}

// A number from 0 to 1 for each whole point of `p`.
float hash(vec2 p) {
  uvec2 q = uvec2(ivec2(floor(p)));
  uint h = (q.x * 1597334677u) ^ (q.y * 3812015801u);
  h = (h ^ (h >> 16)) * 2246822519u;
  return float(h ^ (h >> 13)) / 4294967295.0;
}

// Smooth noise from 0 to 1 with features a unit of `p` across.
float value_noise(vec2 p) {
  vec2 f = fract(p);
  f = f * f * (3.0 - 2.0 * f);
  return mix(mix(hash(p), hash(p + vec2(1.0, 0.0)), f.x), mix(hash(p + vec2(0.0, 1.0)), hash(p + vec2(1.0, 1.0)), f.x), f.y);
}

// Our own value: how much lighter or darker the grain makes the colour at most.
const float kGrain = 0.08;

// Fine grain, three to a texel, where a texel spans several pixels and the texture alone would
// look blurred; it fades out as the grains shrink to a pixel, so it does not shimmer far away.
// shortcut: one kind of grain for every texture; noise kinds per material (fabric, rock, wood) later.
float grain() {
  vec2 p = v_uv * vec2(textureSize(sampler2D(u_texture, u_sampler), 0)) * 3.0;
  float spread = max(length(dFdx(p)), length(dFdy(p))); // Grains across one pixel
  float shown = 1.0 - smoothstep(0.4, 1.2, spread);
  float n = 0.65 * value_noise(p) + 0.35 * value_noise(p * 2.3 + 17.0);
  return 1.0 + kGrain * shown * (2.0 * n - 1.0);
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
  // Lit as the normal says, from whichever side the surface is seen. Turning the normal to face the eye
  // would darken everything above the eye whose normal tilts up (roofs, the cards of trees, which are
  // seen from below there) along a hard line at the horizon. Without a normal, face up.
  vec3 n = vec3(0.0, 1.0, 0.0);
  if (dot(v_normal, v_normal) > 1e-12)
    n = normalize(v_normal);
  dp1 = dFdx(v_world);
  dp2 = dFdy(v_world);
  duv1 = dFdx(v_uv);
  duv2 = dFdy(v_uv);

  int detail = int(u_draw.sky.w / 4.0);
  bool variation_given = mod(u_draw.sky.w, 2.0) > 0.5;
  bool relief_given = mod(u_draw.sky.w, 4.0) > 1.5;
  vec3 along_u;
  vec3 along_v;
  // The relief shows only near the eye, where a texel spans more than a pixel; it fades in over the band and
  // costs nothing beyond it, where most of the view is.
  float near = 0.0;
  if (detail >= 2 && relief_given) {
    vec2 size = vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
    float pixels = 1.0 / max(max(length(duv1 * size), length(duv2 * size)), 1e-6); // Pixels one texel spans
    near = smoothstep(kReliefFar, kReliefNear, pixels);
  }
  bool framed = near > 0.0 && texture_frame(n, along_u, along_v);
  vec2 uv = framed ? mix(v_uv, shifted(n, along_u, along_v), near) : v_uv;
  // Read with the unshifted coordinate's changes, so the shift does not change the mip level.
  vec4 color = (detail >= 2 ? textureGrad(sampler2D(u_texture, u_sampler), uv, duv1, duv2) : texture(sampler2D(u_texture, u_sampler), v_uv)) * v_color;
  if (abs(v_cross.y) > 1.5)
    color.rgb = mix(color.rgb, v_cross.y > 0.0 ? vec3(1.0, 0.0, 0.0) : vec3(0.0, 0.0, 1.0), 0.6);
  if (variation_given)
    color.rgb *= variation();
  if (detail >= 1)
    color.rgb *= grain();
  if (kCutout) {
    if (color.a < 0.5)
      discard;
    color.a = 1.0;
  }

  // The shadow is looked up by the true normal below; only the light follows the relief.
  vec3 bumped = n;
  float open = 1.0;       // How much of the sky reaches into the relief here
  float relief_lit = 1.0; // How much of the sun gets past the relief round here
  if (framed) {
    vec4 relief = textureGrad(sampler2D(u_relief, u_sampler), uv, duv1, duv2);
    vec2 lean = (relief.rg * 2.0 - 1.0) * kRelief * near;
    bumped = normalize(n * sqrt(max(1.0 - dot(lean, lean), 0.0)) + along_u * lean.x + along_v * lean.y);
    open = mix(1.0, relief.a, near);
    relief_lit = mix(1.0, relief_sunlit(uv, n, along_u, along_v, relief.b), near);
  }
  vec3 ambient = mix(u_draw.ground.rgb, u_draw.sky.rgb, bumped.y * 0.5 + 0.5) * open;
  vec3 direct = u_draw.sun.rgb * max(dot(bumped, u_draw.sun_direction.xyz), 0.0) * relief_lit;
  float sun_reaches = relief_lit;
  // In shadow the sky still lights a surface, but less of it: part of the sky is behind what casts the shadow.
  if (u_draw.sun_direction.w > 0.5 && dot(direct, direct) > 0.0) {
    float before = u_draw.sun_direction.w - 1.0;
    float lit = sunlit(u_shadow, u_sun_view, n);
    if (before > 0.002)
      lit = mix(lit, sunlit(u_shadow_before, u_sun_view_before, n), before);
    direct *= lit;
    ambient *= mix(0.7, 1.0, lit);
    sun_reaches *= lit;
  }
  vec3 shaded = color.rgb * (ambient + direct);
  // The sun's glint off the relief's slopes that face between it and the eye.
  if (framed && v_eye.w > 0.5) {
    vec3 half_way = normalize(u_draw.sun_direction.xyz + normalize(v_eye.xyz - v_world));
    shaded += u_draw.sun.rgb * kSheen * near * pow(max(dot(bumped, half_way), 0.0), kGlint) * sun_reaches * open;
  }
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
