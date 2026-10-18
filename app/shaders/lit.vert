// SPDX-License-Identifier: GPL-3.0-or-later
#version 450

// render::Shader::Lit and LitCutout. Positions and normals are in world space.
layout(location = 0) in vec3 a_position;
layout(location = 1) in vec4 a_color;
layout(location = 2) in vec2 a_uv;
layout(location = 3) in vec3 a_normal;

layout(push_constant) uniform Draw {
  mat4 transform;
} u_draw;

layout(location = 0) out vec4 v_color;
layout(location = 1) out vec2 v_uv;
layout(location = 2) out vec3 v_normal;
layout(location = 3) out vec3 v_world;
layout(location = 4) flat out vec2 v_cross; // Not crossing levels (lit_crossing.vert)
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
  v_cross = vec2(0.0);
  v_eye = eye_of(u_draw.transform);
}
