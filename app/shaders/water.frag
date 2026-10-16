// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Water. The surface's slope comes from two drifting layers of a tiling height picture and from
// the ripples. Seen from above, it shows a blend of the mirrored scene and what lies under it, leaning on the
// mirror more than real water does so the park shows in it even from high up; light under the water is absorbed
// over the length of water it crosses (Beer-Lambert), red first, and replaced by light scattered in the water's
// own blue, so shallows are clear and depths turn blue, though even deep water lets a little of the bottom
// through. The bottom catches a moving net of sunlight focused by the waves (caustics), fading with depth. A
// faint, broken foam gathers in the shallows and on ripple crests.
//
// Seen from below, the surface shows the world above inside a bright circle overhead and mirrors the dark water
// outside it, where the light cannot leave (total internal reflection). With the eye under water the draw is
// instead one quad over the view: the scene behind it is fogged and coloured by the water each line of sight
// crosses, and what lies under the surface catches the caustics. Every number here is our own tuning.

layout(set = 0, binding = 0) uniform texture2D u_waves;
layout(set = 1, binding = 0) uniform sampler u_sampler;
layout(set = 2, binding = 0) uniform texture2D u_reflection;
layout(set = 3, binding = 0) uniform texture2D u_scene;
layout(set = 4, binding = 0) uniform texture2D u_scene_depth;
layout(set = 5, binding = 0) uniform texture2D u_ripples;
// render::SceneLight's shadow, as the Lit shaders read it (lit.frag).
layout(set = 6, binding = 0) uniform texture2D u_shadow;
layout(set = 7, binding = 0) uniform texture2D u_sun_view;

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
layout(location = 2) in float v_depth; // Under water: the height of the surface over the eye
layout(location = 3) in vec4 v_clip;
layout(location = 0) out vec4 out_color;

const uint kReflection = 1u;
const uint kRefraction = 2u;
const uint kWaves = 4u;
const uint kFoam = 8u;
const uint kRipples = 16u;
const uint kUnderwater = 32u;
const uint kShadow = 64u;

const float kFresnelBase = 0.02;                     // Reflectance seen straight on, for a refractive index near 1.33
const float kMirrorLeast = 0.42;                     // The least the mirror shows, looking straight down
const vec2 kMirrorTint = vec2(0.45, 1.8);           // How much of the water's colour the mirror takes, and its strength
const float kWaterIndex = 1.33;
const vec3 kAbsorption = vec3(0.80, 0.30, 0.12);     // Per world unit of water crossed
const vec3 kMurk = vec3(0.015);                      // Light scattered out of a line of sight, per unit, seen from inside
const float kInsideClarity = 0.2;                    // Share of kAbsorption a swimmer's eye sees, so the fog is not a wall
const vec3 kDeepClarity = vec3(0.05, 0.10, 0.16);    // The least of the bottom's light that still shows, blue keeps most
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
const float kCausticScale = 0.55;                    // Cells of the net per world unit
const float kCausticLine = 0.09;                     // Width of a bright line, in cells
const float kCausticFade = 7.0;                      // Depth over which caustics fade to a third
const float kCausticGain = 2.2;                      // Light a full line adds, as a share of the bottom's own
const float kCausticDim = 0.25;                      // Light the bottom loses between the lines, where the net drew it

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

// Where the cell `c` of a jittered grid has its point at `time`: a fixed random place in the cell, circling it.
vec2 cell_point(vec2 c, float time) {
  uvec2 u = uvec2(ivec2(c));
  uint h = (u.x * 668265261u) ^ (u.y * 374761393u);
  h = (h ^ (h >> 13u)) * 739982445u;
  h ^= h >> 16u;
  vec2 r = vec2(float(h & 65535u), float(h >> 16u)) / 65535.0;
  return c + 0.5 + 0.36 * sin(time * (0.5 + 0.4 * r) + 6.2832 * r.yx);
}

// How far `p` is from the nearest border between the cells of the grid's points: 0 on a border.
float cell_border(vec2 p, float time) {
  vec2 base = floor(p);
  float first = 9.0;
  float second = 9.0;
  for (int y = -1; y <= 1; ++y) {
    for (int x = -1; x <= 1; ++x) {
      vec2 q = cell_point(base + vec2(x, y), time) - p;
      float d = dot(q, q);
      second = min(second, max(d, first));
      first = min(first, d);
    }
  }
  return sqrt(second) - sqrt(first);
}

// Sunlight the waves focus on the bottom, 0 to about 1, at `p` in cells of the net. `blur` is how many cells a
// pixel spans plus the spread deep water gives; the lines widen with it and fade before they would flicker.
// Two nets drift through each other, bent by the wave picture so their lines wander as the light through moving
// water does, and are brightest where they cross.
float caustics(vec2 p, float time, float blur) {
  vec2 warp = vec2(texture(sampler2D(u_waves, u_sampler), p * 0.13 + vec2(0.011, 0.004) * time).r,
                   texture(sampler2D(u_waves, u_sampler), p * 0.11 + vec2(0.37, 0.61) - vec2(0.006, 0.010) * time).r);
  p += (warp - 0.5) * 1.2;
  float width = kCausticLine + blur;
  float a = 1.0 - smoothstep(0.0, width, cell_border(p, time * 0.8));
  float b = 1.0 - smoothstep(0.0, width * 1.2, cell_border(p * 1.41 + vec2(4.7, 1.9), time * 1.1 + 2.0));
  float net = a * a + 0.55 * b * b + 0.8 * a * b;
  // Wider lines carry the same light spread thinner; past a pixel per line there is nothing left to show.
  return net * (kCausticLine / width) * (1.0 - smoothstep(0.25, 0.6, blur));
}

// Direction the sunlight travels in under the surface, bent down by entering the water.
// 1 where the sun reaches the surface at `at`, 0 where something nearer the sun covers it; a 3 by 3 average of
// the shadow map's texels, as lit.frag reads it.
float sunlit(vec3 at) {
  mat4 view = mat4(texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(0, 0), 0), texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(1, 0), 0),
                   texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(2, 0), 0), texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(3, 0), 0));
  vec4 extra = texelFetch(sampler2D(u_sun_view, u_sampler), ivec2(4, 0), 0);
  vec4 clip = view * vec4(at, 1.0);
  vec3 p = clip.xyz / clip.w;
  float edge = max(abs(p.x), abs(p.y));
  if (edge >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
    return 1.0;
  ivec2 size = textureSize(sampler2D(u_shadow, u_sampler), 0);
  ivec2 texel = ivec2(vec2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * vec2(size));
  float sum = 0.0;
  for (int y = -1; y <= 1; ++y)
    for (int x = -1; x <= 1; ++x)
      sum += step(p.z - extra.y * 2.0, texelFetch(sampler2D(u_shadow, u_sampler), clamp(texel + ivec2(x, y), ivec2(0), size - 1), 0).r);
  return mix(sum / 9.0, 1.0, smoothstep(0.85, 1.0, edge));
}

vec3 sun_in_water(vec3 to_sun) {
  return refract(-to_sun, vec3(0.0, 1.0, 0.0), 1.0 / kWaterIndex);
}

// How much brighter the caustics make what lies `depth` under the surface at `at`, as a factor on its light.
// `facing` is how squarely it faces the sunlight; `sun` the sunlight's colour.
float caustic_factor(vec3 at, float depth, float facing, vec3 to_sun, vec3 sun, float time, float pixel) {
  if (!has(kWaves) || to_sun.y <= 0.0 || depth <= 0.0)
    return 1.0;
  vec3 down = sun_in_water(to_sun);
  // The surface point the sunlight reaching `at` passed through; the net moves with the surface above.
  vec2 entry = at.xz + down.xz / min(down.y, -0.2) * depth;
  float strength = smoothstep(0.0, 0.25, depth) * exp(-depth / kCausticFade) * smoothstep(0.0, 0.3, to_sun.y);
  strength *= clamp(dot(sun, vec3(1.0 / 3.0)), 0.0, 1.0) * facing;
  float light = caustics(entry * kCausticScale, time, pixel * kCausticScale + depth * 0.012);
  return 1.0 + strength * (kCausticGain * light - kCausticDim);
}

// What reaches the eye from something `along` world units away through water, starting `eye_depth` under the
// surface: its light dims, red first, and the water's own glow, lit from above, takes its place.
vec3 through_water(vec3 seen, float along, float eye_depth, vec3 body_light) {
  vec3 absorb = kAbsorption * (0.5 + v_color.a) * kInsideClarity;
  vec3 kept = exp(-(absorb + kMurk) * along);
  vec3 glow = v_color.rgb * body_light * exp(-absorb * max(eye_depth, 0.0) * 0.5);
  return seen * kept + glow * (1.0 - kept);
}

vec2 screen_uv() {
  vec2 ndc = v_clip.xy / v_clip.w;
  return vec2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
}

// The eye is under water: the scene behind, fogged by the water in front of it.
vec4 under_water(vec3 sun, vec3 to_sun, vec3 body_light, float time) {
  vec3 eye = u_draw.eye_time.xyz;
  float level = v_depth;
  vec2 screen = screen_uv();
  vec3 ray = v_world - eye;
  float ray_length = length(ray);
  vec3 dir = ray / ray_length;
  // The depth buffer holds distance along the view axis; the quad's own gives the ratio to distance along `dir`.
  float along = scene_distance(screen) * ray_length / v_clip.w;
  vec3 at = eye + dir * along;
  vec3 seen = texture(sampler2D(u_scene, u_sampler), screen).rgb;

  vec3 normal = cross(dFdx(at), dFdy(at));
  normal *= sign(dot(normal, -dir));
  float facing = clamp(dot(normalize(normal + vec3(0.0, 1e-6, 0.0)), -sun_in_water(to_sun)) * 1.5, 0.0, 1.0);
  float pixel = length(fwidth(at.xz));
  seen *= caustic_factor(at, level - at.y, facing, to_sun, sun, time, pixel);
  return vec4(through_water(seen, along, level - eye.y, body_light), 1.0);
}

// The surface seen from under it, along `view` (towards the eye) with normal `n` (up).
vec3 from_below(vec3 n, vec3 view, float dist, vec2 bend, vec3 sky, vec3 sun, vec3 to_sun, vec3 body_light) {
  // Light from above leaves through the surface only inside a circle overhead; outside it the surface mirrors
  // the water below.
  vec3 out_dir = refract(-view, -n, kWaterIndex);
  float leaving = length(out_dir);
  float reflected = 1.0;
  vec3 above = sky;
  if (leaving > 0.0) {
    float cos_out = clamp(dot(out_dir, n), 0.0, 1.0);
    reflected = kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - cos_out, 5.0);
    if (has(kRefraction)) {
      vec2 at = screen_uv() + bend * kRefractionBend;
      above = texture(sampler2D(u_scene, u_sampler), clamp(at, vec2(0.001), vec2(0.999))).rgb;
    }
    above = above * 1.8 + sun * pow(max(dot(out_dir, to_sun), 0.0), 300.0) * 2.0;
  }
  vec3 mirrored = v_color.rgb * body_light * 0.4;
  vec3 colour = mix(above, mirrored, reflected);
  return through_water(colour, dist, v_world.y - u_draw.eye_time.y, body_light);
}

void main() {
  float time = u_draw.eye_time.w;
  // Shaded water keeps its sky light but loses the sun: no glint, dimmer depths and caustics.
  vec3 sun = u_draw.sun_far.rgb * (has(kShadow) ? sunlit(v_world) : 1.0);
  vec3 to_sun = normalize(u_draw.sun_dir_near.xyz);
  vec3 sky = u_draw.sky_flags.rgb;
  vec3 body_light = sky + sun * max(to_sun.y, 0.0) * 0.5;
  if (has(kUnderwater)) {
    out_color = under_water(sun, to_sun, body_light, time);
    return;
  }

  vec3 to_eye = u_draw.eye_time.xyz - v_world;
  float dist = length(to_eye);
  vec3 view = to_eye / max(dist, 1e-4);

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

  vec2 screen = screen_uv();
  // Nearer water bends what it shows over more of the screen.
  vec2 bend = n.xz / (1.0 + 0.02 * dist);

  if (view.y < 0.0) {
    out_color = vec4(from_below(n, view, dist, bend, sky, sun, to_sun, body_light), smoothstep(0.0, kShoreSoftness, v_depth));
    return;
  }

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
  // The bottom under this point, lit by the caustics before the water dims it on the way up.
  vec3 bottom = v_world - view * path;
  float caustic = caustic_factor(bottom, depth, 1.0, to_sun, sun, time, length(fwidth(bottom.xz)));

  float facing = clamp(dot(n, view), 0.0, 1.0);
  float fresnel = mix(kMirrorLeast, 1.0, kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - facing, 5.0));
  vec3 mirrored = sky;
  if (has(kReflection))
    mirrored = texture(sampler2D(u_reflection, u_sampler), clamp(screen + bend * kReflectionBend * 0.05, vec2(0.001), vec2(0.999))).rgb;
  // Stylised: the mirror takes on some of the water's blue, so a pale sky does not wash the water out.
  mirrored *= mix(vec3(1.0), v_color.rgb * kMirrorTint.y, kMirrorTint.x);

  vec3 scattered = v_color.rgb * body_light;
  vec3 kept = mix(kDeepClarity, vec3(1.0), exp(-kAbsorption * (0.5 + v_color.a) * path));

  vec3 half_way = normalize(to_sun + view);
  float shine = max(dot(n, half_way), 0.0);
  vec3 glint = sun * (pow(shine, 900.0) * 1.2 + pow(shine, 64.0) * 0.04) * step(0.0, to_sun.y);

  vec3 colour;
  float alpha;
  if (see_below) {
    colour = mix(below * caustic * kept + scattered * (1.0 - kept), mirrored, fresnel) + glint;
    alpha = 1.0;
  } else {
    // What is under the water is already on screen: blend over it, keeping as much as the water lets through.
    float through = (1.0 - fresnel) * dot(kept, vec3(1.0 / 3.0));
    alpha = 1.0 - through;
    // shortcut: the bottom's colour is unknown here, so the caustics' extra light is added as if it were mid grey.
    vec3 sunlit = sun * max(to_sun.y, 0.0) * 0.4 * (caustic - 1.0) * kept;
    colour = ((1.0 - fresnel) * (scattered * (1.0 - dot(kept, vec3(1.0 / 3.0))) + sunlit) + fresnel * mirrored + glint) / max(alpha, 1e-3);
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
