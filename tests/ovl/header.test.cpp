// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <string>
#include <vector>

#include "ovl/ovl.hpp"
#include "builder.hpp"

using rct3::ovl::Error;
using rct3::ovl::parse_header;
using rct3::test::Builder;

namespace {

// v5, no sub-version block: one external ref, two loaders, with symbol counts.
std::vector<std::byte> make_v5() {
  Builder b;
  b.raw("FGRK", 4);
  b.u8(0x02);
  b.u8(0), b.u8(0), b.u8(0);
  b.u32(5);
  b.u32(0x10);
  b.u16(0);  // subVersionFlag
  b.u16(0);  // extraFlag
  b.u32(1);
  b.str("Sounds\\Sounds");
  b.u32(0);  // secondary refs
  b.u32(2);
  b.str("FGDK"), b.str("FlexiTexture"), b.u32(1), b.str("ftx");
  b.str("RCT3"), b.str("StaticShape"), b.u32(1), b.str("shs");
  b.u32(0), b.u32(4);
  b.u32(1), b.u32(7);
  return b.bytes;
}

// v5 with one data stream and a stream block of `size` bytes holding `tag`, zero padded.
std::vector<std::byte> make_v5_streamed(std::uint32_t size, const std::string& tag) {
  Builder b;
  b.raw("FGRK", 4);
  b.u8(0x02);
  b.u8(0), b.u8(0), b.u8(0);
  b.u32(5);
  b.u32(0x10);
  b.u16(1);  // subVersionFlag
  b.u16(0);
  b.u32(size);
  b.u32(0), b.u32(0);
  b.raw(tag.c_str(), tag.size() + 1);
  while (b.bytes.size() < 0x14 + size) b.u8(0);
  b.u32(0);  // external refs
  b.u32(0);  // secondary refs
  b.u32(1);
  b.str("FGDK"), b.str("Model"), b.u32(1), b.str("mdl");
  b.u32(0), b.u32(2);
  return b.bytes;
}

void check_every_prefix_fails(const std::vector<std::byte>& file) {
  for (std::size_t n = 0; n < file.size(); ++n) {
    INFO("prefix length " << n);
    CHECK_FALSE(parse_header(std::span<const std::byte>(file.data(), n)).has_value());
  }
}

}  // namespace

TEST_CASE("v5 header parses references, loaders and symbol counts") {
  const auto file = make_v5();
  auto h = parse_header(file);
  REQUIRE(h.has_value());

  CHECK(h->version == 5);
  REQUIRE(h->external_refs.size() == 1);
  CHECK(h->external_refs[0] == "Sounds\\Sounds");
  CHECK(h->secondary_refs.empty());

  REQUIRE(h->loaders.size() == 2);
  CHECK(h->loaders[0].name == "FlexiTexture");
  CHECK(h->loaders[0].tag == "ftx");
  CHECK(h->loaders[0].symbol_count == 4);
  CHECK(h->loaders[1].loader == "RCT3");
  CHECK(h->loaders[1].symbol_count == 7);
  CHECK(h->end_offset == file.size());

  SUBCASE("every truncated prefix fails") { check_every_prefix_fails(file); }
}

TEST_CASE("v5 stream block") {
  // Both variants seen in real files: size 20 with a tag, size 16 with an empty tag.
  struct Variant {
    std::uint32_t size;
    std::string tag;
  };
  for (const auto& v : {Variant{20, "tg"}, Variant{16, ""}}) {
    CAPTURE(v.size);
    const auto file = make_v5_streamed(v.size, v.tag);
    auto h = parse_header(file);
    REQUIRE(h.has_value());

    CHECK(h->stream_count == 1);
    CHECK(h->stream_tag == v.tag);
    REQUIRE(h->loaders.size() == 1);
    CHECK(h->loaders[0].tag == "mdl");
    CHECK(h->loaders[0].symbol_count == 2);
    CHECK(h->end_offset == file.size());

    check_every_prefix_fails(file);
  }
}

TEST_CASE("rejects bad input") {
  const auto file = make_v5();

  SUBCASE("bad magic") {
    auto bad = file;
    bad[0] = std::byte{'X'};
    CHECK(parse_header(bad).error() == Error::BadMagic);
  }
  SUBCASE("unsupported version") {
    auto bad = file;
    bad[8] = std::byte{9};
    CHECK(parse_header(bad).error() == Error::UnsupportedVersion);
  }
  SUBCASE("stream block smaller than its own header") {
    auto bad = make_v5_streamed(20, "tg");
    bad[0x14] = std::byte{8};
    CHECK(parse_header(bad).error() == Error::BadStreamBlock);
  }
}
