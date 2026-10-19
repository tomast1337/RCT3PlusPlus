// SPDX-License-Identifier: GPL-3.0-or-later
#include "audio/asf_file.hpp"

#include "sine_wma.hpp"
#include "tiny_wmv.hpp"

#include <cstddef>
#include <doctest/doctest.h>
#include <span>
#include <vector>

using namespace rct3::audio;

namespace
{
    std::span<const std::byte> sine_file()
    {
        return std::as_bytes(std::span(rct3::test::kSineWma));
    }

    std::vector<std::byte> bytes(std::initializer_list<int> list)
    {
        std::vector<std::byte> out;
        for (const int b : list)
            out.push_back(static_cast<std::byte>(b));
        return out;
    }
} // namespace

TEST_CASE("the header gives the audio stream's format and the packets' size")
{
    const auto size = asf_header_size(sine_file().first(30));
    REQUIRE(size);
    const auto h = read_asf_header(sine_file().first(*size));
    REQUIRE(h);
    CHECK(h->audio.codec == 0x161);
    CHECK(h->audio.channels == 2);
    CHECK(h->audio.sample_rate == 22050);
    CHECK(h->audio.bytes_per_second == 4000);
    CHECK(h->audio.block_align > 0);
    CHECK(h->audio.extra.size() == 10);
    CHECK(h->stream == 1);
    CHECK(h->packet_size == 512);
    CHECK(h->packet_count == (sine_file().size() - h->first_packet) / 512);
    CHECK(h->seconds == doctest::Approx(0.278).epsilon(0.01));
}

TEST_CASE("files that are not ASF or are cut short are refused")
{
    CHECK_FALSE(asf_header_size(bytes({ 1, 2, 3 })));
    auto other = std::vector(sine_file().begin(), sine_file().end());
    other[0] = std::byte { 0 };
    CHECK_FALSE(asf_header_size(other));
    const auto size = asf_header_size(sine_file());
    REQUIRE(size);
    CHECK_FALSE(read_asf_header(sine_file().first(*size - 1)));
}

TEST_CASE("a packet's payloads are read with their object and offset")
{
    // Several payloads; the packet and padding lengths, object numbers, replicated data lengths and payload
    // lengths a byte each, offsets four bytes.
    std::vector<std::byte> packet;
    for (const auto& part : {
             bytes({ 0x82, 0, 0 }),                                                    // Two bytes of error correction
             bytes({ 0x01 | 0x08 | 0x20, 0x01 | 0x0C | 0x10 | 0x40 }),                 // Length types
             bytes({ 44, 3, 0, 0, 0, 0, 0, 0 }),                                       // Packet length, padding, send time, duration
             bytes({ 0x40 | 2 }),                                                      // Two payloads
             bytes({ 0x81, 7, 0, 0, 0, 0, 8, 6, 0, 0, 0, 0, 0, 0, 0, 2, 0xAA, 0xBB }), // Object 7 at 0, of 6 bytes
             bytes({ 0x01, 7, 2, 0, 0, 0, 0, 1, 0xCC }),                               // Object 7 at 2, nothing replicated
             bytes({ 0, 0, 0 }),                                                       // Padding
         })
        packet.insert(packet.end(), part.begin(), part.end());
    const auto payloads = read_asf_packet(packet);
    REQUIRE(payloads);
    REQUIRE(payloads->size() == 2);
    CHECK((*payloads)[0].stream == 1);
    CHECK((*payloads)[0].object == 7);
    CHECK((*payloads)[0].offset == 0);
    CHECK((*payloads)[0].object_size == 6);
    CHECK((*payloads)[0].data.size() == 2);
    CHECK((*payloads)[1].offset == 2);
    CHECK((*payloads)[1].object_size == 3); // No replicated data: the object ends with the payload
    CHECK((*payloads)[1].data[0] == std::byte { 0xCC });

    auto cut = packet;
    cut.resize(26);
    CHECK_FALSE(read_asf_packet(cut));
}

TEST_CASE("media objects are put together and split into blocks")
{
    const auto data = bytes({ 1, 2, 3, 4, 5, 6 });
    const std::span<const std::byte> all(data);
    AsfAudioBlocks blocks(1, 3);
    blocks.add({ 2, 9, 0, 6, all }); // Another stream
    CHECK(blocks.empty());
    blocks.add({ 1, 4, 3, 6, all.subspan(3) }); // The middle of an object whose start was missed
    CHECK(blocks.empty());
    blocks.add({ 1, 5, 0, 6, all.first(4) });
    CHECK(blocks.empty());
    blocks.add({ 1, 5, 4, 6, all.subspan(4) });
    REQUIRE_FALSE(blocks.empty());
    CHECK(blocks.take() == bytes({ 1, 2, 3 }));
    CHECK(blocks.take() == bytes({ 4, 5, 6 }));
    CHECK(blocks.empty());

    blocks.add({ 1, 6, 0, 6, all.first(2) });
    blocks.add({ 1, 6, 3, 6, all.subspan(3) }); // A piece missing: the object is lost
    CHECK(blocks.empty());

    blocks.add({ 1, 7, 0, 0xFFFFFFFFu, all }); // Claims more than an audio object can be: not held
    blocks.add({ 1, 7, 6, 0xFFFFFFFFu, all });
    CHECK(blocks.empty());
}

TEST_CASE("a video stream gives its picture's size and codec, and its frames whole, timed and keyed")
{
    const auto file = std::as_bytes(std::span(rct3::test::kTinyWmv));
    const auto size = asf_header_size(file.first(30));
    REQUIRE(size);
    const auto h = read_asf_header(file.first(*size));
    REQUIRE(h);
    REQUIRE(h->video);
    CHECK(h->video->width == 32);
    CHECK(h->video->height == 16);
    CHECK(h->video->codec == 0x32564D57); // "WMV2"
    CHECK(h->audio.codec == 0x161);       // The sound beside it is still read
    CHECK(h->video_stream != h->stream);

    AsfObjects frames(h->video_stream);
    for (std::size_t at = h->first_packet; at + h->packet_size <= file.size(); at += h->packet_size)
    {
        const auto payloads = read_asf_packet(file.subspan(at, h->packet_size));
        REQUIRE(payloads);
        for (const AsfPayload& p : *payloads)
            frames.add(p);
    }
    std::vector<AsfObject> got;
    while (!frames.empty())
        got.push_back(frames.take());
    REQUIRE(got.size() == 3);
    CHECK(got[0].key);
    CHECK(got[0].time >= h->preroll);
    CHECK(got[1].time == got[0].time + 100); // A tenth of a second apart
    CHECK(got[2].time == got[0].time + 200);
    CHECK_FALSE(got[1].data.empty());
}
