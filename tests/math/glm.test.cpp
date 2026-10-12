// SPDX-License-Identifier: GPL-3.0-or-later
//
// Pins the GLM conventions the rest of the code relies on, with invented matrices.
#include <doctest/doctest.h>

#include <array>
#include <cstring>

#include "math/glm.hpp"

TEST_CASE("row-major row-vector matrices copy into glm as the same transform") {
  // Row-vector convention: translation lives in the fourth row (elements 12..14).
  const std::array<float, 16> rows = {
      1, 0, 0, 0,  //
      0, 1, 0, 0,  //
      0, 0, 1, 0,  //
      2, 3, 4, 1,
  };
  glm::mat4 m;
  static_assert(sizeof(m) == sizeof(rows));
  std::memcpy(&m, rows.data(), sizeof(m));
  const glm::vec4 p = m * glm::vec4(1.0f, 1.0f, 1.0f, 1.0f);
  CHECK(p.x == doctest::Approx(3.0f));
  CHECK(p.y == doctest::Approx(4.0f));
  CHECK(p.z == doctest::Approx(5.0f));
}

TEST_CASE("coordinates are left-handed") {
  // Looking down +Z from the origin, +X is to the right.
  const glm::mat4 view = glm::lookAt(glm::vec3(0, 0, 0), glm::vec3(0, 0, 1), glm::vec3(0, 1, 0));
  const glm::vec4 right = view * glm::vec4(1, 0, 0, 1);
  const glm::vec4 ahead = view * glm::vec4(0, 0, 5, 1);
  CHECK(right.x == doctest::Approx(1.0f));
  CHECK(ahead.z == doctest::Approx(5.0f));
}

TEST_CASE("clip depth runs from zero to one") {
  const glm::mat4 proj = glm::perspective(glm::radians(60.0f), 1.0f, 1.0f, 100.0f);
  const glm::vec4 near_pt = proj * glm::vec4(0, 0, 1.0f, 1);
  const glm::vec4 far_pt = proj * glm::vec4(0, 0, 100.0f, 1);
  CHECK(near_pt.z / near_pt.w == doctest::Approx(0.0f).epsilon(1e-4));
  CHECK(far_pt.z / far_pt.w == doctest::Approx(1.0f).epsilon(1e-4));
}
