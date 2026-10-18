// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::LitCrossing and LitCrossingCutout: as Lit, for an object drawn at two levels of detail while
// the eye's distance runs through its band. Positions and normals are in world space.
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_normal;
layout(location = 4) in vec4 a_band; // The object's anchor, and the distance its crossing starts at
// The distance it ends at, and 1 on the coarser level, -1 on the finer; 2 and -2 tint them to show them.
layout(location = 5) in vec2 a_side;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec3 v_world;
// How far through its band the object is, and its side; side 0 draws it whole.
layout(location = 4) flat out vec2 v_cross;
layout(location = 5) flat out vec4 v_eye;

// Where the eye is, w 1: the point the transform sends to clip x = y = w = 0, where the planes of those three
// rows meet. A view without perspective has no such point (its w row is flat): w 0.
vec4 eye_of(mat4 m) {
  vec4 x = vec4(m[0][0], m[1][0], m[2][0], m[3][0]);
  vec4 y = vec4(m[0][1], m[1][1], m[2][1], m[3][1]);
  vec4 w = vec4(m[0][3], m[1][3], m[2][3], m[3][3]);
  float det = dot(x.xyz, cross(y.xyz, w.xyz));
  if (abs(det) < 1e-30)
    return vec4(0.0);
  return vec4(-(x.w * cross(y.xyz, w.xyz) + y.w * cross(w.xyz, x.xyz) + w.w * cross(x.xyz, y.xyz)) / det, 1.0);
}

void main() {
  gl_Position = u_draw.transform * vec4(a_position, 1.0);
  v_color = a_color;
  v_uv = a_uv;
  v_normal = a_normal;
  v_world = a_position;
  // A view without perspective draws both levels whole.
  v_eye = eye_of(u_draw.transform);
  if (v_eye.w < 0.5) {
    v_cross = vec2(0.0);
    return;
  }
  float through = clamp((distance(v_eye.xyz, a_band.xyz) - a_band.w) / max(a_side.x - a_band.w, 1e-3), 0.0, 1.0);
  v_cross = vec2(through, a_side.y);
}
