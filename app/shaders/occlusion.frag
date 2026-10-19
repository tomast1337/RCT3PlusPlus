// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Occlusion, one step a draw (render::OcclusionStep): trace, blur, blend, apply.
layout(set = 0, binding = 0) uniform texture2D u_texture;
layout(set = 1, binding = 0) uniform sampler u_sampler;
layout(set = 2, binding = 0) uniform texture2D u_extra0;
layout(set = 3, binding = 0) uniform texture2D u_extra1;
layout(set = 4, binding = 0) uniform texture2D u_extra2;

// a: step, frame, radius, strength; b: the last frame's share, unused, then the depth terms.
layout(push_constant) uniform Draw {
  mat4 matrix;
  vec4 a;
  vec4 b;
} u_draw;

layout(location = 0) in vec2 v_uv;
layout(location = 0) out vec4 out_color;

const float kPi = 3.14159265;
// Our own values: directions and steps per side traced, the most pixels a trace reaches, and how far depths
// may differ, as a share of the depth, and still count as one surface when blurring and blending.
const int kSlices = 2;
const int kSteps = 4;
const float kMostPixels = 96.0;
const float kSameSurface = 0.08;
// Each pixel's place in a 4 by 4 square, in sixteenths, in an order that spreads neighbours apart.
const float kBayer[16] = float[16](0.0, 8.0, 2.0, 10.0, 12.0, 4.0, 14.0, 6.0, 3.0, 11.0, 1.0, 9.0, 15.0, 7.0, 13.0, 5.0);

float stored_depth(texture2D t, vec2 uv) {
  return textureLod(sampler2D(t, u_sampler), uv, 0.0).r;
}

// Depth from the eye, from the depth buffer's value.
float distance_of(float stored) {
  return u_draw.b.w / (stored - u_draw.b.z);
}

// The point seen at `uv`, in eye space.
vec3 eye_point(vec2 uv, float stored) {
  vec4 p = u_draw.matrix * vec4(uv * 2.0 - 1.0, stored, 1.0);
  return p.xyz / p.w;
}

vec3 eye_point_at(vec2 uv) {
  return eye_point(uv, stored_depth(u_texture, uv));
}

// Ground truth ambient occlusion (Jimenez et al. 2016): in each slice of the view round the pixel, the highest
// horizon either side within the radius, and the cosine-weighted share of the half circle between them that
// the surface's normal sees.
float trace() {
  vec2 size = vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
  vec2 texel = 1.0 / size;
  float stored = stored_depth(u_texture, v_uv);
  if (stored >= 1.0)
    return 1.0; // The sky
  vec3 p = eye_point(v_uv, stored);
  vec3 v = normalize(-p);
  // The normal from the neighbours on whichever side is nearer in depth, so edges do not bend it.
  vec3 right = eye_point_at(v_uv + vec2(texel.x, 0.0)) - p;
  vec3 left = p - eye_point_at(v_uv - vec2(texel.x, 0.0));
  vec3 up = eye_point_at(v_uv + vec2(0.0, texel.y)) - p;
  vec3 down = p - eye_point_at(v_uv - vec2(0.0, texel.y));
  vec3 across = abs(right.z) < abs(left.z) ? right : left;
  vec3 along = abs(up.z) < abs(down.z) ? up : down;
  vec3 n = normalize(cross(across, along));
  if (dot(n, v) < 0.0)
    n = -n;

  // How many pixels the radius spans here: the projection's scale on y is 1 over the inverse's.
  float radius = u_draw.a.z;
  float pixels = min(radius / u_draw.matrix[1][1] * 0.5 * size.y / p.z, kMostPixels);
  if (pixels < 1.0)
    return 1.0;
  ivec2 cell = ivec2(gl_FragCoord.xy) & 3;
  float frame = u_draw.a.y;
  float turn = fract(kBayer[cell.y * 4 + cell.x] / 16.0 + frame * 0.6180340);
  float shift = fract(float((cell.x + 2 * cell.y) & 3) / 4.0 + frame * 0.3819660);

  float seen = 0.0;
  for (int s = 0; s < kSlices; ++s) {
    float phi = (float(s) + turn) * kPi / float(kSlices);
    vec2 omega = vec2(cos(phi), sin(phi));
    vec3 dir = vec3(omega, 0.0);
    vec3 axis = normalize(cross(dir, v));
    vec3 ortho = cross(v, axis);
    vec3 projected = n - axis * dot(n, axis);
    float projected_length = length(projected);
    if (projected_length < 1e-4)
      continue;
    float cos_n = clamp(dot(projected, v) / projected_length, -1.0, 1.0);
    float angle_n = sign(dot(ortho, projected)) * acos(cos_n);
    vec2 horizon = vec2(-1.0);
    for (int side = 0; side < 2; ++side) {
      vec2 way = omega * (side == 0 ? -1.0 : 1.0) * pixels * texel;
      for (int j = 0; j < kSteps; ++j) {
        // Closer together near the pixel, where what is near counts most; at least a pixel out.
        float t = (float(j) + shift) / float(kSteps);
        vec2 uv = v_uv + way * max(t * t, 1.0 / pixels);
        vec3 delta = eye_point_at(uv) - p;
        float reach = length(delta);
        float rises = dot(delta, v) / max(reach, 1e-5);
        // Fades out towards the radius, so far things do not shut the point in.
        float near = clamp(1.0 - reach * reach / (radius * radius), 0.0, 1.0);
        horizon[side] = max(horizon[side], mix(-1.0, rises, near));
      }
    }
    float h0 = angle_n + max(-acos(horizon[0]) - angle_n, -kPi / 2.0);
    float h1 = angle_n + min(acos(horizon[1]) - angle_n, kPi / 2.0);
    float sin_n = sin(angle_n);
    seen += projected_length * 0.25 * ((-cos(2.0 * h0 - angle_n) + cos_n + 2.0 * h0 * sin_n) + (-cos(2.0 * h1 - angle_n) + cos_n + 2.0 * h1 * sin_n));
  }
  return clamp(seen / float(kSlices), 0.0, 1.0);
}

// The 4 by 4 pixels the turning repeats over, those on the same surface only.
float blur() {
  vec2 texel = 1.0 / vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
  float centre = distance_of(stored_depth(u_extra0, v_uv));
  float sum = 0.0;
  float weight = 0.0;
  for (int y = -2; y < 2; ++y)
    for (int x = -2; x < 2; ++x) {
      vec2 uv = v_uv + (vec2(x, y) + 0.5) * texel;
      float d = distance_of(stored_depth(u_extra0, uv));
      float w = clamp(1.0 - abs(d - centre) / (kSameSurface * centre), 0.0, 1.0);
      sum += textureLod(sampler2D(u_texture, u_sampler), uv, 0.0).r * w;
      weight += w;
    }
  return weight > 0.0 ? sum / weight : textureLod(sampler2D(u_texture, u_sampler), v_uv, 0.0).r;
}

// This frame's result, with the last frame's where the surface seen there is the one seen here, kept within
// what the pixels round here hold, so moving things do not leave trails.
float blend() {
  vec2 texel = 1.0 / vec2(textureSize(sampler2D(u_texture, u_sampler), 0));
  float now = textureLod(sampler2D(u_texture, u_sampler), v_uv, 0.0).r;
  float history = u_draw.b.x;
  float stored = stored_depth(u_extra1, v_uv);
  if (history <= 0.0 || stored >= 1.0)
    return now;
  vec4 then = u_draw.matrix * vec4(v_uv * 2.0 - 1.0, stored, 1.0);
  then /= then.w;
  vec2 uv = then.xy * 0.5 + 0.5;
  if (any(lessThan(uv, vec2(0.0))) || any(greaterThan(uv, vec2(1.0))))
    return now;
  float expected = distance_of(then.z);
  if (abs(distance_of(stored_depth(u_extra2, uv)) - expected) > kSameSurface * expected)
    return now;
  float low = now;
  float high = now;
  for (int i = 0; i < 4; ++i) {
    vec2 o = vec2(i == 0 ? 1.0 : i == 1 ? -1.0 : 0.0, i == 2 ? 1.0 : i == 3 ? -1.0 : 0.0) * 2.0 * texel;
    float n = textureLod(sampler2D(u_texture, u_sampler), v_uv + o, 0.0).r;
    low = min(low, n);
    high = max(high, n);
  }
  float before = clamp(textureLod(sampler2D(u_extra0, u_sampler), uv, 0.0).r, low - 0.05, high + 0.05);
  return mix(now, before, history);
}

void main() {
  int step = int(u_draw.a.x + 0.5);
  if (step == 0)
    out_color = vec4(vec3(trace()), 1.0);
  else if (step == 1)
    out_color = vec4(vec3(blur()), 1.0);
  else if (step == 2)
    out_color = vec4(vec3(blend()), 1.0);
  else
    out_color = vec4(0.0, 0.0, 0.0, (1.0 - textureLod(sampler2D(u_texture, u_sampler), v_uv, 0.0).r) * u_draw.a.w);
}
