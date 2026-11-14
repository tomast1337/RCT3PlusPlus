// SPDX-License-Identifier: GPL-3.0-or-later
//
// render::Shader::Waterfall: the park's waterfalls (park/waterfall_mesh.hpp), a draw for each part, all from the
// wave picture alone. The mesh stands still; everything that moves is worked out here from the park's clock.
//
// The sheet takes its lakes' colour and is streaked white by noise that rides down with the water: read by the
// time each drop left the edge, so the streaks stretch as the water speeds up. The water leaves the edge smooth
// and glassy, catching the sky, and breaks into white on the way down; a tall fall ends mostly white, a small one
// stays blue between its streaks. Streaks fixed across the sheet (strands) keep its look steady as it runs. From
// High it tears open low down, and at Ultra the scene behind shows through the tears, bent.
//
// Foam lies on the lower lake round where the water lands: a churning middle, patches thinning outward and, from
// Medium, broken rings running out. At Ultra the lake round it ripples in rings.
//
// A tall fall raises mist: puffs facing the eye, each rising, growing and thinning in a loop of its own, softened
// where they meet what stands behind them (soft particles) when the scene's depth is given. Mist is lit more when
// seen towards the sun, as fine drops scatter light mostly forward; at Ultra each puff is shadowed where the sun's
// shadow says. At Ultra droplets are thrown up where the water lands, each flying on a loop of its own.
import common;

[[vk::binding(0, 0)]] Texture2D u_waves;
[[vk::binding(0, 1)]] SamplerState u_sampler;
// The opaque scene behind and its depth (render::Shader::Water's), when the flags say so.
[[vk::binding(0, 3)]] Texture2D u_scene;
[[vk::binding(0, 4)]] Texture2D u_scene_depth;
// render::SceneLight's shadow, as the Lit shaders read it (lit.slang).
[[vk::binding(0, 6)]] Texture2D u_shadow;
[[vk::binding(0, 7)]] Texture2D u_sun_view;

// render::WaterView and render::SceneLight, packed as for render::Shader::Water (render/vulkan/device.cpp).
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
    [[vk::location(2)]] float4 clip;
    [[vk::location(3)]] float2 uv;
    [[vk::location(4)]] float3 normal;
    [[vk::location(5)]] float2 mist; ///< Of mist: its size as drawn, the share of the sun reaching it
}

static const uint kRefraction = 2u;                     // The scene behind and its depth are given
static const uint kShadow = 64u;
// Bits 9 and 10: the part (park::FallPart). Bits 11 and 12: the detail (park::WaterfallDetail).
static const uint kSheet = 0u;
static const uint kFoam = 1u;
static const uint kMist = 2u;
static const uint kMedium = 1u;
static const uint kHigh = 2u;
static const uint kUltra = 3u;

// All chosen by eye.
static const float kGravity = 9.81;                     // World units are metres
static const float kFresnelBase = 0.02;                 // Reflectance seen straight on, for a refractive index near 1.33
static const float kMirrorLeast = 0.42;                 // The least of the sky the sheet shows, as the lake's mirror
static const float2 kMirrorTint = float2(0.45, 1.8);    // How much of the water's colour the mirror takes, and its strength
static const float kStrandAcross = 0.09;                // Picture widths per world unit across: strands that stay where they are
static const float2 kStreakAcross = float2(0.23, 0.61); // And of the streaks riding down: broad, fine
static const float2 kStreakAlong = float2(0.22, 0.5);   // Picture widths per second of falling: several over any sheet, so its share of white holds
static const float kWhiteAfter = 0.7;                   // Seconds past the edge by which the sheet has broken up
static const float2 kWhiteShare = float2(0.08, 0.35);   // Share of the sheet that is white: at the edge, broken up
static const float2 kTallDrop = float2(25.0, 65.0);     // Drops over which a fall turns from streaked to mostly white
static const float kTallWhite = 0.45;                   // How much more of a tall fall's broken water is white
static const float kNoiseSpread = 0.55;                 // The noise's spread round its middle, turning a share into a level
static const float2 kStreakSoft = float2(0.09, 0.035);  // Softness of a streak's edge, Low and above, in noise
static const float kSheetAlpha = 0.93;
static const float kSheetGlow = 1.5;                    // The sheet's own colour is this much brighter than the lake's, which shows its bottom too
static const float kGapMost = 0.8;                      // How clear the tears low on a sheet get
static const float kGapBend = 0.012;                    // Screen widths the scene seen through a tear is bent by
static const float kWhiteAlbedo = 0.8;                  // Of the light reaching white water, the share it sends back
static const float kWhiteSun = 0.85;                    // Of the sun, the share reaching the churned surface's every way
static const float kWhiteSunLeast = 0.4;                // Of that, what reaches it with the sun at the horizon
static const float kFoamWhite = 0.82;                   // How far foam is from the lake's colour to white
static const float kFoamRingLength = 2.6;               // World units between the foam's rings
static const float kFoamRingRate = 0.45;                // Rings a second
static const float kFoamRingWidth = 0.16;               // Share of kFoamRingLength a ring covers
static const float kRippleLength = 1.3;                 // World units between the lake's ripples round the foam
static const float kRippleRate = 0.8;                   // Ripples a second
static const float kRippleSlope = 0.12;                 // Their steepest slope
static const float kMistRate = 0.11;                    // Loops a second of a puff
static const float kMistRise = 0.9;                     // Sizes a puff rises over its loop
static const float kMistSoft = 1.5;                     // World units in front of what is behind it over which a puff fades in
static const float kMistAlpha = 0.38;                   // The most one puff covers
static const float kMistAbove = 0.65;                   // How much thinner mist is seen from straight above, so the foam shows
static const float kMistBright = 1.15;                  // Fine drops send back more of the light than churned water
static const float kMistForward = 0.4;                  // Extra light mist scatters towards an eye looking at the sun through it
static const float kDropletRest = 0.4;                  // Of its flight, how long a droplet waits before it is thrown again
static const float kDropletStretch = 0.12;              // How much longer than wide a droplet is drawn per unit of speed

uint part()
{
    return (uint(u_draw.sky_flags.w) >> 9u) & 3u;
}

uint detail()
{
    return (uint(u_draw.sky_flags.w) >> 11u) & 3u;
}

bool has(uint flag)
{
    return (uint(u_draw.sky_flags.w) & flag) != 0u;
}

// 1 where the sun reaches `at`, 0 where something nearer the sun covers it; one texel of the shadow map.
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
    const int2 texel = clamp(int2(float2(0.5 + 0.5 * p.x, 0.5 - 0.5 * p.y) * float2(size)), int2(0), size - 1);
    return lerp(step(p.z - extra.y * 2.0, u_shadow.Load(int3(texel, 0)).r), 1.0, smoothstep(0.85, 1.0, edge));
}

[shader("vertex")]
Varyings vertex_main([[vk::location(0)]] float3 position, [[vk::location(1)]] float4 color, [[vk::location(2)]] float2 uv, [[vk::location(3)]] float3 normal)
{
    if (part() != kMist)
    {
        const float4 clip = mul(u_draw.transform, float4(position, 1.0));
        return { clip, position, color, clip, uv, normal, float2(0.0) };
    }
    const float time = u_draw.eye_time.w;
    float3 middle = position;
    float3 right;
    float3 up;
    float2 size;
    float4 colour;
    if (normal.z < 0.0)
    {
        // A droplet: thrown at its speed, it flies and falls back, waits, and is thrown again.
        const float3 speed = color.rgb;
        const float flight = 2.0 * max(speed.y, 0.1) / kGravity;
        const float loop = flight * (1.0 + kDropletRest);
        const float t = frac(time / loop + normal.y) * loop;
        const float flying = step(t, flight);
        const float3 moving = speed - float3(0.0, kGravity * t, 0.0);
        middle = position + speed * t - float3(0.0, 0.5 * kGravity * t * t, 0.0);
        // Drawn long along the way it moves, as the eye sees it.
        const float3 away = normalize(middle - u_draw.eye_time.xyz);
        const float3 along = moving - away * dot(moving, away);
        up = dot(along, along) > 1e-6 ? normalize(along) : float3(0.0, 1.0, 0.0);
        right = normalize(cross(up, away) + float3(1e-6, 0.0, 0.0));
        size = normal.x * float2(1.0, 1.0 + kDropletStretch * length(moving)) * flying;
        colour = float4(1.0, 1.0, 1.0, flying * (1.0 - pow(t / flight, 4.0)));
    }
    else
    {
        // A puff: it rises and grows over its loop, fading in and thinning out, and faces the eye.
        const float age = frac(time * kMistRate + normal.y);
        middle = position + float3(0.0, age * normal.x * kMistRise, 0.0);
        const float3 away = middle - u_draw.eye_time.xyz;
        right = normalize(cross(away, float3(0.0, 1.0, 0.0)) + float3(1e-6, 0.0, 0.0));
        up = normalize(cross(right, away));
        size = float2(normal.x * (0.55 + 0.9 * age));
        colour = float4(color.rgb, normal.z * smoothstep(0.0, 0.15, age) * (1.0 - age * age));
    }
    const float3 at = middle + right * uv.x * size.x + up * uv.y * size.y;
    const float4 clip = mul(u_draw.transform, float4(at, 1.0));
    // Shadowed by its middle, a few reads a puff rather than many a pixel.
    const float lit = has(kShadow) && detail() >= kUltra ? sunlit(middle) : 1.0;
    return { clip, at, colour, clip, uv, normal, float2(size.x, lit) };
}

// Distance along the view axis of a depth read back from the depth buffer.
float view_distance(float depth)
{
    const float near = u_draw.sun_dir_near.w;
    const float far = u_draw.sun_far.w;
    return near * far / (far - depth * (far - near));
}

float2 screen_uv(Varyings v)
{
    const float2 ndc = v.clip.xy / v.clip.w;
    return float2(0.5 + 0.5 * ndc.x, 0.5 - 0.5 * ndc.y);
}

float scene_distance(float2 uv)
{
    const float2 size = size_of(u_scene_depth);
    const int2 texel = int2(clamp(uv * size, float2(0.0), size - 1.0));
    return view_distance(u_scene_depth.Load(int3(texel, 0)).r);
}

// The light white water sends back, `sun` already shadowed: churned water faces every way, so it takes some sun
// even when the sun is low, and less than all of it; half way to grey of the same brightness, so it is not as blue
// as the sky that lights it.
float3 white_light(float3 sky, float3 sun, float3 to_sun)
{
    const float3 light = kWhiteAlbedo * (sky + sun * (kWhiteSunLeast + (1.0 - kWhiteSunLeast) * max(to_sun.y, 0.0)) * kWhiteSun);
    return lerp(light, float3(dot(light, float3(1.0 / 3.0))), 0.5);
}

// Fades out towards the far plane, before the haze would leave it standing out.
float far_fade(float dist)
{
    return 1.0 - smoothstep(0.6, 0.95, dist / u_draw.sun_far.w);
}

// The sheet. uv: units across from its middle, seconds since the water left the edge; normal: half its width,
// seconds the fall takes, its drop; colour: its lakes', alpha 0 where it starts behind the edge.
float4 sheet(Varyings v, float3 view, float dist, float3 sky, float3 body_light, float3 white, float time, uint level)
{
    const float half_width = v.normal.x;
    const float whole = max(v.normal.y, 1e-3);
    const float tall = smoothstep(kTallDrop.x, kTallDrop.y, v.normal.z);
    const float since = v.uv.y;
    // Noise read by when the water left the edge moves down with it, and stretches as it speeds up.
    const float parcel = since - time;
    const float strand = u_waves.Sample(u_sampler, float2(v.uv.x * kStrandAcross, 0.31)).r;
    const float broad = u_waves.Sample(u_sampler, float2(v.uv.x * kStreakAcross.x, parcel * kStreakAlong.x)).r;
    float fine = broad;
    // Low, with no fine streaks, leans less on the strands, which alone would stand as even bars.
    float value = 0.75 * broad + 0.25 * strand;
    if (level >= kMedium)
    {
        fine = u_waves.Sample(u_sampler, float2(v.uv.x * kStreakAcross.y + 0.37, parcel * kStreakAlong.y + 0.11)).r;
        value = 0.4 * broad + 0.3 * fine + 0.3 * strand;
    }
    const float broken = smoothstep(0.0, kWhiteAfter, since);
    const float low = saturate(since / whole);
    // The share of white, as a level in the noise: the more white, the lower.
    const float share = saturate(lerp(kWhiteShare.x, kWhiteShare.y + kTallWhite * tall, broken));
    const float cut = 0.5 + (0.5 - share) * kNoiseSpread;
    const float soft = max(fwidth(value) * 1.5, level >= kMedium ? kStreakSoft.y : kStreakSoft.x);
    float whiteness = smoothstep(cut - soft, cut + soft, value);
    // Between the streaks the broken water is milky, more so the taller the fall, and it lands in white.
    whiteness = lerp((0.1 + 0.45 * tall) * broken, 1.0, whiteness);
    whiteness = max(whiteness, smoothstep(0.85, 1.0, low) * 0.85);

    float3 n = cross(ddx(v.world), ddy(v.world));
    n = normalize(n + float3(0.0, 1e-9, 0.0));
    const float facing = abs(dot(n, view));
    const float fresnel = kFresnelBase + (1.0 - kFresnelBase) * pow(1.0 - facing, 5.0);
    // As the lake: its own colour under the sky it mirrors, the mirror tinted its way.
    const float3 mirror = sky * lerp(float3(1.0), v.color.rgb * kMirrorTint.y, kMirrorTint.x);
    float3 water = lerp(v.color.rgb * body_light * kSheetGlow, mirror, lerp(kMirrorLeast, 1.0, fresnel));
    // Thicker water is darker, so the sheet has depth between its streaks.
    water *= 1.1 - 0.25 * smoothstep(0.3, 0.7, value) * (1.0 - whiteness);
    // The smooth water bending over the edge catches the sky.
    if (level >= kMedium)
        water += mirror * 0.5 * exp(-since * since / 0.02);
    float3 colour = lerp(water, white * (0.9 + 0.15 * fine), whiteness);

    float alpha = kSheetAlpha;
    if (level >= kHigh)
    {
        // Low on the sheet the thin water between the streaks tears open.
        const float tear = lerp(0.1, 0.5, low);
        const float gap = (1.0 - smoothstep(tear - 0.12, tear, 0.5 * fine + 0.5 * broad)) * smoothstep(0.15, 0.6, low) * (1.0 - whiteness);
        if (level >= kUltra && has(kRefraction))
        {
            // The scene behind, bent by the water round the tear, takes its place.
            const float2 bend = (float2(fine, broad) - 0.5) * kGapBend;
            const float3 behind = u_scene.Sample(u_sampler, screen_uv(v) + bend).rgb;
            colour = lerp(colour, behind * lerp(float3(1.0), v.color.rgb * 1.6, 0.25), kGapMost * gap);
        }
        else
            alpha *= 1.0 - kGapMost * gap;
    }
    // Ragged sides, more so once it has broken up.
    const float inside = half_width - abs(v.uv.x);
    alpha *= smoothstep(0.0, 0.25 + 1.6 * broken * (1.0 - broad), inside);
    return float4(colour, alpha * v.color.a * far_fade(dist));
}

// The foam on the lower lake. uv: units across from the middle of where the water lands, units out from there;
// normal: half the fall's width, its drop, how far the foam reaches; colour: the lake's.
float4 foam(Varyings v, float3 view, float dist, float3 sky, float3 sun, float3 to_sun, float3 body_light, float3 white, float time, uint level)
{
    // How far from the line the water lands on.
    const float away = length(float2(max(abs(v.uv.x) - v.normal.x, 0.0), v.uv.y));
    const float reach = max(v.normal.z, 1e-3);
    if (away >= reach)
        discard;
    const float broad = u_waves.Sample(u_sampler, v.world.xz * 0.06 + float2(0.006, -0.004) * time).r;
    // Nearer or further by the noise, so the foam's outline is not the landing line's.
    const float out = away / reach + 0.5 * (broad - 0.5);
    float fine = broad;
    if (level >= kMedium)
        fine = u_waves.Sample(u_sampler, v.world.xz * 0.15 - float2(0.009, 0.007) * time).r;
    const float grain = 0.6 * broad + 0.4 * fine;
    // Churned white where the water lands, boiling up in lumps, then patches that thin out.
    const float churn = 1.0 - smoothstep(-0.1, 0.4, out);
    const float threshold = lerp(0.46, 0.8, sqrt(saturate(out)));
    const float soft = max(fwidth(grain) * 1.5, level >= kMedium ? 0.05 : 0.12);
    float cover = smoothstep(threshold, threshold + soft, grain + 0.22 * churn * churn);
    if (level >= kMedium)
    {
        // Rings running out, bent by the noise and broken into arcs.
        const float phase = away / kFoamRingLength - time * kFoamRingRate + (broad - 0.5) * 1.6;
        const float ring = abs(frac(phase) - 0.5) * 2.0;
        const float width = kFoamRingWidth * (1.0 - 0.6 * out);
        const float edge = max(fwidth(phase) * 2.0, 0.03);
        cover = max(cover, (1.0 - smoothstep(width, width + edge, ring)) * smoothstep(0.42, 0.5, fine) * smoothstep(0.1, 0.25, out));
    }
    cover *= (1.0 - smoothstep(0.6, 1.0, out)) * lerp(1.0, 0.75 + 0.25 * fine, churn);
    // Lumps catch the light on their tops and are darker in their folds.
    float3 colour = lerp(v.color.rgb * body_light * kSheetGlow, white, kFoamWhite) * lerp(0.82, 1.08, grain);
    float alpha = cover * 0.92;
    if (level >= kUltra)
    {
        // Round the foam the lake ripples in rings running out: the slope tilts the sky it shows and the sun's glint.
        const float wave = (away / kRippleLength - time * kRippleRate) * 6.2831853;
        const float strength = kRippleSlope * (1.0 - smoothstep(0.3, 1.0, away / reach)) * (0.6 + 0.8 * broad);
        // The way out, on the ground, from how `away` changes across the screen.
        const float2 wx = ddx(v.world.xz);
        const float2 wy = ddy(v.world.xz);
        const float2 ax = float2(ddx(away), ddy(away));
        const float det = wx.x * wy.y - wx.y * wy.x;
        float2 outward = abs(det) > 1e-12 ? float2(ax.x * wy.y - ax.y * wx.y, ax.y * wx.x - ax.x * wy.x) / det : float2(0.0);
        outward = dot(outward, outward) > 1e-6 ? normalize(outward) : float2(0.0);
        const float slope = cos(wave) * strength;
        const float3 n = normalize(float3(-outward.x * slope, 1.0, -outward.y * slope));
        const float glint = pow(saturate(dot(reflect(-view, n), to_sun)), 80.0);
        const float tilt = abs(slope) / kRippleSlope;
        const float3 ripple = sky * (1.0 + 0.6 * sign(slope)) + sun * glint * 4.0;
        const float shown = saturate(0.18 * tilt * (1.0 - cover) + glint * strength * 6.0);
        colour = lerp(ripple, colour, alpha / max(alpha + shown, 1e-4));
        alpha = alpha + shown * (1.0 - alpha);
    }
    return float4(colour, alpha * far_fade(dist));
}

// A puff of mist or a droplet. uv: its corner; normal: its size, its own number, how thick it is (a droplet
// below 0); colour alpha: how much of it shows at this point of its loop; `mist`: its size as drawn and how much
// sun reaches it.
float4 mist(Varyings v, float3 view, float3 sky, float3 sun, float3 to_sun, float time, uint level)
{
    const float r2 = dot(v.uv, v.uv);
    if (r2 >= 1.0 || v.color.a <= 0.0)
        discard;
    const float3 lit = white_light(sky, sun * v.mist.y, to_sun);
    if (v.normal.z < 0.0)
        return float4(lit * 1.1, (1.0 - r2) * v.color.a * 0.85);
    float alpha = (1.0 - r2) * (1.0 - r2) * v.color.a * kMistAlpha;
    if (level >= kHigh)
    {
        // Wisps drifting up through it.
        const float wisp = u_waves.Sample(u_sampler, v.uv * 0.21 + float2(v.normal.y * 7.3, v.normal.y * 3.1 + time * 0.035)).r;
        alpha *= 0.5 + wisp;
    }
    // Soft where it meets what stands behind it, and gone before it would cover the eye. Without the scene's
    // depth the puffs stand out from the cliff instead (park/waterfall_mesh.cpp), and their edges are softer.
    if (has(kRefraction))
        alpha *= saturate((scene_distance(screen_uv(v)) - v.clip.w) / min(0.15 * v.mist.x, kMistSoft));
    else
        alpha *= 1.0 - r2;
    alpha *= saturate((v.clip.w - 2.0) / 8.0);
    // Seen from above, the mist is a thin layer over the foam, not the wall it is from the side.
    alpha *= 1.0 - kMistAbove * smoothstep(0.5, 0.95, view.y);
    // Fine drops scatter sunlight mostly forward: bright looking towards the sun through it.
    const float towards = saturate(dot(-view, to_sun));
    const float3 colour = lit * kMistBright + sun * v.mist.y * max(to_sun.y, 0.0) * kMistForward * pow(towards, 4.0);
    return float4(colour, alpha);
}

[shader("fragment")]
float4 fragment_main(Varyings v) : SV_Target
{
    const float time = u_draw.eye_time.w;
    const uint level = detail();
    const uint drawn = part();
    // Mist takes its shadow by the puff, the cheapest look none.
    const float shadowed = has(kShadow) && drawn != kMist && level >= kMedium ? sunlit(v.world) : 1.0;
    const float3 sun = u_draw.sun_far.rgb * shadowed;
    const float3 to_sun = normalize(u_draw.sun_dir_near.xyz);
    const float3 sky = u_draw.sky_flags.rgb;
    const float3 to_eye = u_draw.eye_time.xyz - v.world;
    const float dist = length(to_eye);
    const float3 view = to_eye / max(dist, 1e-4);
    if (drawn == kMist)
        return mist(v, view, sky, u_draw.sun_far.rgb, to_sun, time, level);
    // As the lake's water is lit.
    const float3 body_light = sky + sun * max(to_sun.y, 0.0) * 0.5;
    const float3 white = white_light(sky, sun, to_sun);
    if (drawn == kSheet)
        return sheet(v, view, dist, sky, body_light, white, time, level);
    return foam(v, view, dist, sky, sun, to_sun, body_light, white, time, level);
}
