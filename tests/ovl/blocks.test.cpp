// SPDX-License-Identifier: GPL-3.0-or-later
#include <doctest/doctest.h>

#include <cstdint>
#include <span>
#include <vector>

#include "builder.hpp"
#include "ovl/ovl.hpp"

using rct3::ovl::Error;
using rct3::ovl::parse;

namespace {

// Blocks filled with a marker byte, sized per type.
std::vector<std::byte> make(std::uint32_t version, const std::vector<std::vector<std::uint32_t>>& sizes,
                            const std::vector<std::uint32_t>& relocations) {
  rct3::test::BlockBytes blocks;
  for (const auto& type : sizes) {
    auto& out = blocks.emplace_back();
    for (auto size : type) out.emplace_back(size, std::byte{0xAA});
  }
  return rct3::test::build_file(version, blocks, relocations);
}

}  // namespace

TEST_CASE("blocks get consecutive relative offsets across types") {
  for (const std::uint32_t version : {1u, 4u, 5u}) {
    CAPTURE(version);
    const std::vector<std::vector<std::uint32_t>> sizes = {{8, 4}, {}, {16}};
    const auto file = make(version, sizes, {0, 12});
    auto f = parse(file);
    REQUIRE(f.has_value());

    CHECK(f->data_size == 28);
    REQUIRE(f->block_types[0].blocks.size() == 2);
    CHECK(f->block_types[0].blocks[0].relative_offset == 0);
    CHECK(f->block_types[0].blocks[1].relative_offset == 8);
    CHECK(f->block_types[1].blocks.empty());
    REQUIRE(f->block_types[2].blocks.size() == 1);
    CHECK(f->block_types[2].blocks[0].relative_offset == 12);
    CHECK(f->block_types[2].blocks[0].size == 16);
    CHECK(f->relocations == std::vector<std::uint32_t>{0, 12});
    CHECK(f->end_offset == file.size());
  }
}

TEST_CASE("post-block metadata is kept per version") {
  const auto v4 = parse(make(4, {}, {}));
  REQUIRE(v4.has_value());
  CHECK(v4->unknown_words.size() == 2);

  const auto v5 = parse(make(5, {}, {}));
  REQUIRE(v5.has_value());
  CHECK(v5->unknown_bytes.size() == 3);
  CHECK(v5->unknown_words == std::vector<std::uint32_t>{7, 8});

  const auto v1 = parse(make(1, {}, {}));
  REQUIRE(v1.has_value());
  CHECK(v1->unknown_bytes.empty());
  CHECK(v1->unknown_words.empty());
}

TEST_CASE("locate finds the block and offset, honouring a base") {
  const auto file = make(5, {{8, 4}, {}, {16}}, {});
  const auto f = parse(file);
  REQUIRE(f.has_value());

  const auto in_first = f->locate(5);
  REQUIRE(in_first.has_value());
  CHECK(in_first->type == 0);
  CHECK(in_first->index == 0);
  CHECK(in_first->offset == 5);

  const auto in_third = f->locate(13);
  REQUIRE(in_third.has_value());
  CHECK(in_third->type == 2);
  CHECK(in_third->offset == 1);

  CHECK_FALSE(f->locate(28).has_value());

  // A unique file's addresses start after the common file's data.
  const auto shifted = f->locate(100 + 9, 100);
  REQUIRE(shifted.has_value());
  CHECK(shifted->index == 1);
  CHECK(shifted->offset == 1);
  CHECK_FALSE(f->locate(99, 100).has_value());
}

TEST_CASE("truncated and oversized block data fail cleanly") {
  const auto file = make(5, {{8, 4}, {}, {16}}, {0, 12});

  SUBCASE("every prefix fails") {
    for (std::size_t n = 0; n < file.size(); ++n) {
      INFO("prefix length " << n);
      CHECK_FALSE(parse(std::span<const std::byte>(file.data(), n)).has_value());
    }
  }

  SUBCASE("block count larger than the file is refused before allocating") {
    auto bad = make(5, {}, {});
    // Header is 0x14 bytes, then external refs, secondary refs and loader counts, then type 0's count.
    for (std::size_t i = 0x14 + 12; i < 0x14 + 16; ++i) bad[i] = std::byte{0xFF};
    CHECK(parse(bad).error() == Error::ImplausibleCount);
  }

  SUBCASE("relocation count larger than the file is refused") {
    auto bad = make(5, {}, {});
    // The relocation count is the last u32.
    for (std::size_t i = bad.size() - 4; i < bad.size(); ++i) bad[i] = std::byte{0xFF};
    CHECK(parse(bad).error() == Error::ImplausibleCount);
  }
}
