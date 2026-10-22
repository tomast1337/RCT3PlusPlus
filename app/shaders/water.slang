// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Water. Positions are in world space; the first texture coordinate is the depth of water over
// the ground at the vertex.
//
// The surface's slope comes from two drifting layers of a tiling height picture and from the ripples. Seen from
// above, it shows a blend of the mirrored scene and what lies under it, leaning on the mirror more than real
// water does so the park shows in it even from high up; light under the water is absorbed over the length of
// water it crosses (Beer-Lambert), red first, and replaced by light scattered in the water's own blue, so
// shallows are clear and depths turn blue, though even deep water lets a little of the bottom through. The
// bottom catches a moving net of sunlight focused by the waves (caustics), fading with depth, and the water
// between catches it too, so the net shows as rays slanting down through it. A faint, broken foam gathers in the
// shallows and on ripple crests.
//
// Seen from below, the surface shows the world above inside a bright circle overhead and mirrors the dark water
// outside it, where the light cannot leave (total internal reflection). With the eye under water the draw is
// instead one quad over the view: the scene behind it is fogged and coloured by the water each line of sight
// crosses, and what lies under the surface catches the caustics.
import common;

[[vk::binding(0, 0)]] Texture2D u_waves;
[[vk::binding(0, 1)]] SamplerState u_sampler;
[[vk::binding(0, 2)]] Texture2D u_reflection;
[[vk::binding(0, 3)]] Texture2D u_scene;
[[vk::binding(0, 4)]] Texture2D u_scene_depth;
[[vk::binding(0, 5)]] Texture2D u_ripples;
// render::SceneLight's shadow, as the Lit shaders read it (lit.slang).
[[vk::binding(0, 6)]] Texture2D u_shadow;
[[vk::binding(0, 7)]] Texture2D u_sun_view;

// render::WaterView and render::SceneLight, packed (render/vulkan/vulkan_device.cpp).
struct Draw
{
    float4x4 transform;
    float4 eye_time;     ///< Eye, seconds
    float4 sun_dir_near; ///< Towards the sun, near plane
    float4 sun_far;      ///< Sunlight, far plane
    float4 sky_flags;    ///< Skylight, flags
}
[[vk::push_constant]] ConstantBuffer<Draw> u_draw;

struct Varyings
{
    float4 position : SV_Position;
    [[vk::location(0)]] float3 world;
    [[vk::location(1)]] float4 color;
    [[vk::location(2)]] float depth; ///< Under water: the height of the surface over the eye
    [[vk::location(3)]] float4 clip;
}

[shader("vertex")]
Varyings vertex_main([[vk::location(0)]] float3 position, [[vk::location(1)]] float4 color, [[vk::location(2)]] float2 uv)
{
    const float4 clip = mul(u_draw.transform, float4(position, 1.0));
    return { clip, position, color, uv.x, clip };
}

static const uint kReflection = 1u;
static const uint kRefraction = 2u;
static const uint kWaves = 4u;
static const uint kFoam = 8u;
static const uint kRipples = 16u;
static const uint kUnderwater = 32u;
static const uint kShadow = 64u;
static const uint kShafts = 128u;

static const float kFresnelBase = 0.02;                       // Reflectance seen straight on, for a refractive index near 1.33
static const float kMirrorLeast = 0.42;                       // The least the mirror shows, looking straight down
static const float2 kMirrorTint = float2(0.45, 1.8);          // How much of the water's colour the mirror takes, and its strength
static const float kWaterIndex = 1.33;
static const float3 kAbsorption = float3(0.80, 0.30, 0.12);   // Per world unit of water crossed
static const float3 kMurk = float3(0.015);                    // Light scattered out of a line of sight, per unit, seen from inside
static const float kInsideClarity = 0.2;                      // Share of kAbsorption a swimmer's eye sees, so the fog is not a wall
static const float3 kDeepClarity = float3(0.05, 0.10, 0.16);  // The least of the bottom's light that still shows, blue keeps most
static const float2 kWaveScale = float2(1.0 / 11.0, 1.0 / 3.7); // Picture repeats per world unit: broad layer, fine layer
static const float2 kWaveDriftA = float2(0.010, 0.006);       // Picture widths per second
static const float2 kWaveDriftB = float2(-0.016, 0.012);
static const float kWaveHeight = 0.08;                        // World units the picture's full range stands for
static const float kWaveCalm = 80.0;                          // Waves fade over this distance, before they shimmer
static const float kReflectionBend = 0.6;                     // Screen shift per unit of slope
static const float kRefractionBend = 0.35;
static const float kShoreSoftness = 0.08;                     // Depth over which the water's edge fades in
static const float kFoamDepth = 0.35;                         // Shallows foam gathers in
static const float kFoamMost = 0.28;                          // The most of a point foam covers
static const float kCrestLow = 0.015;                         // Ripple heights foam starts and is full on
static const float kCrestHigh = 0.050;
static const float kCausticScale = 0.55;                      // Cells of the net per world unit
static const float kCausticLine = 0.09;                       // Width of a bright line, in cells
static const float kCausticFade = 7.0;                        // Depth over which caustics fade to a third
static const float kCausticGain = 2.2;                        // Light a full line adds, as a share of the bottom's own
static const float kCausticDim = 0.25;                        // Light the bottom loses between the lines, where the net drew it
static const float kShaftReach = 12.0;                        // The most water a line of sight is read through
static const float kShaftGain = 0.5;                          // Light a full line scatters to the eye, per world unit of water

bool has(uint flag)
{
    return (uint(u_draw.sky_flags.w) & flag) != 0u;
}

// Slope of the height picture at `p`, in height per picture width.
float2 picture_slope(float2 p)
{
    const float2 size = size_of(u_waves);
    const float2 step = 1.0 / size;
    const float h = u_waves.Sample(u_sampler, p).r;
    const float hx = u_waves.Sample(u_sampler, p + float2(step.x, 0.0)).r;
    const float hy = u_waves.Sample(u_sampler, p + float2(0.0, step.y)).r;
    return float2(hx - h, hy - h) * size;
}

// The ripples' slope in x and z, and how high the highest crest stands here.
float3 ripples(float2 at)
{
    float2 slope = float2(0.0);
    float crest = 0.0;
    const int count = texels_of(u_ripples).x;
    for (int i = 0; i < count; ++i)
    {
        const float4 ring = u_ripples.Load(int3(i, 0, 0));
        if (ring.w <= 0.0)
            continue;
        const float4 shape = u_ripples.Load(int3(i, 1, 0));
        const float2 d = at - ring.xy;
        const float r = length(d);
        const float x = r - ring.z;
        const float u = x / shape.y;
        if (abs(u) > 3.0)
            continue;
        const float envelope = exp(-u * u);
        const float c = cos(shape.x * x);
        const float s = sin(shape.x * x);
        const float h = ring.w * envelope * c;
        const float dh = ring.w * envelope * (-2.0 * x / (shape.y * shape.y) * c - shape.x * s);
        slope += dh * d / max(r, 1e-3);
        crest = max(crest, h);
    }
    return float3(slope, crest);
}

// Distance along the view axis of a depth read back from the depth buffer.
float view_distance(float depth)
{
    const float near = u_draw.sun_dir_near.w;
    const float far = u_draw.sun_far.w;
    return near * far / (far - depth * (far - near));
}

float scene_distance(float2 uv)
{
    const float2 size = size_of(u_scene_depth);
    const int2 texel = int2(clamp(uv * size, float2(0.0), size - 1.0));
    return view_distance(u_scene_depth.Load(int3(texel, 0)).r);
}

// Where the cell `c` of a jittered grid has its point at `time`: a fixed random place in the cell, circling it.
float2 cell_point(float2 c, float time)
{
    const uint2 u = uint2(int2(c));
    uint h = (u.x * 668265261u) ^ (u.y * 374761393u);
    h = (h ^ (h >> 13u)) * 739982445u;
    h ^= h >> 16u;
    const float2 r = float2(float(h & 65535u), float(h >> 16u)) / 65535.0;
    return c + 0.5 + 0.36 * sin(time * (0.5 + 0.4 * r) + 6.2832 * r.yx);
}

// How far `p` is from the nearest border between the cells of the grid's points: 0 on a border.
float cell_border(float2 p, float time)
{
    const float2 base = floor(p);
    float first = 9.0;
    float second = 9.0;
    for (int y = -1; y <= 1; ++y)
    {
        for (int x = -1; x <= 1; ++x)
        {
            const float2 q = cell_point(base + float2(x, y), time) - p;
            const float d = dot(q, q);
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
float caustics(float2 p, float time, float blur)
{
    const float2 warp = float2(u_waves.Sample(u_sampler, p * 0.13 + float2(0.011, 0.004) * time).r,
                               u_waves.Sample(u_sampler, p * 0.11 + float2(0.37, 0.61) - float2(0.006, 0.010) * time).r);
    p += (warp - 0.5) * 1.2;
    const float width = kCausticLine + blur;
    const float a = 1.0 - smoothstep(0.0, width, cell_border(p, time * 0.8));
    const float b = 1.0 - smoothstep(0.0, width * 1.2, cell_border(p * 1.41 + float2(4.7, 1.9), time * 1.1 + 2.0));
    const float net = a * a + 0.55 * b * b + 0.8 * a * b;
    // Wider lines carry the same light spread thinner; past a pixel per line there is nothing left to show.
    return net * (kCausticLine / width) * (1.0 - smoothstep(0.25, 0.6, blur));
}

// 1 where the sun reaches the surface at `at`, 0 where something nearer the sun covers it; a 3 by 3 average of
// the shadow map's texels, as lit.slang reads it.
float sunlit(float3 at)
{
    const float4x4 view = from_columns(u_sun_view.Load(int3(0, 0, 0)), u_sun_view.Load(int3(1, 0, 0)), u_sun_view.Load(int3(2, 0, 0)), u_sun_view.Load(int3(3, 0, 0)));
    const float4 extra = u_sun_view.Load(int3(4, 0, 0));
    const float4 clip = mul(view, float4(at, 1.0));
    const float3 p = clip.xyz / clip.w;
    const float edge = max(abs(p.x), abs(p.y));
    if (edge >= 1.0 || p.z <= 0.0 || p.z >= 1.0)
        return 1.0;
    const int2 size = texels_of(u_shadow);
    const int2 texel = int2(float2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * float2(size));
    float sum = 0.0;
    for (int y = -1; y <= 1; ++y)
        for (int x = -1; x <= 1; ++x)
            sum += step(p.z - extra.y * 2.0, u_shadow.Load(int3(clamp(texel + int2(x, y), int2(0), size - 1), 0)).r);
    return lerp(sum / 9.0, 1.0, smoothstep(0.85, 1.0, edge));
}

// Direction the sunlight travels in under the surface, bent down by entering the water.
float3 sun_in_water(float3 to_sun)
{
    return refract(-to_sun, float3(0.0, 1.0, 0.0), 1.0 / kWaterIndex);
}

// How much brighter the caustics make what lies `depth` under the surface at `at`, as a factor on its light.
// `facing` is how squarely it faces the sunlight; `sun` the sunlight's colour.
float caustic_factor(float3 at, float depth, float facing, float3 to_sun, float3 sun, float time, float pixel)
{
    if (!has(kWaves) || to_sun.y <= 0.0 || depth <= 0.0)
        return 1.0;
    const float3 down = sun_in_water(to_sun);
    // The surface point the sunlight reaching `at` passed through; the net moves with the surface above.
    const float2 entry = at.xz + down.xz / min(down.y, -0.2) * depth;
    float strength = smoothstep(0.0, 0.25, depth) * exp(-depth / kCausticFade) * smoothstep(0.0, 0.3, to_sun.y);
    strength *= clamp(dot(sun, float3(1.0 / 3.0)), 0.0, 1.0) * facing;
    const float light = caustics(entry * kCausticScale, time, pixel * kCausticScale + depth * 0.012);
    return 1.0 + strength * (kCausticGain * light - kCausticDim);
}

// Sunlight the water scatters towards the eye along `dir` from `from`, over `span` world units, under a surface at
// height `level`: each point is lit as the waves' bright net falls there, so the light reads as rays slanting
// down from the surface along the sunlight. It dims crossing water on its way down and on to the eye (`absorb`
// per unit). It reads `steps` points along the line. `blur` is how many cells of the net a pixel spans at `from`,
// growing by `spread` a world unit away; `pixel` the pixel, which shifts where it reads.
float3 water_shafts(float3 from, float3 dir, float span, float level, float3 absorb, float3 to_sun, float3 sun, float time, int steps, float blur, float spread, float2 pixel)
{
    if (!has(kShafts) || !has(kWaves) || to_sun.y <= 0.0 || span <= 0.0)
        return float3(0.0);
    const float3 down = sun_in_water(to_sun);
    const float stride = min(span, kShaftReach) / float(steps);
    // A different start on each pixel, so the reads blur together instead of banding (interleaved gradient noise,
    // Jimenez, "Next generation post processing in Call of Duty: Advanced Warfare", SIGGRAPH 2014).
    const float jitter = frac(52.9829189 * frac(dot(pixel, float2(0.06711056, 0.00583715))));
    float3 sum = float3(0.0);
    for (int i = 0; i < steps; ++i)
    {
        const float t = (float(i) + jitter) * stride;
        const float3 q = from + dir * t;
        const float depth = level - q.y;
        if (depth <= 0.0)
            continue;
        // As caustic_factor: the surface point the sunlight reaching `q` passed through.
        const float2 entry = q.xz + down.xz / min(down.y, -0.2) * depth;
        const float light = caustics(entry * kCausticScale, time, blur + spread * t + depth * 0.02);
        sum += light * exp(-absorb * (depth / max(-down.y, 0.2) + t));
    }
    return sun * sum * stride * kShaftGain * smoothstep(0.0, 0.3, to_sun.y);
}

// What reaches the eye from something `along` world units away through water, starting `eye_depth` under the
// surface: its light dims, red first, and the water's own glow, lit from above, takes its place.
float3 through_water(Varyings v, float3 seen, float along, float eye_depth, float3 body_light)
{
    const float3 absorb = kAbsorption * (0.5 + v.color.a) * kInsideClarity;
    const float3 kept = exp(-(absorb + kMurk) * along);
    const float3 glow = v.color.rgb * body_light * exp(-absorb * max(eye_depth, 0.0) * 0.5);
    return seen * kept + glow * (1.0 - kept);
}

float2 screen_uv(Varyings v)
{
    const float2 ndc = v.clip.xy / v.clip.w;
    return float2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
}

// The eye is under water: the scene behind, fogged by the water in front of it.
float4 under_water(Varyings v, float3 sun, float3 to_sun, float3 body_light, float time)
{
    const float3 eye = u_draw.eye_time.xyz;
    const float level = v.depth;
    const float2 screen = screen_uv(v);
    const float3 ray = v.world - eye;
    const float ray_length = length(ray);
    const float3 dir = ray / ray_length;
    // The depth buffer holds distance along the view axis; the quad's own gives the ratio to distance along `dir`.
    const float along = scene_distance(screen) * ray_length / v.clip.w;
    const float3 at = eye + dir * along;
    float3 seen = u_scene.Sample(u_sampler, screen).rgb;

    float3 normal = cross(ddx(at), ddy(at));
    normal *= sign(dot(normal, -dir));
    const float facing = clamp(dot(normalize(normal + float3(0.0, 1e-6, 0.0)), -sun_in_water(to_sun)) * 1.5, 0.0, 1.0);
    const float pixel = length(fwidth(at.xz));
    seen *= caustic_factor(at, level - at.y, facing, to_sun, sun, time, pixel);
    const float3 absorb = kAbsorption * (0.5 + v.color.a) * kInsideClarity;
    // A swimmer looks along the rays through much more water: more reads, and softer lines that soften further
    // away, so they do not break into grain.
    const float3 shafts = water_shafts(eye, dir, along, level, absorb, to_sun, sun, time, 16, 0.12, 0.012, v.position.xy);
    return float4(through_water(v, seen, along, level - eye.y, body_light) + shafts, 1.0);
}

// The surface seen from under it, along `view` (towards the eye) with normal `n` (up).
float3 from_below(Varyings v, float3 n, float3 view, float dist, float2 bend, float3 sky, float3 sun, float3 to_sun, float3 body_light)
{
    // Light from above leaves through the surface only inside a circle overhead; outside it the surface mirrors
    // the water below.
    const float3 out_dir = refract(-view, -n, kWaterIndex);
    const float leaving = length(out_dir);
    float reflected = 1.0;
    float3 above = sky;
    if (leaving > 0.0)
    {
        const float cos_out = clamp(dot(out_dir, n), 0.0, 1.0);
        reflected = kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - cos_out, 5.0);
        if (has(kRefraction))
        {
            const float2 at = screen_uv(v) + bend * kRefractionBend;
            above = u_scene.Sample(u_sampler, clamp(at, float2(0.001), float2(0.999))).rgb;
        }
        above = above * 1.8 + sun * pow(max(dot(out_dir, to_sun), 0.0), 300.0) * 2.0;
    }
    const float3 mirrored = v.color.rgb * body_light * 0.4;
    const float3 colour = lerp(above, mirrored, reflected);
    return through_water(v, colour, dist, v.world.y - u_draw.eye_time.y, body_light);
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    const float time = u_draw.eye_time.w;
    // Shaded water keeps its sky light but loses the sun: no glint, dimmer depths and caustics.
    const float3 sun = u_draw.sun_far.rgb * (has(kShadow) ? sunlit(v.world) : 1.0);
    const float3 to_sun = normalize(u_draw.sun_dir_near.xyz);
    const float3 sky = u_draw.sky_flags.rgb;
    const float3 body_light = sky + sun * max(to_sun.y, 0.0) * 0.5;
    if (has(kUnderwater))
        return under_water(v, sun, to_sun, body_light, time);

    const float3 to_eye = u_draw.eye_time.xyz - v.world;
    const float dist = length(to_eye);
    const float3 view = to_eye / max(dist, 1e-4);

    float2 slope = float2(0.0);
    if (has(kWaves))
    {
        const float calm = 1.0 / (1.0 + dist / kWaveCalm);
        const float2 broad = picture_slope(v.world.xz * kWaveScale.x + time * kWaveDriftA) * kWaveScale.x;
        const float2 fine = picture_slope(v.world.xz * kWaveScale.y + time * kWaveDriftB) * kWaveScale.y;
        slope += (broad + 0.5 * fine) * kWaveHeight * calm;
    }
    const float3 rip = has(kRipples) ? ripples(v.world.xz) : float3(0.0);
    slope += rip.xy;
    const float3 n = normalize(float3(-slope.x, 1.0, -slope.y));

    const float2 screen = screen_uv(v);
    // Nearer water bends what it shows over more of the screen.
    const float2 bend = n.xz / (1.0 + 0.02 * dist);

    if (view.y < 0.0)
        return float4(from_below(v, n, view, dist, bend, sky, sun, to_sun, body_light), smoothstep(0.0, kShoreSoftness, v.depth));

    // How deep the water is under this point, and how much of it the line of sight crosses.
    float depth = v.depth;
    float path = depth / max(view.y, 0.15);
    float3 below = float3(0.0);
    const bool see_below = has(kRefraction);
    if (see_below)
    {
        const float water = v.clip.w;
        float2 bent = screen + bend * kRefractionBend * clamp(depth, 0.0, 1.0);
        float behind = scene_distance(bent);
        // What stands in front of the water must not be pulled into it.
        if (behind < water)
        {
            bent = screen;
            behind = scene_distance(screen);
        }
        path = max(behind - water, 0.0) * dist / max(water, 1e-4);
        depth = path * view.y;
        below = u_scene.Sample(u_sampler, clamp(bent, float2(0.001), float2(0.999))).rgb;
    }
    // The bottom under this point, lit by the caustics before the water dims it on the way up.
    const float3 bottom = v.world - view * path;
    const float caustic = caustic_factor(bottom, depth, 1.0, to_sun, sun, time, length(fwidth(bottom.xz)));
    const float3 shafts = water_shafts(v.world, -view, path, v.world.y, kAbsorption * (0.5 + v.color.a), to_sun, sun, time, 8, length(fwidth(v.world.xz)) * kCausticScale, 0.0, v.position.xy);

    const float facing = clamp(dot(n, view), 0.0, 1.0);
    const float fresnel = lerp(kMirrorLeast, 1.0, kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - facing, 5.0));
    float3 mirrored = sky;
    if (has(kReflection))
        mirrored = u_reflection.Sample(u_sampler, clamp(screen + bend * kReflectionBend * 0.05, float2(0.001), float2(0.999))).rgb;
    // Stylised: the mirror takes on some of the water's blue, so a pale sky does not wash the water out.
    mirrored *= lerp(float3(1.0), v.color.rgb * kMirrorTint.y, kMirrorTint.x);

    const float3 scattered = v.color.rgb * body_light;
    const float3 kept = lerp(kDeepClarity, float3(1.0), exp(-kAbsorption * (0.5 + v.color.a) * path));

    const float3 half_way = normalize(to_sun + view);
    const float shine = max(dot(n, half_way), 0.0);
    const float3 glint = sun * (pow(shine, 900.0) * 1.2 + pow(shine, 64.0) * 0.04) * step(0.0, to_sun.y);

    float3 colour;
    float alpha;
    if (see_below)
    {
        colour = lerp(below * caustic * kept + scattered * (1.0 - kept) + shafts, mirrored, fresnel) + glint;
        alpha = 1.0;
    }
    else
    {
        // What is under the water is already on screen: blend over it, keeping as much as the water lets through.
        const float through = (1.0 - fresnel) * dot(kept, float3(1.0 / 3.0));
        alpha = 1.0 - through;
        // shortcut: the bottom's colour is unknown here, so the caustics' extra light is added as if it were mid grey.
        const float3 sunlit = sun * max(to_sun.y, 0.0) * 0.4 * (caustic - 1.0) * kept;
        colour = ((1.0 - fresnel) * (scattered * (1.0 - dot(kept, float3(1.0 / 3.0))) + sunlit + shafts) + fresnel * mirrored + glint) / max(alpha, 1e-3);
    }

    if (has(kFoam))
    {
        const float shallows = (1.0 - smoothstep(0.0, kFoamDepth, depth)) * smoothstep(0.0, 0.04, depth);
        const float grain = u_waves.Sample(u_sampler, v.world.xz * 0.31 + float2(0.020, -0.013) * time).r;
        const float grain2 = u_waves.Sample(u_sampler, v.world.xz * 0.77 - float2(0.011, 0.017) * time).r;
        const float broken = smoothstep(0.55, 0.9, 0.6 * grain + 0.4 * grain2 + 0.3 * shallows);
        float foam = max(shallows * broken, 0.6 * smoothstep(kCrestLow, kCrestHigh, rip.z));
        foam *= kFoamMost / (1.0 + dist / 150.0);
        colour = lerp(colour, body_light * 0.95, foam);
        alpha = lerp(alpha, 1.0, foam);
    }

    // The edge fades in over the first bit of depth, so the shore has no hard line; far off, water fades into the
    // horizon drawn behind it before the far plane cuts it, as the land beyond the park does.
    alpha *= smoothstep(0.0, kShoreSoftness, depth) * (1.0 - smoothstep(0.6, 0.95, dist / u_draw.sun_far.w));
    return float4(colour, alpha);
}
