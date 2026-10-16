// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Water. The surface's slope comes from two drifting layers of a tiling height picture and from
// the ripples. What it shows is a Fresnel (Schlick) blend of the mirrored scene and what lies under it; light
// under the water is absorbed over the length of water it crosses (Beer-Lambert), red first, and replaced by
// light scattered in the water's own colour, so shallows are clear and depths take the water's colour. A faint,
// broken foam gathers in the shallows and on ripple crests.

layout(set = 0, binding = 0) uniform texture2D u_waves;
layout(set = 1, binding = 0) uniform sampler u_sampler;
layout(set = 2, binding = 0) uniform texture2D u_reflection;
layout(set = 3, binding = 0) uniform texture2D u_scene;
layout(set = 4, binding = 0) uniform texture2D u_scene_depth;
layout(set = 5, binding = 0) uniform texture2D u_ripples;

// render::WaterView and render::SceneLight, packed (render/vulkan/vulkan_device.cpp).
layout(push_constant) uniform Draw {
  mat4 transform;
  vec4 eye_time;     // eye, seconds
  vec4 sun_dir_near; // towards the sun, near plane
  vec4 sun_far;      // sunlight, far plane
  vec4 sky_flags;    // skylight, flags
} u_draw;

layout(location = 0) in vec3 v_world;
layout(location = 1) in vec4 v_color;
layout(location = 2) in float v_depth;
layout(location = 3) in vec4 v_clip;
layout(location = 0) out vec4 out_color;

const uint kReflection = 1u;
const uint kRefraction = 2u;
const uint kWaves = 4u;
const uint kFoam = 8u;
const uint kRipples = 16u;

const float kFresnelBase = 0.02;                     // Reflectance seen straight on, for a refractive index near 1.33
const vec3 kAbsorption = vec3(0.42, 0.085, 0.055);   // Per world unit of water crossed
const vec2 kWaveScale = vec2(1.0 / 11.0, 1.0 / 3.7); // Picture repeats per world unit: broad layer, fine layer
const vec2 kWaveDriftA = vec2(0.010, 0.006);         // Picture widths per second
const vec2 kWaveDriftB = vec2(-0.016, 0.012);
const float kWaveHeight = 0.08;                      // World units the picture's full range stands for
const float kWaveCalm = 80.0;                        // Waves fade over this distance, before they shimmer
const float kReflectionBend = 0.6;                   // Screen shift per unit of slope
const float kRefractionBend = 0.35;
const float kShoreSoftness = 0.08;                   // Depth over which the water's edge fades in
const float kFoamDepth = 0.35;                       // Shallows foam gathers in
const float kFoamMost = 0.28;                        // The most of a point foam covers
const float kCrestLow = 0.015;                       // Ripple heights foam starts and is full on
const float kCrestHigh = 0.050;

bool has(uint flag) {
  return (uint(u_draw.sky_flags.w) & flag) != 0u;
}

// Slope of the height picture at `p`, in height per picture width.
vec2 picture_slope(vec2 p) {
  vec2 size = vec2(textureSize(sampler2D(u_waves, u_sampler), 0));
  vec2 step = 1.0 / size;
  float h = texture(sampler2D(u_waves, u_sampler), p).r;
  float hx = texture(sampler2D(u_waves, u_sampler), p + vec2(step.x, 0.0)).r;
  float hy = texture(sampler2D(u_waves, u_sampler), p + vec2(0.0, step.y)).r;
  return vec2(hx - h, hy - h) * size;
}

// The ripples' slope in x and z, and how high the highest crest stands here.
vec3 ripples(vec2 at) {
  vec2 slope = vec2(0.0);
  float crest = 0.0;
  int count = textureSize(sampler2D(u_ripples, u_sampler), 0).x;
  for (int i = 0; i < count; ++i) {
    vec4 ring = texelFetch(sampler2D(u_ripples, u_sampler), ivec2(i, 0), 0);
    if (ring.w <= 0.0)
      continue;
    vec4 shape = texelFetch(sampler2D(u_ripples, u_sampler), ivec2(i, 1), 0);
    vec2 d = at - ring.xy;
    float r = length(d);
    float x = r - ring.z;
    float u = x / shape.y;
    if (abs(u) > 3.0)
      continue;
    float envelope = exp(-u * u);
    float c = cos(shape.x * x);
    float s = sin(shape.x * x);
    float h = ring.w * envelope * c;
    float dh = ring.w * envelope * (-2.0 * x / (shape.y * shape.y) * c - shape.x * s);
    slope += dh * d / max(r, 1e-3);
    crest = max(crest, h);
  }
  return vec3(slope, crest);
}

// Distance along the view axis of a depth read back from the depth buffer.
float view_distance(float depth) {
  float near = u_draw.sun_dir_near.w;
  float far = u_draw.sun_far.w;
  return near * far / (far - depth * (far - near));
}

float scene_distance(vec2 uv) {
  vec2 size = vec2(textureSize(sampler2D(u_scene_depth, u_sampler), 0));
  ivec2 texel = ivec2(clamp(uv * size, vec2(0.0), size - 1.0));
  return view_distance(texelFetch(sampler2D(u_scene_depth, u_sampler), texel, 0).r);
}

void main() {
  float time = u_draw.eye_time.w;
  vec3 to_eye = u_draw.eye_time.xyz - v_world;
  float dist = length(to_eye);
  vec3 view = to_eye / max(dist, 1e-4);
  vec3 sun = u_draw.sun_far.rgb;
  vec3 to_sun = normalize(u_draw.sun_dir_near.xyz);
  vec3 sky = u_draw.sky_flags.rgb;

  vec2 slope = vec2(0.0);
  if (has(kWaves)) {
    float calm = 1.0 / (1.0 + dist / kWaveCalm);
    vec2 broad = picture_slope(v_world.xz * kWaveScale.x + time * kWaveDriftA) * kWaveScale.x;
    vec2 fine = picture_slope(v_world.xz * kWaveScale.y + time * kWaveDriftB) * kWaveScale.y;
    slope += (broad + 0.5 * fine) * kWaveHeight * calm;
  }
  vec3 rip = has(kRipples) ? ripples(v_world.xz) : vec3(0.0);
  slope += rip.xy;
  vec3 n = normalize(vec3(-slope.x, 1.0, -slope.y));

  vec2 ndc = v_clip.xy / v_clip.w;
  vec2 screen = vec2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
  // Nearer water bends what it shows over more of the screen.
  vec2 bend = n.xz / (1.0 + 0.02 * dist);

  // How deep the water is under this point, and how much of it the line of sight crosses.
  float depth = v_depth;
  float path = depth / max(view.y, 0.15);
  vec3 below = vec3(0.0);
  bool see_below = has(kRefraction);
  if (see_below) {
    float water = v_clip.w;
    vec2 bent = screen + bend * kRefractionBend * clamp(depth, 0.0, 1.0);
    float behind = scene_distance(bent);
    // What stands in front of the water must not be pulled into it.
    if (behind < water) {
      bent = screen;
      behind = scene_distance(screen);
    }
    path = max(behind - water, 0.0) * dist / max(water, 1e-4);
    depth = path * view.y;
    below = texture(sampler2D(u_scene, u_sampler), clamp(bent, vec2(0.001), vec2(0.999))).rgb;
  }

  float facing = clamp(dot(n, view), 0.0, 1.0);
  float fresnel = kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - facing, 5.0);
  vec3 mirrored = sky;
  if (has(kReflection))
    mirrored = texture(sampler2D(u_reflection, u_sampler), clamp(screen + bend * kReflectionBend * 0.05, vec2(0.001), vec2(0.999))).rgb;

  vec3 body_light = sky + sun * max(to_sun.y, 0.0) * 0.5;
  vec3 scattered = v_color.rgb * body_light;
  vec3 kept = exp(-kAbsorption * (0.5 + v_color.a) * path);

  vec3 half_way = normalize(to_sun + view);
  float shine = max(dot(n, half_way), 0.0);
  vec3 glint = sun * (pow(shine, 900.0) * 1.2 + pow(shine, 64.0) * 0.04) * step(0.0, to_sun.y);

  vec3 colour;
  float alpha;
  if (see_below) {
    colour = mix(below * kept + scattered * (1.0 - kept), mirrored, fresnel) + glint;
    alpha = 1.0;
  } else {
    // What is under the water is already on screen: blend over it, keeping as much as the water lets through.
    float through = (1.0 - fresnel) * dot(kept, vec3(1.0 / 3.0));
    alpha = 1.0 - through;
    colour = ((1.0 - fresnel) * scattered * (1.0 - dot(kept, vec3(1.0 / 3.0))) + fresnel * mirrored + glint) / max(alpha, 1e-3);
  }

  if (has(kFoam)) {
    float shallows = (1.0 - smoothstep(0.0, kFoamDepth, depth)) * smoothstep(0.0, 0.04, depth);
    float grain = texture(sampler2D(u_waves, u_sampler), v_world.xz * 0.31 + vec2(0.020, -0.013) * time).r;
    float grain2 = texture(sampler2D(u_waves, u_sampler), v_world.xz * 0.77 - vec2(0.011, 0.017) * time).r;
    float broken = smoothstep(0.55, 0.9, 0.6 * grain + 0.4 * grain2 + 0.3 * shallows);
    float foam = max(shallows * broken, 0.6 * smoothstep(kCrestLow, kCrestHigh, rip.z));
    foam *= kFoamMost / (1.0 + dist / 150.0);
    colour = mix(colour, body_light * 0.95, foam);
    alpha = mix(alpha, 1.0, foam);
  }

  // The edge fades in over the first bit of depth, so the shore has no hard line.
  alpha *= smoothstep(0.0, kShoreSoftness, depth);
  out_color = vec4(colour, alpha);
}
