// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Shape. Follows shade_shape() in ui/shape.cpp step for step; change both together.
layout(location = 0) in vec4 v_local;
layout(location = 1) flat in vec4 v_radii;
layout(location = 2) flat in vec4 v_edge;
layout(location = 3) flat in vec4 v_top;
layout(location = 4) flat in vec4 v_bottom;
layout(location = 5) flat in vec4 v_outline;
layout(location = 6) flat in vec4 v_light;
layout(location = 0) out vec4 out_color;

float side(float v) { return v < 0.0 ? -1.0 : 1.0; }

float corner_radius(vec2 p) {
  if (p.y < 0.0)
    return p.x < 0.0 ? v_radii.x : v_radii.y;
  return p.x < 0.0 ? v_radii.w : v_radii.z;
}

float box_distance(vec2 p, vec2 half_size) {
  float r = corner_radius(p);
  vec2 q = abs(p) - half_size + r;
  return length(max(q, 0.0)) + min(max(q.x, q.y), 0.0) - r;
}

float facing_up(vec2 p, vec2 half_size, float ring) {
  float r = corner_radius(p);
  vec2 q = abs(p) - half_size + r;
  float ny = 0.0;
  if (q.x > 0.0 && q.y > 0.0)
    ny = side(p.y) * q.y / length(q);
  else if (q.y >= q.x)
    ny = side(p.y);
  if (ring > 0.0 && box_distance(p, half_size) < -ring / 2.0)
    ny = -ny;
  return clamp(-ny, 0.0, 1.0);
}

vec4 over(vec4 under, vec4 top, float amount) {
  float a = top.a * amount;
  float result = a + under.a * (1.0 - a);
  if (result <= 0.0)
    return vec4(0.0);
  return vec4((top.rgb * a + under.rgb * under.a * (1.0 - a)) / result, result);
}

void main() {
  vec2 p = v_local.xy;
  vec2 half_size = v_local.zw;
  float softness = v_edge.x;
  float outline_width = v_edge.y;
  float light_width = v_edge.z;
  float ring = v_edge.w;

  float d = box_distance(p, half_size);
  if (ring > 0.0)
    d = abs(d + ring / 2.0) - ring / 2.0;
  float t = clamp(0.5 - d / max(softness, 1.0), 0.0, 1.0);
  float coverage = softness > 1.0 ? t * t * (3.0 - 2.0 * t) : t;
  if (coverage <= 0.0)
    discard;

  float along = half_size.y > 0.0 ? clamp((p.y + half_size.y) / (2.0 * half_size.y), 0.0, 1.0) : 0.0;
  vec4 color = mix(v_top, v_bottom, along);
  float inside = -d;
  if (light_width > 0.0)
    color = over(color, v_light, clamp(1.0 - inside / light_width, 0.0, 1.0) * facing_up(p, half_size, ring));
  if (outline_width > 0.0)
    color = over(color, v_outline, clamp(outline_width - inside + 0.5, 0.0, 1.0));
  color.a *= coverage;
  out_color = color;
}
